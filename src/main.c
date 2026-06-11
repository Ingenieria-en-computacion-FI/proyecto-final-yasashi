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

// Declaraciones externas de los accesos de symtab.c si no están en tu symtab.h
extern int symtab_get_count(void);
extern SymTabEntry* symtab_get_entry(int index);

typedef enum {
    SEC_UNKNOWN,
    SEC_TEXT,
    SEC_DATA,
    SEC_BSS
} ActiveSection;

typedef struct {
    Instruction   instr;         /* Instrucción tal como la entrega parser.c */
    EncodedInstruction enc;     /* Bytes generados por el encoder           */
    int           needs_reloc;   /* 1 si tiene un placeholder de 4 bytes     */
    char          reloc_sym[64]; /* Nombre del símbolo a resolver            */
    uint32_t      reloc_offset;  /* Offset del placeholder dentro de .text   */
    uint32_t      explicit_org;  /* Dirección ORG explícita, o 0xFFFFFFFF    */
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

/* Estructura para almacenar de manera legítima los datos de la sección .data */
typedef struct {
    char     name[64];
    uint32_t value;
    uint32_t offset;
} DataVariable;

/* Estructura para almacenar la reserva de espacio de la sección .bss */
typedef struct {
    char     name[64];
    uint32_t size;
    uint32_t offset;
} BssVariable;

static DataVariable data_symbols[128];
static int data_symbol_count = 0;

static BssVariable bss_symbols[128];
static int bss_symbol_count = 0;

static FILE   *open_source(const char *path);
static int     run_pass1(FILE *src);
static int     run_pass2(FILE *src, AsmEntry *entries, int *count);
static int     encode_all(AsmEntry *entries, int count);
static int     build_object(AsmEntry *entries, int count, ObjectFile **obj_out);
static int     run_linker(ObjectFile **objs, int obj_count, const char *out_path);
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
        op->type = OP_LABEL;
        snprintf(op->label, sizeof(op->label), "%s", tok->lexeme);
        
        op->needs_reloc = 1;
        op->imm = 0; 

        if (needs_reloc_out) *needs_reloc_out = 1;
        if (reloc_sym_out) {
            snprintf(reloc_sym_out, 64, "%s", tok->lexeme);
        }
        
        *tok = get_next_token(f);
        return 0;
    }

    if (tok->type == TOKEN_LBRACKET) {
        *tok = get_next_token(f);

        /* PROCESAMIENTO COMPLEJO: [var1] o [resultado] dentro de brackets */
        if (tok->type == TOKEN_IDENTIFIER) {
            if (str_to_reg(tok->lexeme) == REG_NONE) {
                op->type = OP_MEM_DIRECT;
                op->disp = 0; 
                if (needs_reloc_out) *needs_reloc_out = 1;
                if (reloc_sym_out) {
                    snprintf(reloc_sym_out, 64, "%s", tok->lexeme);
                }
                *tok = get_next_token(f);
                if (tok->type != TOKEN_RBRACKET) return -1;
                *tok = get_next_token(f);
                return 0;
            }
        }

        if (tok->type == TOKEN_NUMBER) {
            op->type = OP_MEM_DIRECT;
            op->disp = (int32_t)strtol(tok->lexeme, NULL, 0);
            *tok = get_next_token(f);
            if (tok->type != TOKEN_RBRACKET) return -1;
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
                    if (tok->type != TOKEN_RBRACKET) return -1;
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
                        if       (sc == 2) scale = SCALE_2;
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
                        if (tok->type != TOKEN_RBRACKET) return -1;
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
                    if (tok->type != TOKEN_RBRACKET) return -1;
                    *tok = get_next_token(f);
                    return 0;
                }
            }
        }
        return -1;
    }

    return -1;
}

