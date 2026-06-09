#ifndef OBJECT_H
#define OBJECT_H

#include <stdint.h>

/* ============================================================
   CONSTANTES
   ============================================================ */
#define MAX_SECTIONS     8
#define MAX_SYMBOLS      64
#define MAX_RELOCATIONS  64
#define MAX_SECTION_DATA 4096
#define OBJ_MAGIC        0x4F424A21   /* "OBJ!" en hex */

/* ============================================================
   TIPOS DE SIMBOLO
   LOCAL  = solo visible dentro de este archivo objeto
   GLOBAL = visible para otros archivos objeto (directiva GLOBAL)
   EXTERN = definido en otro archivo objeto (directiva EXTERN)
   ============================================================ */
typedef enum {
    SYM_LOCAL  = 0,
    SYM_GLOBAL = 1,
    SYM_EXTERN = 2
} SymbolType;

/* ============================================================
   TIPOS DE RELOCACION
   REL32 = dirección relativa de 32 bits (usada en CALL, JMP)
   ABS32 = dirección absoluta de 32 bits (usada en MOV con memoria)
   ============================================================ */
typedef enum {
    RELOC_REL32 = 0,
    RELOC_ABS32 = 1
} RelocType;

/* ============================================================
   SECCION
   Representa .text, .data o .bss
   ============================================================ */
typedef struct {
    char     name[16];                  /* ".text", ".data", ".bss" */
    uint8_t  data[MAX_SECTION_DATA];    /* bytes del contenido      */
    uint32_t size;                      /* cuantos bytes tiene      */
    uint32_t offset;                    /* offset en el archivo     */
} Section;

/* ============================================================
   SIMBOLO
   Una entrada en la tabla de simbolos
   ============================================================ */
typedef struct {
    char       name[64];    /* nombre del simbolo, ej: "main", "suma" */
    uint32_t   value;       /* offset dentro de su seccion            */
    int        section_idx; /* indice de la seccion donde vive (-1 si EXTERN) */
    SymbolType type;        /* LOCAL, GLOBAL o EXTERN                 */
    int        defined;     /* 1 si ya fue definido, 0 si no          */
} Symbol;

/* ============================================================
   RELOCACION
   Un "parche pendiente": direccion que el linker debe corregir
   ============================================================ */
typedef struct {
    uint32_t  offset;      /* donde esta el placeholder en .text      */
    int       sym_idx;     /* cual simbolo hay que resolver           */
    RelocType type;        /* REL32 o ABS32                           */
    int32_t   addend;      /* ajuste adicional (normalmente -4 o 0)   */
} Relocation;

/* ============================================================
   ENCABEZADO DEL ARCHIVO OBJETO
   Lo primero que va en el .o
   ============================================================ */
typedef struct {
    uint32_t magic;          /* siempre OBJ_MAGIC = 0x4F424A21  */
    uint32_t num_sections;   /* cuantas secciones hay            */
    uint32_t num_symbols;    /* cuantos simbolos hay             */
    uint32_t num_relocs;     /* cuantas relocaciones hay         */
} ObjHeader;

/* ============================================================
   ARCHIVO OBJETO COMPLETO (en memoria)
   ============================================================ */
typedef struct {
    ObjHeader  header;
    Section    sections[MAX_SECTIONS];
    Symbol     symbols[MAX_SYMBOLS];
    Relocation relocs[MAX_RELOCATIONS];
} ObjectFile;

/* ============================================================
   PROTOTIPOS
   ============================================================ */
ObjectFile* obj_create(void);
void        obj_free(ObjectFile *obj);

int  obj_add_section(ObjectFile *obj, const char *name);
int  obj_write_bytes(ObjectFile *obj, int sec_idx,
                     const uint8_t *bytes, uint32_t len);

int  obj_add_symbol(ObjectFile *obj, const char *name,
                    uint32_t value, int sec_idx, SymbolType type);
int  obj_find_symbol(ObjectFile *obj, const char *name);

int  obj_add_reloc(ObjectFile *obj, uint32_t offset,
                   int sym_idx, RelocType type, int32_t addend);

int  obj_write_file(ObjectFile *obj, const char *filename);
ObjectFile* obj_read_file(const char *filename);

void obj_print(ObjectFile *obj);

#endif /* OBJECT_H */
