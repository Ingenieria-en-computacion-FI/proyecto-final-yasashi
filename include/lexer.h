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
    TOKEN_COLON,    // <-- Agregado
    TOKEN_PLUS,     // <-- Agregado
    TOKEN_STAR,     // <-- Agregado
    TOKEN_EOF         
} TokenType;

typedef struct {
    TokenType type;
    char lexeme[64];
} Token;

Token get_next_token(FILE *file);

#endif