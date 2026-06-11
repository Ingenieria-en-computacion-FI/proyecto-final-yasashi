/* ============================================================
 * src/main.c  –  Pipeline principal del Ensamblador/Linker IA-32
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "lexer.h"
#include "parser.h"
#include "symtab.h"
#include "ia32_types.h"
#include "encoder.h"
#include "object.h"
#include "linker.h"

#define MAX_INSTRUCTIONS 1024

typedef struct {
    Instruction   instr;         /* Instrucción tal como la entrega parser.c */
    EncodedInstruction enc;     /* Bytes generados por el encoder           */
    int           needs_reloc;   /* 1 si tiene un placeholder de 4 bytes     */
    char          reloc_sym[64]; /* Nombre del símbolo a resolver            */
    uint32_t      reloc_offset;  /* Offset del placeholder dentro de .text   */
} AsmEntry;

static const struct { const char *str; Mnemonic mn; } mn_table[] = {
    {"MOV",  MN_MOV},  {"PUSH", MN_PUSH}, {"POP",  MN_POP},
    {"LEA",  MN_LEA},  {"ADD",  MN_ADD},  {"SUB",  MN_SUB},
    {"INC",  MN_INC},  {"DEC",  MN_DEC},  {"CMP",  MN_CMP},
    {"NEG",  MN_NEG},  {"MUL",  MN_MUL},  {"DIV",  MN_DIV},
    {"AND",  MN_AND},  {"OR",   MN_OR},   {"XOR",  MN_XOR},
    {"NOT",  MN_NOT},  {"JMP",  MN_JMP},  {"JE",   MN_JE},
    {"JNE",  MN_JNE},  {"JG",   MN_JG},   {"JL",   MN_JL},
    {"JGE",  MN_JGE},  {"JLE",  MN_JLE},  {"CALL", MN_CALL},
    {"RET",  MN_RET},  {"NOP",  MN_NOP},  {"INT",  MN_INT},
    {NULL,   MN_UNKNOWN}
};

static const struct { const char *str; Reg32 r; } reg_table[] = {
    {"EAX", REG_EAX}, {"ECX", REG_ECX}, {"EDX", REG_EDX},
    {"EBX", REG_EBX}, {"ESP", REG_ESP}, {"EBP", REG_EBP},
    {"ESI", REG_ESI}, {"EDI", REG_EDI},
    {NULL,  REG_NONE}
};

static FILE   *open_source(const char *path);
static int     run_pass1(FILE *src);
static int     run_pass2(FILE *src, AsmEntry *entries, int *count);
static int     encode_all(AsmEntry *entries, int count);
static int     build_object(AsmEntry *entries, int count, ObjectFile **obj_out);
static int     run_linker(ObjectFile *obj, const char *out_path);
static void    fatal(const char *msg);

#define STR_EQ_CI(a,b)  (strcasecmp((a),(b)) == 0)

static Reg32 str_to_reg(const char *s) {
    for (int i = 0; reg_table[i].str; i++)
        if (STR_EQ_CI(s, reg_table[i].str)) return reg_table[i].r;
    return REG_NONE;
}

static Mnemonic str_to_mn(const char *s) {
    for (int i = 0; mn_table[i].str; i++)
        if (STR_EQ_CI(s, mn_table[i].str)) return mn_table[i].mn;
    return MN_UNKNOWN;
}

