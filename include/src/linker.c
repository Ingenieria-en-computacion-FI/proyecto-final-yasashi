#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/linker.h"

/* ============================================================
   CREAR el contexto del linker
   ============================================================ */
LinkerCtx* linker_create(void) {
    LinkerCtx *ctx = calloc(1, sizeof(LinkerCtx));
    if (!ctx) {
        printf("Error: no se pudo reservar memoria para LinkerCtx\n");
        return NULL;
    }
    return ctx;
}

/* ============================================================
   LIBERAR memoria del linker
   ============================================================ */
void linker_free(LinkerCtx *ctx) {
    if (ctx) free(ctx);
}

/* ============================================================
   AGREGAR un archivo objeto al linker
   ============================================================ */
int linker_add_object(LinkerCtx *ctx, ObjectFile *obj) {
    if (ctx->num_objs >= MAX_OBJ_FILES) {
        printf("Error: limite de archivos objeto alcanzado\n");
        return -1;
    }
    ctx->objs[ctx->num_objs++] = obj;
    return 0;
}

/* ============================================================
   PASO 1: FUSIONAR SECCIONES
   Coloca todas las secciones .text una tras otra,
   luego todas las .data, luego todas las .bss.
   Calcula el offset final de cada seccion en el binario.
   ============================================================ */
int linker_merge_sections(LinkerCtx *ctx) {
    uint32_t current_offset = 0;

    /* Primero fusionar todas las secciones .text */
    for (int i = 0; i < ctx->num_objs; i++) {
        ObjectFile *obj = ctx->objs[i];
        for (int j = 0; j < (int)obj->header.num_sections; j++) {
            if (strcmp(obj->sections[j].name, ".text") != 0) continue;

            ctx->section_offsets[i][j] = current_offset;

            /* Copiar bytes al buffer de salida */
            memcpy(ctx->output + current_offset,
                   obj->sections[j].data,
                   obj->sections[j].size);
            current_offset += obj->sections[j].size;
        }
    }

    /* Luego fusionar todas las secciones .data */
    for (int i = 0; i < ctx->num_objs; i++) {
        ObjectFile *obj = ctx->objs[i];
        for (int j = 0; j < (int)obj->header.num_sections; j++) {
            if (strcmp(obj->sections[j].name, ".data") != 0) continue;

            ctx->section_offsets[i][j] = current_offset;
            memcpy(ctx->output + current_offset,
                   obj->sections[j].data,
                   obj->sections[j].size);
            current_offset += obj->sections[j].size;
        }
    }

    /* Luego las secciones .bss (no tienen bytes, solo reservan espacio) */
    for (int i = 0; i < ctx->num_objs; i++) {
        ObjectFile *obj = ctx->objs[i];
        for (int j = 0; j < (int)obj->header.num_sections; j++) {
            if (strcmp(obj->sections[j].name, ".bss") != 0) continue;

            ctx->section_offsets[i][j] = current_offset;
            /* .bss no copia bytes, solo avanza el offset */
            current_offset += obj->sections[j].size;
        }
    }

    ctx->output_size = current_offset;
    printf("Linker: secciones fusionadas, total %u bytes\n", current_offset);
    return 0;
}

/* ============================================================
   PASO 2: CONSTRUIR TABLA GLOBAL DE SIMBOLOS
   Recorre todos los .o y registra cada simbolo GLOBAL.
   Si encuentra un simbolo definido dos veces, es un error.
   ============================================================ */
int linker_build_symbol_table(LinkerCtx *ctx) {
    for (int i = 0; i < ctx->num_objs; i++) {
        ObjectFile *obj = ctx->objs[i];
        for (int j = 0; j < (int)obj->header.num_symbols; j++) {
            Symbol *sym = &obj->symbols[j];

            /* Solo nos interesan los simbolos GLOBAL y EXTERN */
            if (sym->type == SYM_LOCAL) continue;

            /* Buscar si ya existe en la tabla global */
            int found = -1;
            for (int k = 0; k < ctx->num_gsyms; k++) {
                if (strcmp(ctx->gsyms[k].name, sym->name) == 0) {
                    found = k;
                    break;
                }
            }

            if (sym->type == SYM_GLOBAL && sym->defined) {
                if (found >= 0 && ctx->gsyms[found].defined) {
                    /* Simbolo definido dos veces: error */
                    printf("Error: simbolo '%s' definido mas de una vez\n",
                           sym->name);
                    return -1;
                }

                /* Calcular direccion final del simbolo */
                uint32_t sec_off = ctx->section_offsets[i][sym->section_idx];
                uint32_t final_addr = LOAD_ADDRESS + sec_off + sym->value;

                if (found >= 0) {
                    /* Ya existia como EXTERN, ahora lo resolvemos */
                    ctx->gsyms[found].final_address = final_addr;
                    ctx->gsyms[found].obj_idx       = i;
                    ctx->gsyms[found].sec_idx       = sym->section_idx;
                    ctx->gsyms[found].sec_offset    = sym->value;
                    ctx->gsyms[found].defined       = 1;
                } else {
                    /* Nuevo simbolo global */
                    int idx = ctx->num_gsyms++;
                    strncpy(ctx->gsyms[idx].name, sym->name, 63);
                    ctx->gsyms[idx].final_address = final_addr;
                    ctx->gsyms[idx].obj_idx       = i;
                    ctx->gsyms[idx].sec_idx       = sym->section_idx;
                    ctx->gsyms[idx].sec_offset    = sym->value;
                    ctx->gsyms[idx].defined       = 1;
                }
            } else if (sym->type == SYM_EXTERN) {
                /* Registrar como pendiente si no existe ya */
                if (found < 0) {
                    int idx = ctx->num_gsyms++;
                    strncpy(ctx->gsyms[idx].name, sym->name, 63);
                    ctx->gsyms[idx].defined = 0;
                }
            }
        }
    }

    /* Verificar que todos los EXTERN fueron resueltos */
    for (int i = 0; i < ctx->num_gsyms; i++) {
        if (!ctx->gsyms[i].defined) {
            printf("Error: simbolo externo '%s' nunca fue definido\n",
                   ctx->gsyms[i].name);
            return -1;
        }
    }

    printf("Linker: tabla de simbolos construida, %d simbolos\n",
           ctx->num_gsyms);
    return 0;
}

