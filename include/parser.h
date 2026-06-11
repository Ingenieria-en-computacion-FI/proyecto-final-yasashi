#ifndef PARSER_H
#define PARSER_H

#include <stdio.h>

typedef struct {
    char opcode[16];      // Mnemónico de la instrucción (MOV, JMP, ADD, etc.)
    int has_memory;       // Flag: 1 si usa direccionamiento de memoria, 0 si no
    int immediate;        // Valor inmediato (ej: MOV EAX, 10) o Desplazamiento (ej: [EBX+8])
    int has_displacement; // Flag: 1 si la dirección de memoria incluye un número sumado/restado
    
    // --- Campos de arquitectura para ModRM y SIB ---
    char base_reg[16];    // Registro Base (ej: "EBX", "EBP", "ESP")
    char index_reg[16];   // Registro Índice (ej: "ECX", "ESI")
    int scale;            // Escala del SIB: 1, 2, 4 u 8 (0 si no se usa índice)
} ParsedInstruction;

// Prototipos de las funciones principales del parser
void parse_program(FILE *file);
void pass2_program(FILE *file);

#endif