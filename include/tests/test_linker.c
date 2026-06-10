#include <stdio.h>
#include <string.h>
#include "../include/object.h"
#include "../include/linker.h"

/* ============================================================
   TEST 1: Crear un archivo objeto y escribirlo a disco
   ============================================================ */
void test_objeto_basico() {
    printf("\n=== TEST 1: Objeto basico ===\n");

    ObjectFile *obj = obj_create();

    /* Agregar seccion .text */
    int text = obj_add_section(obj, ".text");

    /* Simular bytes de MOV EAX, 10  ->  B8 0A 00 00 00 */
    uint8_t bytes[] = {0xB8, 0x0A, 0x00, 0x00, 0x00};
    obj_write_bytes(obj, text, bytes, 5);

    /* Agregar simbolo GLOBAL "main" en offset 0 de .text */
    obj_add_symbol(obj, "main", 0, text, SYM_GLOBAL);

    /* Imprimir contenido */
    obj_print(obj);

    /* Escribir a disco */
    obj_write_file(obj, "tests/test1.o");

    /* Leer de vuelta y verificar */
    ObjectFile *leido = obj_read_file("tests/test1.o");
    if (leido && leido->header.magic == OBJ_MAGIC) {
        printf("TEST 1 PASADO: archivo objeto escrito y leido correctamente\n");
    } else {
        printf("TEST 1 FALLADO\n");
    }

    obj_free(obj);
    obj_free(leido);
}

/* ============================================================
   TEST 2: Dos modulos, CALL entre ellos (relocacion REL32)
   modulo A define "main" y hace CALL a "suma"
   modulo B define "suma"
   ============================================================ */
void test_dos_modulos() {
    printf("\n=== TEST 2: Dos modulos con CALL ===\n");

    /* --- MODULO A --- */
    ObjectFile *objA = obj_create();
    int textA = obj_add_section(objA, ".text");

    /*
     * Simula este codigo:
     *   main:
     *     MOV EAX, 5       -> B8 05 00 00 00
     *     CALL suma        -> E8 00 00 00 00  (placeholder)
     *     RET              -> C3
     */
    uint8_t bytesA[] = {
        0xB8, 0x05, 0x00, 0x00, 0x00,  /* MOV EAX, 5  */
        0xE8, 0x00, 0x00, 0x00, 0x00,  /* CALL suma   */
        0xC3                            /* RET         */
    };
    obj_write_bytes(objA, textA, bytesA, 11);

    /* Simbolos de A */
    obj_add_symbol(objA, "main", 0, textA, SYM_GLOBAL);
    int sym_suma = obj_add_symbol(objA, "suma", 0, -1, SYM_EXTERN);

    /* Relocacion: el CALL esta en offset 5, necesita REL32 a "suma" */
    obj_add_reloc(objA, 6, sym_suma, RELOC_REL32, 0);

    obj_write_file(objA, "tests/moduloA.o");

    /* --- MODULO B --- */
    ObjectFile *objB = obj_create();
    int textB = obj_add_section(objB, ".text");

    /*
     * Simula este codigo:
     *   suma:
     *     ADD EAX, 10      -> 83 C0 0A
     *     RET              -> C3
     */
    uint8_t bytesB[] = {
        0x83, 0xC0, 0x0A,  /* ADD EAX, 10 */
        0xC3               /* RET         */
    };
    obj_write_bytes(objB, textB, bytesB, 4);

    obj_add_symbol(objB, "suma", 0, textB, SYM_GLOBAL);

    obj_write_file(objB, "tests/moduloB.o");

    /* --- LINKER --- */
    LinkerCtx *ctx = linker_create();
    linker_add_object(ctx, objA);
    linker_add_object(ctx, objB);

    int resultado = linker_link(ctx, "tests/output.bin");

    if (resultado == 0) {
        printf("TEST 2 PASADO: dos modulos enlazados correctamente\n");
    } else {
        printf("TEST 2 FALLADO\n");
    }

    linker_print(ctx);

    obj_free(objA);
    obj_free(objB);
    linker_free(ctx);
}

/* ============================================================
   TEST 3: Simbolo EXTERN no resuelto (debe dar error)
   ============================================================ */
void test_extern_no_resuelto() {
    printf("\n=== TEST 3: EXTERN no resuelto ===\n");

    ObjectFile *obj = obj_create();
    int text = obj_add_section(obj, ".text");

    uint8_t bytes[] = {0xE8, 0x00, 0x00, 0x00, 0x00};
    obj_write_bytes(obj, text, bytes, 5);

    int sym = obj_add_symbol(obj, "funcion_inexistente", 0, -1, SYM_EXTERN);
    obj_add_reloc(obj, 1, sym, RELOC_REL32, 0);

    LinkerCtx *ctx = linker_create();
    linker_add_object(ctx, obj);

    int resultado = linker_link(ctx, "tests/no_deberia_crearse.bin");

    if (resultado != 0) {
        printf("TEST 3 PASADO: error detectado correctamente\n");
    } else {
        printf("TEST 3 FALLADO: debio detectar error\n");
    }

    obj_free(obj);
    linker_free(ctx);
}

/* ============================================================
   MAIN
   ============================================================ */
int main() {
    printf("========================================\n");
    printf("  PRUEBAS DEL FORMATO OBJETO Y LINKER   \n");
    printf("========================================\n");

    test_objeto_basico();
    test_dos_modulos();
    test_extern_no_resuelto();

    printf("\n========================================\n");
    printf("  PRUEBAS TERMINADAS\n");
    printf("========================================\n");
    return 0;
}
