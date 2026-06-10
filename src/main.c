/* ============================================================
 * src/main.c  –  Pipeline principal del Ensamblador/Linker IA-32
 *
 * Flujo de ejecución:
 *   1. Argparse: archivo .asm de entrada + nombre del ejecutable
 *   2. Pass 1 (Parser): construye la tabla de símbolos
 *   3. Pass 2 (Parser): genera Instruction[] con direcciones asignadas
 *   4. Encoder: transforma cada Instruction en bytes de máquina
 *   5. Object: empaqueta los bytes en un ObjectFile en memoria
 *   6. Linker: resuelve relocalizaciones y escribe el binario final
 *
 * Compilar (desde la raíz del proyecto):
 *   gcc -Wall -Wextra -Iinclude -o assembler.exe \
 *       src/main.c src/lexer.c src/parser.c src/symtab.c \
 *       src/encoder.c src/object.c src/linker.c
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

/* ----------------------------------------------------------
 * Límite superior de instrucciones que el ensamblador puede
 * procesar en un único archivo fuente.
 * ---------------------------------------------------------- */
#define MAX_INSTRUCTIONS 1024

/* ----------------------------------------------------------
 * Estructura interna que extiende Instruction con la
 * información de relocalización que necesita el linker.
 * ---------------------------------------------------------- */
typedef struct {
    Instruction  instr;         /* Instrucción tal como la entrega parser.c */
    EncodedInstruction enc;     /* Bytes generados por el encoder           */
    int          needs_reloc;   /* 1 si tiene un placeholder de 4 bytes     */
    char         reloc_sym[64]; /* Nombre del símbolo a resolver            */
    uint32_t     reloc_offset;  /* Offset del placeholder dentro de .text   */
} AsmEntry;

/* ----------------------------------------------------------
 * Prototipos de funciones internas del pipeline.
 * ---------------------------------------------------------- */
static FILE   *open_source(const char *path);
static int     run_pass1(FILE *src);
static int     run_pass2(FILE *src, AsmEntry *entries, int *count);
static int     encode_all(AsmEntry *entries, int count);
static int     build_object(AsmEntry *entries, int count,
                             ObjectFile **obj_out);
static int     run_linker(ObjectFile *obj, const char *out_path);
static void    fatal(const char *msg);

/* ==============================================================
 * main
 * ==============================================================
 * Argumentos:
 *   argv[1]  – archivo fuente  (ej. test.asm)
 *   argv[2]  – ejecutable final (ej. output.bin  o  output.hex)
 * ============================================================== */
