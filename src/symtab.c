#include <stdio.h>
#include <string.h>
#include "../include/symtab.h"

// Variables globales para manejar la tabla
SymTabEntry symbol_table[SYMTAB_MAX_SYMBOLS];
int symbol_count = 0;

void init_symtab() {
    symbol_count = 0;
    memset(symbol_table, 0, sizeof(symbol_table));
}

int add_symbol(const char *name, int address, int defined) {
    // Primero verificamos si el símbolo ya existe
    // Corregido: Tipo cambiado a SymTabEntry* en lugar del obsoleto Symbol*
    SymTabEntry *existing = get_symbol(name);
    
    if (existing != NULL) {
        if (existing->defined && defined) {
            printf("Error: Simbolo '%s' redefinido.\n", name);
            return -1;
        }
        if (!existing->defined && defined) {
            // Resolviendo una referencia adelantada
            existing->address = address;
            existing->defined = 1;
            return 0; 
        }
        return 0; 
    }

    // Si no existe y hay espacio, lo creamos
    // Corregido: Cambiado a la macro correcta SYMTAB_MAX_SYMBOLS
    if (symbol_count >= SYMTAB_MAX_SYMBOLS) {
        printf("Error: Tabla de simbolos llena.\n");
        return -1;
    }

    strcpy(symbol_table[symbol_count].name, name);
    symbol_table[symbol_count].address = address;
    symbol_table[symbol_count].defined = defined;
    symbol_count++;

    return 0;
}

SymTabEntry* get_symbol(const char *name) {
    for (int i = 0; i < symbol_count; i++) {
        if (strcmp(symbol_table[i].name, name) == 0) {
            return &symbol_table[i];
        }
    }
    return NULL;
}

void print_symtab() {
    printf("\n--- Tabla de Simbolos ---\n");
    printf("Nombre\t\tDireccion\tDefinido\n");
    for (int i = 0; i < symbol_count; i++) {
        printf("%-15s\t0x%04X\t\t%d\n", 
               symbol_table[i].name, 
               symbol_table[i].address, 
               symbol_table[i].defined);
    }
    printf("-------------------------\n");
}

// Enlace de unificación: Mapea la llamada alternativa al método real de impresión
void print_symbol_table() {
    print_symtab();
}