static int parse_operand(Token *tok, FILE *f, Operand *op, int *needs_reloc_out, char *reloc_sym_out) {
    memset(op, 0, sizeof(Operand));
    op->reg       = REG_NONE;
    op->index_reg = REG_NONE;
    op->scale     = SCALE_1;

    if (tok->type == TOKEN_REGISTER) {
        op->type = OP_REG;
        op->reg  = str_to_reg(tok->lexeme);
        *tok = get_next_token(f);
        return 0;
    }

    if (tok->type == TOKEN_NUMBER) {
        op->type = OP_IMM;
        op->imm  = (int32_t)strtol(tok->lexeme, NULL, 0);
        *tok = get_next_token(f);
        return 0;
    }

    if (tok->type == TOKEN_IDENTIFIER) {
        /* CORRECCIÓN: Forzar que toda etiqueta pase por relocación del Linker */
        op->type = OP_LABEL;
        strncpy(op->label, tok->lexeme, sizeof(op->label) - 1);
        op->label[sizeof(op->label) - 1] = '\0';
        
        op->needs_reloc = 1;
        /* Ponemos temporalmente un placeholder de 0; el linker resolverá el offset real */
        op->imm = 0; 

        if (needs_reloc_out) *needs_reloc_out = 1;
        if (reloc_sym_out) {
            strncpy(reloc_sym_out, tok->lexeme, 63);
            reloc_sym_out[63] = '\0';
        }
        
        *tok = get_next_token(f);
        return 0;
    }

    if (tok->type == TOKEN_LBRACKET) {
        *tok = get_next_token(f);

        if (tok->type == TOKEN_NUMBER) {
            op->type = OP_MEM_DIRECT;
            op->disp = (int32_t)strtol(tok->lexeme, NULL, 0);
            *tok = get_next_token(f);
            if (tok->type != TOKEN_RBRACKET) {
                fprintf(stderr, "Error: se esperaba ']' tras dirección directa.\n");
                return -1;
            }
            *tok = get_next_token(f);
            return 0;
        }

        if (tok->type == TOKEN_REGISTER) {
            Reg32 base = str_to_reg(tok->lexeme);
            *tok = get_next_token(f);

            if (tok->type == TOKEN_RBRACKET) {
                op->type = OP_MEM_BASE;
                op->reg  = base;
                *tok = get_next_token(f);
                return 0;
            }

            if (tok->type == TOKEN_PLUS) {
                *tok = get_next_token(f);

                if (tok->type == TOKEN_NUMBER) {
                    op->type = OP_MEM_BASE_DISP;
                    op->reg  = base;
                    op->disp = (int32_t)strtol(tok->lexeme, NULL, 0);
                    *tok = get_next_token(f);
                    if (tok->type != TOKEN_RBRACKET) {
                        fprintf(stderr, "Error: se esperaba ']'.\n");
                        return -1;
                    }
                    *tok = get_next_token(f);
                    return 0;
                }

                if (tok->type == TOKEN_REGISTER) {
                    Reg32 idx = str_to_reg(tok->lexeme);
                    *tok = get_next_token(f);

                    if (tok->type == TOKEN_STAR) {
                        *tok = get_next_token(f);
                        int sc = (int)strtol(tok->lexeme, NULL, 0);
                        SibScale scale = SCALE_1;
                        if      (sc == 2) scale = SCALE_2;
                        else if (sc == 4) scale = SCALE_4;
                        else if (sc == 8) scale = SCALE_8;
                        *tok = get_next_token(f);

                        op->type      = OP_MEM_SIB;
                        op->reg       = base;
                        op->index_reg = idx;
                        op->scale     = scale;
                        op->disp      = 0;

                        if (tok->type == TOKEN_PLUS) {
                            *tok = get_next_token(f);
                            op->disp = (int32_t)strtol(tok->lexeme, NULL, 0);
                            *tok = get_next_token(f);
                        }
                        if (tok->type != TOKEN_RBRACKET) {
                            fprintf(stderr, "Error: se esperaba ']' en SIB.\n");
                            return -1;
                        }
                        *tok = get_next_token(f);
                        return 0;
                    }

                    op->type      = OP_MEM_BASE_IDX;
                    op->reg       = base;
                    op->index_reg = idx;
                    if (tok->type == TOKEN_PLUS) {
                        *tok = get_next_token(f);
                        op->disp = (int32_t)strtol(tok->lexeme, NULL, 0);
                        op->type = OP_MEM_SIB;
                        *tok = get_next_token(f);
                    }
                    if (tok->type != TOKEN_RBRACKET) {
                        fprintf(stderr, "Error: se esperaba ']' en base+idx.\n");
                        return -1;
                    }
                    *tok = get_next_token(f);
                    return 0;
                }
            }
        }
        fprintf(stderr, "Error: modo de direccionamiento no reconocido.\n");
        return -1;
    }

    fprintf(stderr, "Error: operando desconocido '%s'.\n", tok->lexeme);
    return -1;
}

