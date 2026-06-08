#ifndef PARSER_H
#define PARSER_H

#include <stdio.h>
#include "lexer.h" // <-- PRIMERO cargamos el Lexer para que el compilador aprenda qué es 'TokenType' y 'Token'

// Estructura que empaqueta una instrucción completamente analizada
typedef struct {
    char opcode[16];       
    int has_memory;        
    int mod;               
    int reg;               
    int rm;                
    int scale;             
    int index;             
    int base;              
    int has_sib;           
    int displacement;      
    int immediate;         
} ParsedInstruction;

// Prototipos de las funciones del Parser (Ahora sí, abajo del include de lexer)
void parse_program(FILE *file);
void match(TokenType expected_type); 

#endif