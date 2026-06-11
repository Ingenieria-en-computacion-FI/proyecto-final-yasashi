#ifndef LEXER_H
#define LEXER_H

#include <stdio.h>

// Lista completa de tokens que tu lexer.c necesita reconocer
typedef enum {
    TOKEN_IDENTIFIER, 
    TOKEN_REGISTER,   
    TOKEN_NUMBER,     
    TOKEN_COMMA,      
    TOKEN_LBRACKET,   
    TOKEN_RBRACKET,   
    TOKEN_NEWLINE,    
    TOKEN_COLON,      
    TOKEN_PLUS,       
    TOKEN_STAR,       
    TOKEN_EOF         
} TokenType;

typedef struct {
    TokenType type;
    char lexeme[64];
    int line; // <-- NUEVO: Para rastrear en qué línea se encontró el token
} Token;

// <-- NUEVO: Contador global de líneas exportado para poder reiniciarlo entre pases
extern int current_line; 

Token get_next_token(FILE *file);

#endif