int main(int argc, char *argv[])
{
    printf("==============================================\n");
    printf("  Ensamblador/Linker IA-32  –  v1.0 (Multi-Sección)\n");
    printf("==============================================\n\n");

    if (argc < 3) {
        fprintf(stderr, "Uso: %s <archivo1.asm> [archivo2.asm ...] <salida.bin>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int num_src_files = argc - 2;
    const char *out_path = argv[argc - 1];

    ObjectFile **objs = malloc(num_src_files * sizeof(ObjectFile*));
    if (!objs) {
        fatal("Error crítico: Falló la asignación de memoria para objetos.");
    }

    // PIPELINE DE ENSAMBLADO EN BUCLE
    for (int i = 0; i < num_src_files; i++) {
        const char *src_path = argv[i + 1];
        printf("--------------------------------------------------\n");
        printf(" PROCESANDO MÓDULO [%d/%d]: '%s'\n", i + 1, num_src_files, src_path);
        printf("--------------------------------------------------\n");

        // REINICIO DE ESTADO GLOBAL PARA CADA ARCHIVO FUENTE INDEPENDIENTE
        data_symbol_count = 0;
        bss_symbol_count = 0;
        memset(data_symbols, 0, sizeof(data_symbols));
        memset(bss_symbols, 0, sizeof(bss_symbols));
        
        symtab_clear(); 

        FILE *src = open_source(src_path);

        printf(">>> PASS 1: Análisis léxico/sintáctico\n");
        if (run_pass1(src) != 0) {
            fclose(src);
            for (int j = 0; j < i; j++) obj_free(objs[j]);
            free(objs);
            fatal("Pass 1 falló. Abortando.");
        }

        rewind(src);

        printf("\n>>> PASS 2: Asignación de variables e instrucciones ejecutable\n");
        AsmEntry entries[MAX_INSTRUCTIONS];
        memset(entries, 0, sizeof(entries));
        int count = 0;

        if (run_pass2(src, entries, &count) != 0) {
            fclose(src);
            for (int j = 0; j < i; j++) obj_free(objs[j]);
            free(objs);
            fatal("Pass 2 falló. Abortando.");
        }
        fclose(src);
        printf("Pass 2 Completa: %d instrucciones procesadas.\n", count);

        printf("\n>>> ENCODER: Codificación IA-32\n");
        if (encode_all(entries, count) != 0) {
            for (int j = 0; j < i; j++) obj_free(objs[j]);
            free(objs);
            fatal("Encoder falló. Abortando.");
        }

        printf("\n>>> OBJECT: Construcción legítima del archivo objeto\n");
        ObjectFile *obj = NULL;
        if (build_object(entries, count, &obj) != 0) {
            for (int j = 0; j < i; j++) obj_free(objs[j]);
            free(objs);
            fatal("Construcción del objeto falló. Abortando.");
        }
        obj_print(obj);

        objs[i] = obj;
    }

    printf("\n>>> LINKER: Enlazado físico de %d módulo(s) y escritura de '%s'\n", num_src_files, out_path);
    if (run_linker(objs, num_src_files, out_path) != 0) {
        for (int i = 0; i < num_src_files; i++) {
            obj_free(objs[i]);
        }
        free(objs);
        fatal("Linker falló. Abortando.");
    }

    for (int i = 0; i < num_src_files; i++) {
        obj_free(objs[i]);
    }
    free(objs);

    printf("\n==============================================\n");
    printf("  Binario real generado con éxito: %s\n", out_path);
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
    ActiveSection current_sec = SEC_UNKNOWN;
    
    uint32_t data_offset_counter = 0;
    uint32_t bss_offset_counter = 0;
    uint32_t current_org = 0xFFFFFFFF; 

    while (tok.type != TOKEN_EOF) {
        if (tok.type == TOKEN_NEWLINE) {
            tok = get_next_token(src);
            continue;
        }

        if (STR_EQ_CI(tok.lexeme, "SECTION")) {
            tok = get_next_token(src); 
            if (STR_EQ_CI(tok.lexeme, ".text")) current_sec = SEC_TEXT;
            else if (STR_EQ_CI(tok.lexeme, ".data")) current_sec = SEC_DATA;
            else if (STR_EQ_CI(tok.lexeme, ".bss")) current_sec = SEC_BSS;
            
            while (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF) tok = get_next_token(src);
            continue;
        }

        if (STR_EQ_CI(tok.lexeme, "GLOBAL")) {
            while (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF) tok = get_next_token(src);
            continue;
        }

        char label_or_var[64];
        snprintf(label_or_var, sizeof(label_or_var), "%s", tok.lexeme);
        tok = get_next_token(src);

        if (tok.type == TOKEN_COLON) {
            tok = get_next_token(src);
            continue;
        }

        if (STR_EQ_CI(label_or_var, "ORG")) {
            uint32_t new_org = (uint32_t)strtol(tok.lexeme, NULL, 0);
            tok = get_next_token(src);
            
            if (current_sec == SEC_DATA) {
                data_offset_counter = new_org;
            } else if (current_sec == SEC_BSS) {
                bss_offset_counter = new_org;
            } else {
                current_org = new_org; 
            }
            
            while (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF) tok = get_next_token(src);
            continue;
        }

        if (current_sec == SEC_DATA) {
            if (STR_EQ_CI(tok.lexeme, "DD")) {
                if (data_symbol_count >= 128) return -1; 
                tok = get_next_token(src); 
                uint32_t initial_val = (uint32_t)strtol(tok.lexeme, NULL, 0);
                
                snprintf(data_symbols[data_symbol_count].name, sizeof(data_symbols[data_symbol_count].name), "%s", label_or_var);
                data_symbols[data_symbol_count].value = initial_val;
                data_symbols[data_symbol_count].offset = data_offset_counter;
                data_symbol_count++;
                
                data_offset_counter += 4; 
            }
            while (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF) tok = get_next_token(src);
            continue;
        }

        if (current_sec == SEC_BSS) {
            if (STR_EQ_CI(tok.lexeme, "RESD")) {
                if (bss_symbol_count >= 128) return -1; 
                tok = get_next_token(src); 
                uint32_t count_elements = (uint32_t)strtol(tok.lexeme, NULL, 0);
                
                snprintf(bss_symbols[bss_symbol_count].name, sizeof(bss_symbols[bss_symbol_count].name), "%s", label_or_var);
                bss_symbols[bss_symbol_count].size = count_elements * 4; 
                bss_symbols[bss_symbol_count].offset = bss_offset_counter;
                bss_symbol_count++;
                
                bss_offset_counter += (count_elements * 4);
            }
            while (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF) tok = get_next_token(src);
            continue;
        }

        Mnemonic mn = str_to_mn(label_or_var);
        if (mn == MN_UNKNOWN) {
            while (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF) tok = get_next_token(src);
            continue;
        }

        if (*count >= MAX_INSTRUCTIONS) return -1;

        AsmEntry *e = &entries[*count];
        memset(e, 0, sizeof(AsmEntry));
        e->instr.mnemonic = mn;
        e->explicit_org = current_org;
        current_org = 0xFFFFFFFF; 

        if (mn == MN_RET || mn == MN_NOP) {
            (*count)++;
            while (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF) tok = get_next_token(src);
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
    }
    return 0;
}

static int encode_all(AsmEntry *entries, int count)
{
    int ok = 1;
    uint32_t real_lc = 0;

    for (int i = 0; i < count; i++) {
        if (entries[i].explicit_org != 0xFFFFFFFF && entries[i].explicit_org > real_lc) {
            real_lc = entries[i].explicit_org;
        }
        
        entries[i].instr.address = real_lc; 
        entries[i].enc = encoder_encode(&entries[i].instr);

        if (entries[i].enc.error) {
            fprintf(stderr, "Encoder error en instrucción %d (mn=%d): %s\n",
                    i, entries[i].instr.mnemonic, entries[i].enc.error_msg);
            ok = 0;
        } else {
            printf("  [0x%04X] ", entries[i].instr.address);
            encoder_dump(&entries[i].enc);
            real_lc += entries[i].enc.length; 
        }
    }
    return ok ? 0 : -1;
}

static int build_object(AsmEntry *entries, int count, ObjectFile **obj_out)
{
    ObjectFile *obj = obj_create();
    if (!obj) return -1;

    /* REGISTRO LEGAL DE SECCIONES INDEPENDIENTES */
    int text_idx = obj_add_section(obj, ".text");
    int data_idx = obj_add_section(obj, ".data");
    int bss_idx  = obj_add_section(obj, ".bss");

    /* ============================================================
     * CORRECCIÓN: Exportar TODOS los símbolos de código (.text) 
     * definidos en la tabla global para que el Linker los vea.
     * ============================================================ */
    int total_syms = symtab_get_count();
    for (int i = 0; i < total_syms; i++) {
        SymTabEntry *st = symtab_get_entry(i);
        if (st && st->defined && !st->is_extern) {
            obj_add_symbol(obj, st->name, st->address, text_idx, SYM_GLOBAL);
        }
    }

    /* INYECTAR LAS VARIABLES DE .data AL ARCHIVO OBJETO CON SUS BYTES REALES */
    for (int i = 0; i < data_symbol_count; i++) {
        obj_add_symbol(obj, data_symbols[i].name, data_symbols[i].offset, data_idx, SYM_GLOBAL);
        uint32_t val = data_symbols[i].value;
        uint8_t bytes[4] = { val & 0xFF, (val >> 8) & 0xFF, (val >> 16) & 0xFF, (val >> 24) & 0xFF };
        obj_write_bytes(obj, data_idx, bytes, 4);
    }

    /* INYECTAR LAS VARIABLES DE .bss AL ARCHIVO OBJETO */
    for (int i = 0; i < bss_symbol_count; i++) {
        obj_add_symbol(obj, bss_symbols[i].name, bss_symbols[i].offset, bss_idx, SYM_GLOBAL);
    }

    /* Recorrer instrucciones para añadir bytes e identificar dependencias de etiquetas */
    uint32_t text_off = 0;
    for (int i = 0; i < count; i++) {
        AsmEntry *e = &entries[i];

        if (e->explicit_org != 0xFFFFFFFF && e->explicit_org > text_off) {
            uint32_t gap = e->explicit_org - text_off;
            uint8_t *pad = calloc(gap, 1);
            if (pad) {
                obj_write_bytes(obj, text_idx, pad, gap);
                free(pad);
            }
            text_off = e->explicit_org;
        }

        if (e->enc.length > 0) {
            if (obj_write_bytes(obj, text_idx, e->enc.bytes, (uint32_t)e->enc.length) != 0) {
                obj_free(obj);
                return -1;
            }
        }

        if (e->needs_reloc && e->reloc_sym[0] != '\0') {
            Mnemonic mn = e->instr.mnemonic;
            uint32_t patch_offset = text_off + (e->enc.length - 4); 

            int sym_idx = obj_find_symbol(obj, e->reloc_sym);
            if (sym_idx < 0) {
                SymTabEntry *global_sym = get_symbol(e->reloc_sym);
                
                if (global_sym && global_sym->defined) {
                    sym_idx = obj_add_symbol(obj, e->reloc_sym, global_sym->address, text_idx, SYM_GLOBAL);
                } else {
                    sym_idx = obj_add_symbol(obj, e->reloc_sym, 0, -1, SYM_EXTERN);
                }
            }

            if (sym_idx >= 0) {
                if (mn == MN_JE || mn == MN_JNE || mn == MN_JG || mn == MN_JL || mn == MN_JMP || mn == MN_CALL || mn == MN_JGE || mn == MN_JLE) {
                    obj_add_reloc(obj, patch_offset, sym_idx, RELOC_REL32, -4);
                } else {
                    obj_add_reloc(obj, patch_offset, sym_idx, RELOC_ABS32, 0);
                }
            }
        }
        text_off += (uint32_t)e->enc.length;
    }

    *obj_out = obj;
    return 0;
}

static int run_linker(ObjectFile **objs, int obj_count, const char *out_path)
{
    LinkerCtx *ctx = linker_create();
    if (!ctx) return -1;

    for (int i = 0; i < obj_count; i++) {
        if (linker_add_object(ctx, objs[i]) != 0) {
            linker_free(ctx);
            return -1;
        }
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