int main(int argc, char *argv[])
{
    printf("==============================================\n");
    printf("  Ensamblador/Linker IA-32  –  v1.0\n");
    printf("==============================================\n\n");

    /* ── 0. Validar argumentos ─────────────────────────────── */
    if (argc < 3) {
        fprintf(stderr,
            "Uso: %s <archivo.asm> <salida>\n"
            "Ejemplo: %s test.asm output.bin\n",
            argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    const char *src_path = argv[1];
    const char *out_path = argv[2];

    /* ── 1. Abrir el archivo fuente ────────────────────────── */
    FILE *src = open_source(src_path);

    /* ── 2. PASS 1: recoger etiquetas y poblar symtab ──────── */
    printf(">>> PASS 1: Análisis léxico/sintáctico (symtab)\n");
    if (run_pass1(src) != 0) {
        fclose(src);
        fatal("Pass 1 falló. Abortando.");
    }
    print_symtab();

    /* Rebobinar para Pass 2 */
    rewind(src);

    /* ── 3. PASS 2: generar array de Instruction con LC ───── */
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

    /* ── 4. ENCODER: transformar instrucciones a bytes ──────── */
    printf("\n>>> ENCODER: Codificación IA-32\n");
    if (encode_all(entries, count) != 0) {
        fatal("Encoder falló. Abortando.");
    }

    /* ── 5. OBJECT: construir el ObjectFile en memoria ──────── */
    printf("\n>>> OBJECT: Construcción del archivo objeto\n");
    ObjectFile *obj = NULL;
    if (build_object(entries, count, &obj) != 0) {
        fatal("Construcción del objeto falló. Abortando.");
    }
    obj_print(obj);

    /* ── 6. LINKER: resolver relocalizaciones y escribir ───── */
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


/* ==============================================================
 * open_source
 *   Abre el archivo fuente .asm en modo lectura.
 *   Aborta el proceso si no existe o no puede abrirse.
 * ============================================================== */
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


/* ==============================================================
 * run_pass1
 *   Invoca parse_program() de parser.c para hacer la primera
 *   pasada: registrar etiquetas en la tabla de símbolos global
 *   (symtab.c) y calcular el Location Counter de cada una.
 *
 *   Retorna 0 si OK, -1 si el parser llamó a exit() internamente
 *   (situación que no podemos interceptar, pero la firma permite
 *   extensibilidad futura).
 * ============================================================== */
static int run_pass1(FILE *src)
{
    /*
     * parse_program() inicializa la symtab, recorre el fuente
     * token a token, registra etiquetas con add_symbol() y avanza
     * el LC por cada instrucción reconocida.
     *
     * En caso de error sintáctico llama a exit(1) internamente;
     * no retorna un código de error, por lo que si llegamos a la
     * línea siguiente es que Pass 1 fue exitosa.
     */
    parse_program(src);
    return 0;
}


/* ==============================================================
 * run_pass2
 *   Realiza la segunda pasada sobre el archivo fuente.
 *
 *   Vuelve a recorrer el fuente con el lexer y construye el array
 *   de AsmEntry[]. Para cada instrucción:
 *     – Identifica el mnemónico y los operandos mediante tokens.
 *     – Resuelve operandos que son etiquetas consultando symtab.
 *     – Asigna instr.address con el LC acumulado.
 *     – Marca needs_reloc si el símbolo destino aún no está
 *       definido (referencia hacia adelante o EXTERN).
 *
 *   NOTA SOBRE EL DISEÑO:
 *   parser.c::pass2_program() escribe directamente a output.hex
 *   y no devuelve el array de Instruction. Para integrar con el
 *   encoder y el linker sin reescribir parser.c, esta función
 *   reimplementa la Pass 2 a nivel de tokens, reutilizando
 *   get_next_token() y get_symbol() que ya están expuestos.
 *
 *   Retorna 0 si OK, -1 si hay error.
 * ============================================================== */
static int run_pass2(FILE *src, AsmEntry *entries, int *count)
{
    /*
     * Mapeo mnemónico (string) → Mnemonic (enum de ia32_types.h).
     * Debe estar sincronizado con el enum Mnemonic.
     */
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

    /* Mapeo nombre de registro → Reg32 */
    static const struct { const char *str; Reg32 r; } reg_table[] = {
        {"EAX", REG_EAX}, {"ECX", REG_ECX}, {"EDX", REG_EDX},
        {"EBX", REG_EBX}, {"ESP", REG_ESP}, {"EBP", REG_EBP},
        {"ESI", REG_ESI}, {"EDI", REG_EDI},
        {NULL,  REG_NONE}
    };

    /* ── helpers lambda-like vía macros locales ── */
#define STR_EQ_CI(a,b)  (strcasecmp((a),(b)) == 0)

    /* Convierte un lexema de registro en Reg32 */
    Reg32 str_to_reg(const char *s) {
        for (int i = 0; reg_table[i].str; i++)
            if (STR_EQ_CI(s, reg_table[i].str)) return reg_table[i].r;
        return REG_NONE;
    }

    /* Convierte un lexema de mnemónico en Mnemonic */
    Mnemonic str_to_mn(const char *s) {
        for (int i = 0; mn_table[i].str; i++)
            if (STR_EQ_CI(s, mn_table[i].str)) return mn_table[i].mn;
        return MN_UNKNOWN;
    }

    /*
     * Parsea un único operando a partir del token actual.
     * Avanza los tokens lo necesario y rellena *op.
     * Retorna 0 si OK, -1 si error.
     *
     * Se implementa como función anidada (extensión GCC que
     * funciona con -std=gnu99 o superior).
     */
    int parse_operand(Token *tok, FILE *f, Operand *op,
                      int *needs_reloc_out, char *reloc_sym_out)
    {
        memset(op, 0, sizeof(Operand));
        op->reg       = REG_NONE;
        op->index_reg = REG_NONE;
        op->scale     = SCALE_1;

        /* ── Registro directo ─────────────────────────── */
        if (tok->type == TOKEN_REGISTER) {
            op->type = OP_REG;
            op->reg  = str_to_reg(tok->lexeme);
            *tok = get_next_token(f);
            return 0;
        }

        /* ── Valor inmediato ──────────────────────────── */
        if (tok->type == TOKEN_NUMBER) {
            op->type = OP_IMM;
            op->imm  = (int32_t)strtol(tok->lexeme, NULL, 0);
            *tok = get_next_token(f);
            return 0;
        }

        /* ── Referencia a etiqueta (saltos/CALL) ──────── */
        if (tok->type == TOKEN_IDENTIFIER) {
            op->type = OP_LABEL;
            strncpy(op->label, tok->lexeme, sizeof(op->label) - 1);

            Symbol *sym = get_symbol(tok->lexeme);
            if (sym && sym->defined) {
                /* Símbolo ya resuelto: convertir a OP_IMM con la dirección */
                op->type = OP_IMM;
                op->imm  = sym->address;
            } else {
                /* Símbolo no resuelto: marcar para relocalización */
                op->needs_reloc = 1;
                if (needs_reloc_out)  *needs_reloc_out = 1;
                if (reloc_sym_out)
                    strncpy(reloc_sym_out, tok->lexeme,
                            63);
            }
            *tok = get_next_token(f);
            return 0;
        }

        /* ── Operando de memoria  [...]  ──────────────── */
        if (tok->type == TOKEN_LBRACKET) {
            *tok = get_next_token(f);   /* consumir '[' */

            if (tok->type == TOKEN_NUMBER) {
                /* [disp32]  →  OP_MEM_DIRECT */
                op->type = OP_MEM_DIRECT;
                op->disp = (int32_t)strtol(tok->lexeme, NULL, 0);
                *tok = get_next_token(f);   /* consume número   */
                if (tok->type != TOKEN_RBRACKET) {
                    fprintf(stderr, "Error: se esperaba ']' tras dirección directa.\n");
                    return -1;
                }
                *tok = get_next_token(f);   /* consume ']'      */
                return 0;
            }

            if (tok->type == TOKEN_REGISTER) {
                Reg32 base = str_to_reg(tok->lexeme);
                *tok = get_next_token(f);   /* consumir registro */

                if (tok->type == TOKEN_RBRACKET) {
                    /* [base]  →  OP_MEM_BASE */
                    op->type = OP_MEM_BASE;
                    op->reg  = base;
                    *tok = get_next_token(f);
                    return 0;
                }

                if (tok->type == TOKEN_PLUS) {
                    *tok = get_next_token(f);   /* consumir '+' */

                    /* Puede ser [base+disp], [base+index] o [base+index*scale+disp] */
                    if (tok->type == TOKEN_NUMBER) {
                        /* [base + disp]  →  OP_MEM_BASE_DISP */
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
                            /* [base + index * scale ...] → OP_MEM_SIB */
                            *tok = get_next_token(f);   /* consumir '*' */
                            int sc = (int)strtol(tok->lexeme, NULL, 0);
                            SibScale scale = SCALE_1;
                            if      (sc == 2) scale = SCALE_2;
                            else if (sc == 4) scale = SCALE_4;
                            else if (sc == 8) scale = SCALE_8;
                            *tok = get_next_token(f);   /* consumir escala */

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

                        /* [base + index]  →  OP_MEM_BASE_IDX */
                        op->type      = OP_MEM_BASE_IDX;
                        op->reg       = base;
                        op->index_reg = idx;
                        if (tok->type == TOKEN_PLUS) {
                            *tok = get_next_token(f);
                            op->disp = (int32_t)strtol(tok->lexeme, NULL, 0);
                            op->type = OP_MEM_SIB;   /* tiene desplazamiento */
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
    /* ── fin parse_operand ── */

    /* ── Bucle principal de Pass 2 ──────────────────────────── */
    uint32_t lc = 0;          /* Location Counter */
    *count = 0;

    Token tok = get_next_token(src);

    while (tok.type != TOKEN_EOF) {

        /* Ignorar saltos de línea vacíos */
        if (tok.type == TOKEN_NEWLINE) {
            tok = get_next_token(src);
            continue;
        }

        /* Debe comenzar con un IDENTIFIER (etiqueta o mnemónico) */
        if (tok.type != TOKEN_IDENTIFIER) {
            fprintf(stderr, "Error Pass 2: token inesperado '%s'.\n",
                    tok.lexeme);
            return -1;
        }

        char word[64];
        strncpy(word, tok.lexeme, sizeof(word) - 1);
        tok = get_next_token(src);

        /* ── Etiqueta: IDENTIFIER seguido de ':' ──────── */
        if (tok.type == TOKEN_COLON) {
            tok = get_next_token(src);   /* saltar ':' */
            continue;
        }

        /* ── Es un mnemónico ──────────────────────────── */
        Mnemonic mn = str_to_mn(word);
        if (mn == MN_UNKNOWN) {
            fprintf(stderr, "Error Pass 2: mnemónico desconocido '%s'.\n",
                    word);
            return -1;
        }

        if (*count >= MAX_INSTRUCTIONS) {
            fprintf(stderr, "Error Pass 2: demasiadas instrucciones (max %d).\n",
                    MAX_INSTRUCTIONS);
            return -1;
        }

        AsmEntry *e = &entries[*count];
        memset(e, 0, sizeof(AsmEntry));
        e->instr.mnemonic = mn;
        e->instr.address  = lc;
        e->instr.dst.type = OP_NONE;
        e->instr.src.type = OP_NONE;
        e->instr.dst.reg  = REG_NONE;
        e->instr.src.reg  = REG_NONE;

        /* ── Instrucciones sin operandos (RET, NOP) ────── */
        if (mn == MN_RET || mn == MN_NOP) {
            lc += 1;
            (*count)++;
            /* consumir hasta fin de línea */
            while (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF)
                tok = get_next_token(src);
            if (tok.type == TOKEN_NEWLINE) tok = get_next_token(src);
            continue;
        }

        /* ── INT necesita un inmediato ─────────────────── */
        if (mn == MN_INT) {
            if (tok.type == TOKEN_NUMBER) {
                e->instr.dst.type = OP_IMM;
                e->instr.dst.imm  = (int32_t)strtol(tok.lexeme, NULL, 0);
                tok = get_next_token(src);
            }
            lc += 2;
            (*count)++;
            while (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF)
                tok = get_next_token(src);
            if (tok.type == TOKEN_NEWLINE) tok = get_next_token(src);
            continue;
        }

        /* ── Parsear operando destino ───────────────────── */
        if (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF) {
            if (parse_operand(&tok, src,
                              &e->instr.dst,
                              &e->needs_reloc,
                              e->reloc_sym) != 0)
                return -1;
        }

        /* ── Coma y operando fuente (si aplica) ─────────── */
        if (tok.type == TOKEN_COMMA) {
            tok = get_next_token(src);   /* consumir ',' */
            if (parse_operand(&tok, src,
                              &e->instr.src,
                              &e->needs_reloc,
                              e->reloc_sym) != 0)
                return -1;
        }

        /* ── Estimar tamaño de instrucción para el LC ───── */
        /*
         * El tamaño exacto lo conocemos sólo después de codificar.
         * Usamos una estimación conservadora:
         *   – MOV r32, imm32   → 5 bytes
         *   – JMP/CALL rel32   → 5 bytes
         *   – Jcc rel32        → 6 bytes
         *   – instrucción genérica → 2 bytes (mínimo)
         * Después de encode_all() se corrigen las referencias
         * relativas via el linker.
         */
        uint32_t est_size = 2;
        if (mn == MN_MOV &&
            e->instr.dst.type == OP_REG &&
            e->instr.src.type == OP_IMM) {
            est_size = 5;
        } else if (mn == MN_JMP || mn == MN_CALL) {
            est_size = 5;
        } else if (mn == MN_JE  || mn == MN_JNE || mn == MN_JG  ||
                   mn == MN_JL  || mn == MN_JGE || mn == MN_JLE) {
            est_size = 6;
        }

        lc += est_size;
        (*count)++;

        /* Consumir hasta fin de línea */
        while (tok.type != TOKEN_NEWLINE && tok.type != TOKEN_EOF)
            tok = get_next_token(src);
        if (tok.type == TOKEN_NEWLINE)
            tok = get_next_token(src);
    }

#undef STR_EQ_CI
    return 0;
}


/* ==============================================================
 * encode_all
 *   Llama a encoder_encode() para cada instrucción del array.
 *   Si alguna codificación falla, imprime el error y retorna -1.
 * ============================================================== */
static int encode_all(AsmEntry *entries, int count)
{
    int ok = 1;
    for (int i = 0; i < count; i++) {
        entries[i].enc = encoder_encode(&entries[i].instr);

        if (entries[i].enc.error) {
            fprintf(stderr,
                "Encoder[%d] error en instrucción %d (mn=%d): %s\n",
                i, i, entries[i].instr.mnemonic,
                entries[i].enc.error_msg);
            ok = 0;
        } else {
            printf("  [0x%04X] ", entries[i].instr.address);
            encoder_dump(&entries[i].enc);
        }
    }
    return ok ? 0 : -1;
}


/* ==============================================================
 * build_object
 *   Crea un ObjectFile en memoria con una única sección .text
 *   y la puebla con los bytes generados por el encoder.
 *   Registra las etiquetas globales y las entradas de
 *   relocalización (CALL/JMP con destino no resuelto).
 *
 *   *obj_out apunta al ObjectFile recién creado (debe liberarse
 *   con obj_free() cuando ya no se necesite).
 * ============================================================== */
static int build_object(AsmEntry *entries, int count, ObjectFile **obj_out)
{
    ObjectFile *obj = obj_create();
    if (!obj) {
        fprintf(stderr, "Error: obj_create() retornó NULL.\n");
        return -1;
    }

    /* Crear sección .text */
    int text_idx = obj_add_section(obj, ".text");
    if (text_idx < 0) {
        obj_free(obj);
        return -1;
    }

    /* Registrar el símbolo de entrada como GLOBAL */
    obj_add_symbol(obj, "_start", 0, text_idx, SYM_GLOBAL);

    /* Offset acumulado en .text para calcular posición de relocalizaciones */
    uint32_t text_off = 0;

    for (int i = 0; i < count; i++) {
        AsmEntry *e = &entries[i];

        /* Volcar los bytes codificados a .text */
        if (e->enc.length > 0) {
            if (obj_write_bytes(obj, text_idx,
                                e->enc.bytes,
                                (uint32_t)e->enc.length) != 0) {
                fprintf(stderr,
                    "Error: obj_write_bytes() falló en instrucción %d.\n", i);
                obj_free(obj);
                return -1;
            }
        }

        /* Si la instrucción necesita relocalización REL32, registrarla */
        if (e->needs_reloc && e->reloc_sym[0] != '\0') {
            /*
             * El placeholder de 4 bytes está justo después del opcode.
             * Para JMP (1 byte opcode): patch_offset = text_off + 1
             * Para Jcc (2 bytes opcode): patch_offset = text_off + 2
             * Para CALL (1 byte opcode): patch_offset = text_off + 1
             */
            uint32_t patch_offset;
            Mnemonic mn = e->instr.mnemonic;
            if (mn == MN_JE || mn == MN_JNE || mn == MN_JG ||
                mn == MN_JL || mn == MN_JGE || mn == MN_JLE) {
                patch_offset = text_off + 2;
            } else {
                patch_offset = text_off + 1;
            }

            /* Asegurarse de que el símbolo externo esté en la tabla */
            int sym_idx = obj_find_symbol(obj, e->reloc_sym);
            if (sym_idx < 0) {
                sym_idx = obj_add_symbol(obj, e->reloc_sym,
                                         0, -1, SYM_EXTERN);
            }

            if (sym_idx >= 0) {
                /* addend = -4 porque la CPU suma 4 al EIP antes de saltar */
                obj_add_reloc(obj, patch_offset, sym_idx,
                              RELOC_REL32, -4);
            }
        }

        text_off += (uint32_t)e->enc.length;
    }

    *obj_out = obj;
    return 0;
}


/* ==============================================================
 * run_linker
 *   Crea el contexto del linker, agrega el único archivo objeto
 *   generado y ejecuta los 4 pasos del linker:
 *     1. Fusionar secciones
 *     2. Construir tabla global de símbolos
 *     3. Aplicar relocalizaciones
 *     4. Escribir binario final
 * ============================================================== */
static int run_linker(ObjectFile *obj, const char *out_path)
{
    LinkerCtx *ctx = linker_create();
    if (!ctx) {
        fprintf(stderr, "Error: linker_create() retornó NULL.\n");
        return -1;
    }

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


/* ==============================================================
 * fatal
 *   Imprime un mensaje de error y termina el proceso.
 * ============================================================== */
static void fatal(const char *msg)
{
    fprintf(stderr, "\n[FATAL] %s\n", msg);
    exit(EXIT_FAILURE);
}