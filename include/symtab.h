#ifndef SYMTAB_H
#define SYMTAB_H

#define SYMTAB_MAX_SYMBOLS 500

typedef struct {
    char name[64];
    int  address;
    int  defined;
    int  is_extern;  // 1 si fue declarado como EXTERN, 0 si es local/etiqueta
} SymTabEntry;

// Prototipos de funciones actualizados
void init_symtab(void);
int add_symbol(const char *name, int address, int defined);
int add_extern_symbol(const char *name);
SymTabEntry* get_symbol(const char *name);
void print_symbol_table(void);
void print_symtab(void);

#endif // SYMTAB_H