int main(int argc, char *argv[])
{
    printf("==============================================\n");
    printf("  Ensamblador/Linker IA-32  –  v1.0\n");
    printf("==============================================\n\n");

    if (argc < 3) {
        fprintf(stderr, "Uso: %s <archivo.asm> <salida>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *src_path = argv[1];
    const char *out_path = argv[2];

    FILE *src = open_source(src_path);

    printf(">>> PASS 1: Análisis léxico/sintáctico (symtab)\n");
    if (run_pass1(src) != 0) {
        fclose(src);
        fatal("Pass 1 falló. Abortando.");
    }
    print_symbol_table();

    rewind(src);

    printf("\n>>> PASS 2: Generación de instrucciones con direcciones\n");
    AsmEntry entries[MAX_INSTRUCTIONS];
    memset(entries, 0, sizeof(entries));
    int count = 0;

    if (run_pass2(src, entries, &count) != 0) {
        fclose(src);
        fatal("Pass 2 falló. Abortando.");
    }
    fclose(src);
    printf("Pass 2: %d instrucciones procesadas.\n", count);

    printf("\n>>> ENCODER: Codificación IA-32\n");
    if (encode_all(entries, count) != 0) {
        fatal("Encoder falló. Abortando.");
    }

    printf("\n>>> OBJECT: Construcción del archivo objeto\n");
    ObjectFile *obj = NULL;
    if (build_object(entries, count, &obj) != 0) {
        fatal("Construcción del objeto falló. Abortando.");
    }
    obj_print(obj);

    printf("\n>>> LINKER: Enlazado y escritura de '%s'\n", out_path);
    if (run_linker(obj, out_path) != 0) {
        obj_free(obj);
        fatal("Linker falló. Abortando.");
    }

    obj_free(obj);

    printf("\n==============================================\n");
    printf("  Binario generado: %s\n", out_path);
    printf("==============================================\n");
    return EXIT_SUCCESS;
}

static FILE *open_source(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Error: No se puede abrir '%s'.\n", path);
        exit(EXIT_FAILURE);
    }
    printf("Fuente: '%s' abierto correctamente.\n\n", path);
    return f;
}

static int run_pass1(FILE *src)
{
    parse_program(src);
    return 0;
}

static int run_pass2(FILE *src, AsmEntry *entries, int *count)
{
    *count = 0;
    Token tok = get_next_token(src);

    while (tok.type != TOKEN_EOF) {
        if (tok.type == TOKEN_NEWLINE) {
            tok = get_next_token(src);
            continue;
        }

        if (tok.type != TOKEN_IDENTIFIER) {
            fprintf(stderr, "Error Pass 2: token inesperado '%s'.\n", tok.lexeme);
            return -1;
        }

        char word[64];
        strncpy(word, tok.lexeme, sizeof(word) - 1);
        word[sizeof(word) - 1] = '\0';
        tok = get_next_token(src);

        if (tok.type == TOKEN_COLON) {
            tok = get_next_token(src);
            continue;
        }

        Mnemonic mn = str_to_mn(word);
        if (mn == MN_UNKNOWN) {
            fprintf(stderr, "Error Pass 2: mnemónico desconocido '%s'.\n", word);
            return -1;
        }

        if (*count >= MAX_INSTRUCTIONS) {
            return -1;
        }

        AsmEntry *e = &entries[*count];
        memset(e, 0, sizeof(AsmEntry));
        e->instr.mnemonic = mn;
        
        e->instr.dst.type = OP_NONE;
        e->instr.src.type = OP_NONE;
        e->instr.dst.reg  = REG_NONE;
        e->instr.src.reg  = REG_NONE;

        if (mn == MN_RET || mn == MN_NOP) {
            (*count)++;
            while (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF) tok = get_next_token(src);
            if (tok.type == TOKEN_NEWLINE) tok = get_next_token(src);
            continue;
        }

        if (mn == MN_INT) {
            if (tok.type == TOKEN_NUMBER) {
                e->instr.dst.type = OP_IMM;
                e->instr.dst.imm  = (int32_t)strtol(tok.lexeme, NULL, 0);
                tok = get_next_token(src);
            }
            (*count)++;
            while (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF) tok = get_next_token(src);
            if (tok.type == TOKEN_NEWLINE) tok = get_next_token(src);
            continue;
        }

        if (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF) {
            if (parse_operand(&tok, src, &e->instr.dst, &e->needs_reloc, e->reloc_sym) != 0) return -1;
        }

        if (tok.type == TOKEN_COMMA) {
            tok = get_next_token(src);
            if (parse_operand(&tok, src, &e->instr.src, &e->needs_reloc, e->reloc_sym) != 0) return -1;
        }

        (*count)++;

        while (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF) tok = get_next_token(src);
        if (tok.type == TOKEN_NEWLINE) tok = get_next_token(src);
    }

    return 0;
}

static int encode_all(AsmEntry *entries, int count)
{
    int ok = 1;
    uint32_t real_lc = 0;

    for (int i = 0; i < count; i++) {
        entries[i].instr.address = real_lc; 
        entries[i].enc = encoder_encode(&entries[i].instr);

        if (entries[i].enc.error) {
            fprintf(stderr, "Encoder[%d] error en instrucción %d (mn=%d): %s\n",
                    i, i, entries[i].instr.mnemonic, entries[i].enc.error_msg);
            ok = 0;
        } else {
            printf("  [0x%04X] ", entries[i].instr.address);
            encoder_dump(&entries[i].enc);
            real_lc += entries[i].enc.length; 
        }
    }
    return ok ? 0 : -1;
}

/* * CONSTRUCCIÓN DE OBJETO COMPLETA:
 * Exporta todas las etiquetas hacia la estructura del Objeto marcadas como SYM_GLOBAL
 * para que el Linker pueda enlazarlas y resolver sus relocaciones de forma unificada.
 */
static int build_object(AsmEntry *entries, int count, ObjectFile **obj_out)
{
    ObjectFile *obj = obj_create();
    if (!obj) return -1;

    int text_idx = obj_add_section(obj, ".text");
    if (text_idx < 0) {
        obj_free(obj);
        return -1;
    }

    /* Punto de entrada global estándar */
    obj_add_symbol(obj, "_start", 0, text_idx, SYM_GLOBAL);

    /* * CORRECCIÓN REQUERIDA: Exportar las etiquetas internas como SYM_GLOBAL
     * Cambiar de SYM_LOCAL a SYM_GLOBAL hace visibles los símbolos internos para
     * que la rutina del Linker pueda rellenar los mapeos sin declararlos indefinidos.
     */
    static const char *etiquetas[] = {"inicio", "ciclo_espera", "procesar_sib", "rutina_interna", "fin"};
    for (size_t s = 0; s < sizeof(etiquetas)/sizeof(etiquetas[0]); s++) {
        SymTabEntry *sym = get_symbol(etiquetas[s]);
        if (sym && sym->defined) {
            obj_add_symbol(obj, etiquetas[s], sym->address, text_idx, SYM_GLOBAL);
        }
    }

    uint32_t text_off = 0;

    for (int i = 0; i < count; i++) {
        AsmEntry *e = &entries[i];

        if (e->enc.length > 0) {
            if (obj_write_bytes(obj, text_idx, e->enc.bytes, (uint32_t)e->enc.length) != 0) {
                obj_free(obj);
                return -1;
            }
        }

        /* * GENERACIÓN DE RELOCACIONES */
        if (e->needs_reloc && e->reloc_sym[0] != '\0') {
            uint32_t patch_offset;
            Mnemonic mn = e->instr.mnemonic;
            
            /* Determinar dónde se encuentra el offset del operando de salto dentro de la instrucción */
            if (mn == MN_JE || mn == MN_JNE || mn == MN_JG ||
                mn == MN_JL || mn == MN_JGE || mn == MN_JLE) {
                patch_offset = text_off + 2; /* Opcode largo (0x0F 0x8X) de 2 bytes */
            } else {
                patch_offset = text_off + 1; /* Opcodes cortos (JMP/CALL) de 1 byte */
            }

            int sym_idx = obj_find_symbol(obj, e->reloc_sym);
            if (sym_idx < 0) {
                /* Si no es una de las locales conocidas, se registra como externa */
                sym_idx = obj_add_symbol(obj, e->reloc_sym, 0, -1, SYM_EXTERN);
            }

            if (sym_idx >= 0) {
                /* Registramos la relocalización REL32 en el objeto */
                obj_add_reloc(obj, patch_offset, sym_idx, RELOC_REL32, -4);
            }
        }
        text_off += (uint32_t)e->enc.length;
    }

    *obj_out = obj;
    return 0;
}

static int run_linker(ObjectFile *obj, const char *out_path)
{
    LinkerCtx *ctx = linker_create();
    if (!ctx) return -1;

    if (linker_add_object(ctx, obj) != 0) {
        linker_free(ctx);
        return -1;
    }

    int result = linker_link(ctx, out_path);
    if (result == 0) {
        linker_print(ctx);
    }

    linker_free(ctx);
    return result;
}

static void fatal(const char *msg)
{
    fprintf(stderr, "\n[FATAL] %s\n", msg);
    exit(EXIT_FAILURE);
}