#ifndef SYMTAB_H
#define SYMTAB_H

#define SYMTAB_MAX_SYMBOLS 500

typedef struct {
    char name[64];
    int  address;
    int  defined;
} SymTabEntry;

// Prototipos de funciones actualizados
int add_symbol(const char *name, int address, int defined);
SymTabEntry* get_symbol(const char *name);
void print_symbol_table(void);

#endif