#include <stdio.h>
#include <stdlib.h>
#include "include/parser.h"

int main() {
    FILE *file = fopen("test.asm", "r");
    if (file == NULL) {
        printf("Error: No se pudo abrir el archivo test.asm\n");
        return 1;
    }

    printf("=== Iniciando Ensamblador - Pasada 1 ===\n\n");
    parse_program(file);

    // Rebobinamos el archivo para la Pasada 2
    rewind(file);

    printf("\n=== Iniciando Ensamblador - Pasada 2 ===\n\n");
    pass2_program(file);

    fclose(file);
    return 0;
}