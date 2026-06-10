#ifdef _WIN32
  #define strcasecmp _stricmp
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // Esto repara de golpe el error de strcasecmp en Windows
#include <ctype.h>   
#include "../include/lexer.h"

// Función auxiliar para verificar si una palabra es un registro de la IA-32
int is_register(const char *lexeme) {
    const char *registers[] = {"EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP", "ESP"};
    for (int i = 0; i < 8; i++) {
        if (strcasecmp(lexeme, registers[i]) == 0) { 
            return 1;
        }
    }
    return 0;
}

Token get_next_token(FILE *file) {
    //  Inicializar la estructura limpia con = {0} es mucho más seguro en C
    // y elimina cualquier queja de memset/sizeof por parte de VS Code.
    Token token = {0}; 
    int c;

    while (1) {
        c = fgetc(file);

        if (c == EOF) {
            token.type = TOKEN_EOF;
            strcpy(token.lexeme, "EOF");
            return token;
        }

        // 1. Ignorar espacios en blanco y tabuladores
        if (c == ' ' || c == '\t') {
            continue;
        }

        // 2. Ignorar comentarios
        if (c == ';') {
            while ((c = fgetc(file)) != '\n' && c != EOF);
        }

        // 3. Detectar saltos de línea explícitos
        if (c == '\n') {
            token.type = TOKEN_NEWLINE;
            strcpy(token.lexeme, "\\n");
            return token;
        }

        // 4. Detectar caracteres de puntuación y operadores únicos
        if (c == ',') {
            token.type = TOKEN_COMMA;
            strcpy(token.lexeme, ",");
            return token;
        }
        if (c == ':') {
            token.type = TOKEN_COLON;
            strcpy(token.lexeme, ":");
            return token;
        }
        if (c == '[') {
            token.type = TOKEN_LBRACKET;
            strcpy(token.lexeme, "[");
            return token;
        }
        if (c == ']') {
            token.type = TOKEN_RBRACKET;
            strcpy(token.lexeme, "]");
            return token;
        }
        if (c == '+') {
            token.type = TOKEN_PLUS;
            strcpy(token.lexeme, "+");
            return token;
        }
        if (c == '*') {
            token.type = TOKEN_STAR;
            strcpy(token.lexeme, "*");
            return token;
        }

        // 5. Detectar Números
        if (isdigit(c)) {
            int i = 0;
            token.lexeme[i++] = (char)c;
            while (isdigit(c = fgetc(file)) && i < 63) {
                token.lexeme[i++] = (char)c;
            }
            if (c != EOF) {
                ungetc(c, file); 
            }
            token.type = TOKEN_NUMBER;
            return token;
        }

        // 6. Detectar Palabras
        if (isalpha(c) || c == '_') {
            int i = 0;
            token.lexeme[i++] = (char)c;
            while ((isalnum(c = fgetc(file)) || c == '_') && i < 63) {
                token.lexeme[i++] = (char)c;
            }
            if (c != EOF) {
                ungetc(c, file); 
            }

            if (is_register(token.lexeme)) {
                token.type = TOKEN_REGISTER;
            } else {
                token.type = TOKEN_IDENTIFIER;
            }
            return token;
        }

        printf("Error Léxico: Carácter inválido '%c' encontrado.\n", c);
    }
}