/* ============================================================
   PASO 3: APLICAR RELOCACIONES
   Va a cada placeholder en el buffer de salida y escribe
   la direccion correcta del simbolo.
   ============================================================ */
int linker_apply_relocations(LinkerCtx *ctx) {
    for (int i = 0; i < ctx->num_objs; i++) {
        ObjectFile *obj = ctx->objs[i];

        for (int j = 0; j < (int)obj->header.num_relocs; j++) {
            Relocation *rel = &obj->relocs[j];
            Symbol     *sym = &obj->symbols[rel->sym_idx];

            /* Buscar el simbolo en la tabla global */
            int found = -1;
            for (int k = 0; k < ctx->num_gsyms; k++) {
                if (strcmp(ctx->gsyms[k].name, sym->name) == 0) {
                    found = k;
                    break;
                }
            }

            if (found < 0 || !ctx->gsyms[found].defined) {
                printf("Error: no se puede relocalizar '%s', indefinido\n",
                       sym->name);
                return -1;
            }

            /* Offset real en el buffer de salida donde esta el placeholder */
            /* Buscamos la seccion .text de este objeto para saber su offset */
            uint32_t text_offset = 0;
            for (int s = 0; s < (int)obj->header.num_sections; s++) {
                if (strcmp(obj->sections[s].name, ".text") == 0) {
                    text_offset = ctx->section_offsets[i][s];
                    break;
                }
            }

            uint32_t patch_pos = text_offset + rel->offset;
            uint32_t sym_addr  = ctx->gsyms[found].final_address;

            if (rel->type == RELOC_REL32) {
                /* Direccion relativa: sym_addr - (patch_pos + 4) + addend */
                uint32_t pc = LOAD_ADDRESS + patch_pos + 4;
                int32_t  value = (int32_t)(sym_addr - pc) + rel->addend;
                memcpy(ctx->output + patch_pos, &value, 4);
                printf("Linker: REL32 '%s' -> 0x%08X en offset 0x%04X\n",
                       sym->name, sym_addr, patch_pos);
            } else {
                /* Direccion absoluta */
                memcpy(ctx->output + patch_pos, &sym_addr, 4);
                printf("Linker: ABS32 '%s' -> 0x%08X en offset 0x%04X\n",
                       sym->name, sym_addr, patch_pos);
            }
        }
    }

    printf("Linker: relocaciones aplicadas\n");
    return 0;
}

/* ============================================================
   PASO 4: ESCRIBIR BINARIO FINAL
   ============================================================ */
int linker_write_binary(LinkerCtx *ctx, const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        printf("Error: no se pudo crear '%s'\n", filename);
        return -1;
    }
    fwrite(ctx->output, 1, ctx->output_size, f);
    fclose(f);
    printf("Linker: binario '%s' generado (%u bytes)\n",
           filename, ctx->output_size);
    return 0;
}

/* ============================================================
   FUNCION PRINCIPAL: ejecuta los 4 pasos en orden
   ============================================================ */
int linker_link(LinkerCtx *ctx, const char *output_file) {
    printf("=== LINKER INICIANDO ===\n");

    if (linker_merge_sections(ctx)      != 0) return -1;
    if (linker_build_symbol_table(ctx)  != 0) return -1;
    if (linker_apply_relocations(ctx)   != 0) return -1;
    if (linker_write_binary(ctx, output_file) != 0) return -1;

    printf("=== LINKER TERMINADO ===\n");
    return 0;
}

/* ============================================================
   IMPRIMIR estado del linker (debug)
   ============================================================ */
void linker_print(LinkerCtx *ctx) {
    printf("=== ESTADO DEL LINKER ===\n");
    printf("Archivos objeto cargados: %d\n", ctx->num_objs);
    printf("Simbolos globales:        %d\n\n", ctx->num_gsyms);

    printf("--- SIMBOLOS GLOBALES ---\n");
    for (int i = 0; i < ctx->num_gsyms; i++) {
        GlobalSymbol *g = &ctx->gsyms[i];
        printf("[%d] %-20s  addr=0x%08X  def=%d\n",
               i, g->name, g->final_address, g->defined);
    }

    printf("\n--- OFFSETS DE SECCIONES ---\n");
    for (int i = 0; i < ctx->num_objs; i++) {
        printf("Objeto [%d]:\n", i);
        ObjectFile *obj = ctx->objs[i];
        for (int j = 0; j < (int)obj->header.num_sections; j++) {
            printf("  %-8s -> offset 0x%04X\n",
                   obj->sections[j].name,
                   ctx->section_offsets[i][j]);
        }
    }
    printf("=========================\n");
}
