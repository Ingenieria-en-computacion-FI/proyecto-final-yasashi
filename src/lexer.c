#ifdef _WIN32
  #define strcasecmp _stricmp
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> 
#include <ctype.h>   
#include "../include/lexer.h"

// NUEVO: Instanciamos el contador global exportado en lexer.h
int current_line = 1;

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
    Token token = {0}; 
    int c;

    while (1) {
        c = fgetc(file);
        
        // Asignamos la línea actual al token en construcción
        token.line = current_line;

        if (c == EOF) {
            token.type = TOKEN_EOF;
            strcpy(token.lexeme, "EOF");
            return token;
        }

        // 1. Ignorar espacios en blanco, tabuladores y retornos de carro de Windows (\r)
        if (c == ' ' || c == '\t' || c == '\r') {
            continue;
        }

        // 2. Ignorar caracteres extraños o basura invisible (como el BOM que causa el '')
        if (c < 0 || c > 127 || (unsigned char)c > 0x7F) {
            continue;
        }

        // 3. Ignorar comentarios y asegurar que la línea termine limpiamente
        if (c == ';') {
            while ((c = fgetc(file)) != '\n' && c != EOF);
            if (c == '\n') {
                ungetc(c, file); // Devolvemos el \n al buffer para que lo cuente el siguiente bloque
            }
            continue;
        }

        // 4. Detectar saltos de línea explícitos
        if (c == '\n') {
            token.type = TOKEN_NEWLINE;
            strcpy(token.lexeme, "\\n");
            current_line++; // Aumentamos la línea PARA EL PRÓXIMO token
            return token;
        }

        // 5. Detectar caracteres de puntuación y operadores
        if (c == ',') { token.type = TOKEN_COMMA; strcpy(token.lexeme, ","); return token; }
        if (c == ':') { token.type = TOKEN_COLON; strcpy(token.lexeme, ":"); return token; }
        if (c == '[') { token.type = TOKEN_LBRACKET; strcpy(token.lexeme, "["); return token; }
        if (c == ']') { token.type = TOKEN_RBRACKET; strcpy(token.lexeme, "]"); return token; }
        if (c == '+') { token.type = TOKEN_PLUS; strcpy(token.lexeme, "+"); return token; }
        if (c == '*') { token.type = TOKEN_STAR; strcpy(token.lexeme, "*"); return token; }

        // 6. Detectar Números (Soporte Nativo para Decimales y Hexadecimales 0x...)
        if (isdigit(c)) {
            int i = 0;
            token.lexeme[i++] = (char)c;

            // Revisamos si el siguiente carácter indica formato Hexadecimal
            int next_c = fgetc(file);
            if (c == '0' && (next_c == 'x' || next_c == 'X')) {
                token.lexeme[i++] = (char)next_c;
                // Extraer todos los dígitos hexadecimales
                while (1) {
                    next_c = fgetc(file);
                    if (isxdigit(next_c)) {
                        if (i < 63) token.lexeme[i++] = (char)next_c;
                    } else {
                        break; // Fin del número
                    }
                }
                if (next_c != EOF) ungetc(next_c, file);
            } else {
                // Extracción normal de número decimal
                while (1) {
                    if (isdigit(next_c)) {
                        if (i < 63) token.lexeme[i++] = (char)next_c;
                    } else {
                        break;
                    }
                    next_c = fgetc(file);
                }
                if (next_c != EOF) ungetc(next_c, file);
            }
            
            token.lexeme[i] = '\0'; // Blindaje para evitar basura en la memoria
            token.type = TOKEN_NUMBER;
            return token;
        }

        // 7. Detectar Palabras (Etiquetas, mnemónicos y variables)
        if (isalpha(c) || c == '_' || c == '.') {
            int i = 0;
            token.lexeme[i++] = (char)c;
            
            while ((isalnum(c = fgetc(file)) || c == '_') && i < 63) {
                token.lexeme[i++] = (char)c;
            }
            if (c != EOF) {
                ungetc(c, file); 
            }
            token.lexeme[i] = '\0'; // Terminador nulo obligatorio

            if (is_register(token.lexeme)) {
                token.type = TOKEN_REGISTER;
            } else {
                token.type = TOKEN_IDENTIFIER;
            }
            return token;
        }

        // 8. Mensaje de Error Léxico actualizado con la línea
        printf("Error Léxico [Linea %d]: Carácter inválido '%c' encontrado.\n", token.line, c);
    }
}