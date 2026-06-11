#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/object.h"

/* ============================================================
   CREAR un archivo objeto vacio en memoria
   ============================================================ */
ObjectFile* obj_create(void) {
    ObjectFile *obj = calloc(1, sizeof(ObjectFile));
    if (!obj) {
        printf("Error: no se pudo reservar memoria para ObjectFile\n");
        return NULL;
    }
    obj->header.magic = OBJ_MAGIC;
    return obj;
}

/* ============================================================
   LIBERAR la memoria del archivo objeto
   ============================================================ */
void obj_free(ObjectFile *obj) {
    if (obj) free(obj);
}

/* ============================================================
   AGREGAR una seccion (ej: ".text", ".data", ".bss")
   Devuelve el indice de la nueva seccion, o -1 si hay error
   ============================================================ */
int obj_add_section(ObjectFile *obj, const char *name) {
    if (obj->header.num_sections >= MAX_SECTIONS) {
        printf("Error: limite de secciones alcanzado\n");
        return -1;
    }
    int idx = obj->header.num_sections;
    strncpy(obj->sections[idx].name, name, 15);
    obj->sections[idx].size = 0;
    obj->header.num_sections++;
    return idx;
}

/* ============================================================
   ESCRIBIR bytes en una seccion
   Asi es como el encoder deposita el codigo maquina en .text
   ============================================================ */
int obj_write_bytes(ObjectFile *obj, int sec_idx,
                    const uint8_t *bytes, uint32_t len) {
    if (sec_idx < 0 || sec_idx >= (int)obj->header.num_sections) {
        printf("Error: indice de seccion invalido\n");
        return -1;
    }
    Section *sec = &obj->sections[sec_idx];
    if (sec->size + len > MAX_SECTION_DATA) {
        printf("Error: seccion '%s' sin espacio\n", sec->name);
        return -1;
    }
    memcpy(sec->data + sec->size, bytes, len);
    sec->size += len;
    return 0;
}

/* ============================================================
   AGREGAR un simbolo a la tabla de simbolos
   Devuelve el indice del nuevo simbolo, o -1 si hay error
   ============================================================ */
int obj_add_symbol(ObjectFile *obj, const char *name,
                   uint32_t value, int sec_idx, SymbolType type) {
    if (obj->header.num_symbols >= MAX_SYMBOLS) {
        printf("Error: limite de simbolos alcanzado\n");
        return -1;
    }
    int idx = obj->header.num_symbols;
    strncpy(obj->symbols[idx].name, name, 63);
    obj->symbols[idx].value     = value;
    obj->symbols[idx].section_idx = sec_idx;
    obj->symbols[idx].type      = type;
    obj->symbols[idx].defined   = (type != SYM_EXTERN) ? 1 : 0;
    obj->header.num_symbols++;
    return idx;
}

/* ============================================================
   BUSCAR un simbolo por nombre
   Devuelve su indice, o -1 si no existe
   ============================================================ */
