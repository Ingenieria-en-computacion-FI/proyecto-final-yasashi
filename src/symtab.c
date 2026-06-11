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

void symtab_clear() {
    symbol_count = 0;
    memset(symbol_table, 0, sizeof(symbol_table));
}

/* ============================================================
 * NUEVAS: Funciones de acceso para el exportador del Object
 * ============================================================ */
int symtab_get_count(void) {
    return symbol_count;
}

SymTabEntry* symtab_get_entry(int index) {
    if (index >= 0 && index < symbol_count) {
        return &symbol_table[index];
    }
    return NULL;
}

int add_symbol(const char *name, int address, int defined) {
    SymTabEntry *existing = get_symbol(name);
    
    if (existing != NULL) {
        if (existing->defined && defined) {
            printf("Error: Simbolo '%s' redefinido.\n", name);
            return -1;
        }
        if (!existing->defined && defined) {
            existing->address = address;
            existing->defined = 1;
            existing->is_extern = 0; 
            return 0; 
        }
        return 0; 
    }

    if (symbol_count >= SYMTAB_MAX_SYMBOLS) {
        printf("Error: Tabla de simbolos llena.\n");
        return -1;
    }

    strcpy(symbol_table[symbol_count].name, name);
    symbol_table[symbol_count].address = address;
    symbol_table[symbol_count].defined = defined;
    symbol_table[symbol_count].is_extern = 0; 
    symbol_count++;

    return 0;
}

int add_extern_symbol(const char *name) {
    SymTabEntry *existing = get_symbol(name);
    if (existing != NULL) {
        return 0;
    }

    if (symbol_count >= SYMTAB_MAX_SYMBOLS) {
        printf("Error: Tabla de simbolos llena al registrar EXTERN.\n");
        return -1;
    }

    strcpy(symbol_table[symbol_count].name, name);
    symbol_table[symbol_count].address = 0x0000; 
    symbol_table[symbol_count].defined = 0;       
    symbol_table[symbol_count].is_extern = 1;    
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
    printf("%-15s\t%-10s\t%-10s\t%-10s\n", "Nombre", "Direccion", "Definido", "Externo");
    for (int i = 0; i < symbol_count; i++) {
        printf("%-15s\t0x%04X\t\t%d\t\t%d\n", 
               symbol_table[i].name, 
               symbol_table[i].address, 
               symbol_table[i].defined,
               symbol_table[i].is_extern);
    }
    printf("-------------------------\n");
}

void print_symbol_table() {
    print_symtab();
}