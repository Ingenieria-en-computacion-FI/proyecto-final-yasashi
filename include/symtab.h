#ifndef SYMTAB_H
#define SYMTAB_H

#define MAX_SYMBOLS 500

typedef struct {
    char name[64];
    int address; // Aquí guardaremos el LC (Location Counter)
    int defined; // 1 si ya conocemos su dirección, 0 si es referencia adelantada
} Symbol;

// Prototipos de funciones
void init_symtab();
int add_symbol(const char *name, int address, int defined);
Symbol* get_symbol(const char *name);
void print_symtab();

#endif