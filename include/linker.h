#ifndef LINKER_H
#define LINKER_H

#include "object.h"

/* ============================================================
   CONSTANTES DEL LINKER
   ============================================================ */
#define MAX_OBJ_FILES     16
#define MAX_GLOBAL_SYMS   256
#define LOAD_ADDRESS      0x08048000   /* direccion base del binario final */

/* ============================================================
   SIMBOLO GLOBAL (tabla unificada del linker)
   Cuando el linker une varios .o necesita una tabla propia
   que sepa en que archivo y a que offset final vive cada simbolo
   ============================================================ */
typedef struct {
    char     name[64];
    uint32_t final_address;   /* direccion absoluta en el binario final */
    int      obj_idx;         /* en cual archivo objeto fue definido    */
    int      sec_idx;         /* en cual seccion de ese archivo         */
    uint32_t sec_offset;      /* offset dentro de esa seccion           */
    int      defined;         /* 1 si ya fue definido, 0 si es EXTERN   */
} GlobalSymbol;

/* ============================================================
   CONTEXTO DEL LINKER
   Todo el estado que el linker necesita para trabajar
   ============================================================ */
typedef struct {
    /* Archivos objeto de entrada */
    ObjectFile *objs[MAX_OBJ_FILES];
    int         num_objs;

    /* Tabla global de simbolos (unificada de todos los .o) */
    GlobalSymbol gsyms[MAX_GLOBAL_SYMS];
    int          num_gsyms;

    /* Offsets donde queda cada seccion de cada .o en el binario final */
    /* section_offsets[i][j] = offset del .o i, seccion j             */
    uint32_t section_offsets[MAX_OBJ_FILES][MAX_SECTIONS];

    /* Buffer del binario final */
    uint8_t  output[65536];
    uint32_t output_size;
} LinkerCtx;

/* ============================================================
   PROTOTIPOS
   ============================================================ */

/* Inicializar el contexto del linker */
LinkerCtx* linker_create(void);

/* Liberar memoria */
void linker_free(LinkerCtx *ctx);

/* Agregar un archivo objeto al linker */
int linker_add_object(LinkerCtx *ctx, ObjectFile *obj);

/* Paso 1: fusionar secciones y calcular offsets finales */
int linker_merge_sections(LinkerCtx *ctx);

/* Paso 2: construir tabla global de simbolos */
int linker_build_symbol_table(LinkerCtx *ctx);

/* Paso 3: aplicar relocaciones (parchear placeholders) */
int linker_apply_relocations(LinkerCtx *ctx);

/* Paso 4: escribir el binario final */
int linker_write_binary(LinkerCtx *ctx, const char *filename);

/* Funcion principal que ejecuta los 4 pasos */
int linker_link(LinkerCtx *ctx, const char *output_file);

/* Imprimir estado del linker (debug) */
void linker_print(LinkerCtx *ctx);

#endif /* LINKER_H */
