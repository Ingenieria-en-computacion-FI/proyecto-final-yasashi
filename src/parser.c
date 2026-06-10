#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/lexer.h"   
#include "../include/parser.h"
#include "../include/symtab.h"

Token current_token;
FILE *source_file;
int LC = 0;

void advance() {
    current_token = get_next_token(source_file);
}

void match(TokenType expected_type) {
    if (current_token.type == expected_type) {
        advance();
    } else {
        printf("Error Sintactico: Se esperaba el tipo %d, pero se encontro '%s' (Tipo: %d)\n", 
               expected_type, current_token.lexeme, current_token.type);
        exit(1);
    }
}

void parse_mov() {
    ParsedInstruction inst;
    memset(&inst, 0, sizeof(ParsedInstruction));
    strcpy(inst.opcode, "MOV");

    if (current_token.type == TOKEN_REGISTER) {
        printf("Parser: Registro destino detectado -> %s\n", current_token.lexeme);
        advance();
    } else if (current_token.type == TOKEN_LBRACKET) {
        inst.has_memory = 1;
        match(TOKEN_LBRACKET);
        if (current_token.type == TOKEN_REGISTER) advance();
        match(TOKEN_RBRACKET);
    }

    match(TOKEN_COMMA);

    if (current_token.type == TOKEN_REGISTER) {
        printf("Parser: Registro origen detectado -> %s\n", current_token.lexeme);
        advance();
    } else if (current_token.type == TOKEN_NUMBER) {
        inst.immediate = atoi(current_token.lexeme);
        printf("Parser: Valor inmediato detectado -> %d\n", inst.immediate);
        advance();
    }

    printf("Parser: Instruccion MOV analizada con exito\n");
    LC += 2;
}

// --- PASS 1 ---
void parse_program(FILE *file) {
    source_file = file;
    advance(); 

    init_symtab();
    LC = 0;

    while (current_token.type != TOKEN_EOF) {
        if (current_token.type == TOKEN_NEWLINE) {
            advance();
            continue;
        }

        if (current_token.type == TOKEN_IDENTIFIER) {
            char temp_lexeme[64];
            strcpy(temp_lexeme, current_token.lexeme);
            
            advance(); 

            if (current_token.type == TOKEN_COLON) {
                printf("Pass 1: Etiqueta '%s' registrada en LC = 0x%04X\n", temp_lexeme, LC);
                add_symbol(temp_lexeme, LC, 1);
                advance(); 
                continue; 
            } else {
                if (strcmp(temp_lexeme, "MOV") == 0) {
                    parse_mov();
                } else if (strcmp(temp_lexeme, "JMP") == 0) {
                    if (current_token.type == TOKEN_IDENTIFIER) {
                        if (get_symbol(current_token.lexeme) == NULL) {
                            add_symbol(current_token.lexeme, 0, 0);
                        }
                        advance(); 
                    } else {
                        printf("Error Sintactico: JMP requiere una etiqueta destino.\n");
                        exit(1);
                    }
                    LC += 2; 
                } else {
                    printf("Error Sintactico: Instruccion '%s' no soportada aun.\n", temp_lexeme);
                    exit(1);
                }
            }
        } else {
            printf("Error Sintactico: Se esperaba instruccion o etiqueta, se encontro: %s\n", current_token.lexeme);
            exit(1);
        }

        if (current_token.type == TOKEN_EOF) {
            break; 
        }
        match(TOKEN_NEWLINE);
    }
    
    printf("\nPass 1: El archivo completo ha sido analizado de forma correcta.\n");
    print_symtab();
}

// --- PASS 2 ---
void pass2_program(FILE *file) {
    FILE *output_file = fopen("output.hex", "w");
    if (output_file == NULL) {
        printf("Error: No se pudo crear el archivo output.hex\n");
        exit(1);
    }

    source_file = file;
    advance(); 
    int current_LC = 0; 

    fprintf(output_file, "--- CODE MACHINE GENERATED ---\n");

    while (current_token.type != TOKEN_EOF) {
        if (current_token.type == TOKEN_NEWLINE) {
            advance();
            continue;
        }

        if (current_token.type == TOKEN_IDENTIFIER) {
            char temp_lexeme[64];
            strcpy(temp_lexeme, current_token.lexeme);
            advance();

            if (current_token.type == TOKEN_COLON) {
                advance();
                continue;
            } else if (strcmp(temp_lexeme, "MOV") == 0) {
                printf("Pass 2: Traduciendo MOV en LC = 0x%04X\n", current_LC);
                
                // Simulación de escritura de opcode base para MOV
                fprintf(output_file, "0x%04X: 8B\n", current_LC);
                
                while(current_token.type != TOKEN_NEWLINE && current_token.type != TOKEN_EOF) {
                    advance();
                }
                current_LC += 2;
            } else if (strcmp(temp_lexeme, "JMP") == 0) {
                printf("Pass 2: Procesando JMP en LC = 0x%04X\n", current_LC);
                if (current_token.type == TOKEN_IDENTIFIER) {
                    Symbol *sym = get_symbol(current_token.lexeme);
                    if (sym != NULL && sym->defined) {
                        int offset = sym->address - (current_LC + 2);
                        printf("Pass 2: Fixup aplicado para referencia '%s'. Offset hexadecimal calculado: 0x%02X\n", 
                               sym->name, (unsigned char)offset);
                        
                        // Escritura del opcode de JMP (EB) y el offset calculado (Fixup)
                        fprintf(output_file, "0x%04X: EB %02X\n", current_LC, (unsigned char)offset);
                    } else {
                        printf("Error: Etiqueta '%s' no resuelta.\n", current_token.lexeme);
                        fclose(output_file);
                        exit(1);
                    }
                    advance();
                }
                current_LC += 2;
            }
        } else {
            advance(); 
        }
    }
    
    fclose(output_file);
    printf("\nPass 2 terminada. Archivo 'output.hex' generado con exito.\n");
}