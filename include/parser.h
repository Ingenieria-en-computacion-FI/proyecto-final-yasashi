#ifndef PARSER_H
#define PARSER_H

#include <stdio.h>

typedef struct {
    char opcode[16];
    int has_memory;
    int immediate;
} ParsedInstruction;

// Prototipos de las funciones principales del parser
void parse_program(FILE *file);
void pass2_program(FILE *file);

#endif