int obj_find_symbol(ObjectFile *obj, const char *name) {
    for (int i = 0; i < (int)obj->header.num_symbols; i++) {
        if (strcmp(obj->symbols[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* ============================================================
   AGREGAR una relocacion
   Devuelve el indice de la nueva relocacion, o -1 si hay error
   ============================================================ */
int obj_add_reloc(ObjectFile *obj, uint32_t offset,
                  int sym_idx, RelocType type, int32_t addend) {
    if (obj->header.num_relocs >= MAX_RELOCATIONS) {
        printf("Error: limite de relocaciones alcanzado\n");
        return -1;
    }
    int idx = obj->header.num_relocs;
    obj->relocs[idx].offset  = offset;
    obj->relocs[idx].sym_idx = sym_idx;
    obj->relocs[idx].type    = type;
    obj->relocs[idx].addend  = addend;
    obj->header.num_relocs++;
    return idx;
}

/* ============================================================
   ESCRIBIR el archivo objeto a disco (.o)
   Formato: [Header][Sections][Symbols][Relocations]
   ============================================================ */
int obj_write_file(ObjectFile *obj, const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        printf("Error: no se pudo abrir '%s' para escritura\n", filename);
        return -1;
    }

    /* 1. Escribir encabezado */
    fwrite(&obj->header, sizeof(ObjHeader), 1, f);

    /* 2. Escribir secciones */
    for (int i = 0; i < (int)obj->header.num_sections; i++) {
        fwrite(&obj->sections[i], sizeof(Section), 1, f);
    }

    /* 3. Escribir tabla de simbolos */
    for (int i = 0; i < (int)obj->header.num_symbols; i++) {
        fwrite(&obj->symbols[i], sizeof(Symbol), 1, f);
    }

    /* 4. Escribir tabla de relocaciones */
    for (int i = 0; i < (int)obj->header.num_relocs; i++) {
        fwrite(&obj->relocs[i], sizeof(Relocation), 1, f);
    }

    fclose(f);
    printf("Archivo objeto '%s' escrito correctamente\n", filename);
    return 0;
}

/* ============================================================
   LEER un archivo objeto desde disco
   ============================================================ */
ObjectFile* obj_read_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("Error: no se pudo abrir '%s' para lectura\n", filename);
        return NULL;
    }

    ObjectFile *obj = calloc(1, sizeof(ObjectFile));
    if (!obj) { fclose(f); return NULL; }

    /* Leer encabezado y verificar magic number */
    if (fread(&obj->header, sizeof(ObjHeader), 1, f) != 1) {
        printf("Error: Fallo al leer el encabezado del archivo objeto '%s'\n", filename);
        free(obj);
        fclose(f);
        return NULL;
    }
    
    if (obj->header.magic != OBJ_MAGIC) {
        printf("Error: '%s' no es un archivo objeto valido\n", filename);
        free(obj);
        fclose(f);
        return NULL;
    }

    /* Leer secciones */
    for (int i = 0; i < (int)obj->header.num_sections; i++) {
        if (fread(&obj->sections[i], sizeof(Section), 1, f) != 1) {
            printf("Error: Fallo al leer la seccion %d del archivo objeto '%s'\n", i, filename);
            free(obj);
            fclose(f);
            return NULL;
        }
    }

    /* Leer simbolos */
    for (int i = 0; i < (int)obj->header.num_symbols; i++) {
        if (fread(&obj->symbols[i], sizeof(Symbol), 1, f) != 1) {
            printf("Error: Fallo al leer el simbolo %d del archivo objeto '%s'\n", i, filename);
            free(obj);
            fclose(f);
            return NULL;
        }
    }

    /* Leer relocaciones */
    for (int i = 0; i < (int)obj->header.num_relocs; i++) {
        if (fread(&obj->relocs[i], sizeof(Relocation), 1, f) != 1) {
            printf("Error: Fallo al leer la relocacion %d del archivo objeto '%s'\n", i, filename);
            free(obj);
            fclose(f);
            return NULL;
        }
    }

    fclose(f);
    return obj;
}

/* ============================================================
   IMPRIMIR el contenido del archivo objeto (para debug)
   ============================================================ */
void obj_print(ObjectFile *obj) {
    printf("=== ARCHIVO OBJETO ===\n");
    printf("Magic:       0x%08X\n", obj->header.magic);
    printf("Secciones:   %u\n", obj->header.num_sections);
    printf("Simbolos:    %u\n", obj->header.num_symbols);
    printf("Relocaciones:%u\n\n", obj->header.num_relocs);

    /* Imprimir secciones */
    printf("--- SECCIONES ---\n");
    for (int i = 0; i < (int)obj->header.num_sections; i++) {
        Section *s = &obj->sections[i];
        printf("[%d] %-8s  %u bytes\n", i, s->name, s->size);
        /* Imprimir los bytes en hex */
        for (int j = 0; j < (int)s->size; j++) {
            printf("%02X ", s->data[j]);
        }
        if (s->size > 0) printf("\n");
    }

    /* Imprimir simbolos */
    printf("\n--- SIMBOLOS ---\n");
    const char *tipos[] = {"LOCAL", "GLOBAL", "EXTERN"};
    for (int i = 0; i < (int)obj->header.num_symbols; i++) {
        Symbol *sym = &obj->symbols[i];
        printf("[%d] %-20s  val=0x%04X  sec=%d  tipo=%s  def=%d\n",
               i, sym->name, sym->value,
               sym->section_idx, tipos[sym->type], sym->defined);
    }

    /* Imprimir relocaciones */
    printf("\n--- RELOCACIONES ---\n");
    const char *rtypes[] = {"REL32", "ABS32"};
    for (int i = 0; i < (int)obj->header.num_relocs; i++) {
        Relocation *r = &obj->relocs[i];
        printf("[%d] offset=0x%04X  sym=%d  tipo=%s  addend=%d\n",
               i, r->offset, r->sym_idx, rtypes[r->type], r->addend);
    }
    printf("======================\n");
}