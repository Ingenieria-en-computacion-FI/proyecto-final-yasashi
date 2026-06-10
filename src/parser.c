#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/lexer.h"   
#include "../include/parser.h"
// Variables globales de control para el flujo de tokens
Token current_token;
FILE *source_file;

// Avanza al siguiente token utilizando la función del Lexer
void advance() {
    current_token = get_next_token(source_file);
}

// Verifica si el token actual es del tipo esperado; si es así, avanza. Si no, truena con error.
void match(TokenType expected_type) {
    if (current_token.type == expected_type) {
        advance();
    } else {
        printf("Error Sintactico: Se esperaba el tipo %d, pero se encontro '%s' (Tipo: %d)\n", 
               expected_type, current_token.lexeme, current_token.type);
        exit(1);
    }
}

// Lógica para parsear la instrucción MOV y extraer sus operandos
void parse_mov() {
    ParsedInstruction inst;
    memset(&inst, 0, sizeof(ParsedInstruction));
    strcpy(inst.opcode, "MOV");

    advance(); // Consumir el identificador "MOV"

    // --- PRIMER OPERANDO: DESTINO ---
    if (current_token.type == TOKEN_REGISTER) {
        printf("Parser: Registro destino detectado -> %s\n", current_token.lexeme);
        advance();
    } else if (current_token.type == TOKEN_LBRACKET) {
        inst.has_memory = 1;
        match(TOKEN_LBRACKET);
        if (current_token.type == TOKEN_REGISTER) advance();
        match(TOKEN_RBRACKET);
    }

    // Toda instrucción de transferencia de dos operandos exige una coma intermedia
    match(TOKEN_COMMA);

    // --- SEGUNDO OPERANDO: ORIGEN ---
    if (current_token.type == TOKEN_REGISTER) {
        printf("Parser: Registro origen detectado -> %s\n", current_token.lexeme);
        advance();
    } else if (current_token.type == TOKEN_NUMBER) {
        inst.immediate = atoi(current_token.lexeme);
        printf("Parser: Valor inmediato detectado -> %d\n", inst.immediate);
        advance();
    }

    printf("Parser: ¡Instruccion MOV analizada con exito!\n");
}

// Función que lee línea por línea el archivo .asm
void parse_program(FILE *file) {
    source_file = file;
    advance(); // Cargar el primer token del archivo

    while (current_token.type != TOKEN_EOF) {
        if (current_token.type == TOKEN_NEWLINE) {
            advance();
            continue;
        }

        if (current_token.type == TOKEN_IDENTIFIER) {
            if (strcmp(current_token.lexeme, "MOV") == 0) {
                parse_mov();
            } else {
                printf("Error Sintactico: Instruccion '%s' no soportada aun.\n", current_token.lexeme);
                exit(1);
            }
        } else {
            printf("Error Sintactico: Se esperaba instruccion o etiqueta, se encontro: %s\n", current_token.lexeme);
            exit(1);
        }

        // Cada instrucción debe cerrar correctamente con un salto de línea
        match(TOKEN_NEWLINE);
    }
    printf("Parser: El archivo completo ha sido analizado de forma correcta.\n");
}