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

// Función auxiliar para calcular el tamaño real de una instrucción en IA-32
int compute_instruction_size(const char *opcode, int op1_type, int op2_type) {
    if (strcmp(opcode, "MOV") == 0) {
        // MOV Registro, Inmediato (Ej: MOV EAX, 10) -> Opcode (1 byte) + Inmediato (4 bytes) = 5 bytes
        if (op1_type == TOKEN_REGISTER && op2_type == TOKEN_NUMBER) {
            return 5;
        }
        // MOV Registro, Registro (Ej: MOV EBX, EAX) -> Opcode (1 byte) + ModR/M (1 byte) = 2 bytes
        if (op1_type == TOKEN_REGISTER && op2_type == TOKEN_REGISTER) {
            return 2;
        }
        // MOV [Registro], Registro (Ej: MOV [EBX], EAX) -> 2 bytes
        if (op1_type == TOKEN_LBRACKET && op2_type == TOKEN_REGISTER) {
            return 2;
        }
    } else if (strcmp(opcode, "JMP") == 0) {
        // JMP Relativo de 32 bits (E9 + 4 bytes de offset) -> 5 bytes
        return 5; 
    }
    
    // Tamaño por defecto por si no coincide con los patrones
    return 2;
}

void parse_mov() {
    ParsedInstruction inst;
    memset(&inst, 0, sizeof(ParsedInstruction));
    strcpy(inst.opcode, "MOV");
    
    int op1_type = 0; 
    int op2_type = 0;
    int has_displacement = 0; // Registra si hay un desplazamiento numérico (Ej: + 4)

    // --- 1. OPERANDO 1: DESTINO ---
    if (current_token.type == TOKEN_REGISTER) {
        printf("Parser: Registro destino detectado -> %s\n", current_token.lexeme);
        op1_type = TOKEN_REGISTER;
        advance();
    } else if (current_token.type == TOKEN_LBRACKET) {
        printf("Parser: Memoria destino detectada -> [ ");
        op1_type = TOKEN_LBRACKET;
        inst.has_memory = 1;
        match(TOKEN_LBRACKET);
        
        // Bucle inteligente: consume el contenido del corchete y extrae propiedades esenciales
        while (current_token.type != TOKEN_RBRACKET && current_token.type != TOKEN_EOF) {
            printf("%s ", current_token.lexeme);
            if (current_token.type == TOKEN_NUMBER) {
                has_displacement = 1;
                inst.immediate = atoi(current_token.lexeme); // Guardamos el desplazamiento en immediate temporalmente
            }
            advance();
        }
        printf("]\n");
        match(TOKEN_RBRACKET);
    }

    match(TOKEN_COMMA);

    // --- 2. OPERANDO 2: ORIGEN ---
    if (current_token.type == TOKEN_REGISTER) {
        printf("Parser: Registro origen detectado -> %s\n", current_token.lexeme);
        op2_type = TOKEN_REGISTER;
        advance();
    } else if (current_token.type == TOKEN_NUMBER) {
        inst.immediate = atoi(current_token.lexeme);
        printf("Parser: Valor inmediato detectado -> %d\n", inst.immediate);
        op2_type = TOKEN_NUMBER;
        advance();
    } else if (current_token.type == TOKEN_LBRACKET) {
        printf("Parser: Memoria origen detectada -> [ ");
        op2_type = TOKEN_LBRACKET;
        inst.has_memory = 1;
        match(TOKEN_LBRACKET);
        
        // Bucle inteligente para el origen
        while (current_token.type != TOKEN_RBRACKET && current_token.type != TOKEN_EOF) {
            printf("%s ", current_token.lexeme);
            if (current_token.type == TOKEN_NUMBER) {
                has_displacement = 1;
                inst.immediate = atoi(current_token.lexeme); // Guardamos el desplazamiento numérico
            }
            advance();
        }
        printf("]\n");
        match(TOKEN_RBRACKET);
    }

    printf("Parser: Instruccion MOV analizada con exito\n");
    
    // --- CÁLCULO AJUSTADO DEL LC PARA IA-32 ---
    // Si es una instrucción con direccionamiento indirecto por registro y desplazamiento:
    if ((op1_type == TOKEN_LBRACKET || op2_type == TOKEN_LBRACKET) && has_displacement) {
        // Opcode + ModR/M + Displacement de 32-bits (4 bytes) = 6 bytes 
        // Nota: Si usan offsets cortos de 8-bits, cambien el valor a 3 (Opcode + ModR/M + 1 byte)
        LC += 6; 
    } else {
        // En caso contrario, usamos la tabla estándar que ya teníamos sincronizada
        LC += compute_instruction_size("MOV", op1_type, op2_type);
    }
}

// --- PASS 1 ---
void parse_program(FILE *file) {
    source_file = file;
    advance(); 

    init_symtab();
    LC = 0;

    while (current_token.type != TOKEN_EOF) {
        // Consumir saltos de línea iniciales o vacíos de forma segura
        if (current_token.type == TOKEN_NEWLINE) {
            advance();
            continue;
        }

        if (current_token.type == TOKEN_IDENTIFIER) {
            char temp_lexeme[64];
            strcpy(temp_lexeme, current_token.lexeme);
            advance(); 

            // Manejo robusto de Etiquetas
            if (current_token.type == TOKEN_COLON) {
                printf("Pass 1: Etiqueta '%s' registrada en LC = 0x%04X\n", temp_lexeme, LC);
                add_symbol(temp_lexeme, LC, 1);
                advance(); // Saltamos el TOKEN_COLON
                
                // Si la etiqueta estaba sola al final de la línea, consumimos su salto de línea
                if (current_token.type == TOKEN_NEWLINE) {
                    advance();
                }
                continue; // Evaluamos la siguiente instrucción inmediatamente
            } 
            else {
                // Procesamiento de Mnemónicos legítimos
                if (strcmp(temp_lexeme, "MOV") == 0) {
                    parse_mov();
                } else if (strcmp(temp_lexeme, "JMP") == 0) {
                    if (current_token.type == TOKEN_IDENTIFIER) {
                        printf("Parser: Instruccion JMP hacia etiqueta '%s' detectada\n", current_token.lexeme);
                        if (get_symbol(current_token.lexeme) == NULL) {
                            add_symbol(current_token.lexeme, 0, 0);
                        }
                        advance(); 
                    } else {
                        printf("Error Sintactico: JMP requiere una etiqueta destino.\n");
                        exit(1);
                    }
                    LC += compute_instruction_size("JMP", TOKEN_IDENTIFIER, 0); 
                } else {
                    printf("Error Sintactico: Instruccion '%s' no soportada aun.\n", temp_lexeme);
                    exit(1);
                }
                
                // Consumir el salto de línea obligatorio al terminar una instrucción completa
                if (current_token.type != TOKEN_EOF) {
                    match(TOKEN_NEWLINE);
                }
            }
        } else {
            printf("Error Sintactico: Se esperaba instruccion o etiqueta, se encontro: %s\n", current_token.lexeme);
            exit(1);
        }
    }
    
    printf("\nPass 1: El archivo completo ha sido analizado de forma correcta.\n");
    print_symbol_table();
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
                
                int local_op1 = 0, local_op2 = 0;
                if (current_token.type == TOKEN_REGISTER) local_op1 = TOKEN_REGISTER;
                else if (current_token.type == TOKEN_LBRACKET) local_op1 = TOKEN_LBRACKET;
                
                while(current_token.type != TOKEN_NEWLINE && current_token.type != TOKEN_EOF) {
                    if (current_token.type == TOKEN_NUMBER) local_op2 = TOKEN_NUMBER;
                    else if (current_token.type == TOKEN_REGISTER && local_op1 != 0) local_op2 = TOKEN_REGISTER;
                    advance();
                }
                current_LC += compute_instruction_size("MOV", local_op1, local_op2);
            } else if (strcmp(temp_lexeme, "JMP") == 0) {
                printf("Pass 2: Procesando JMP en LC = 0x%04X\n", current_LC);
                if (current_token.type == TOKEN_IDENTIFIER) {
                    SymTabEntry *sym = get_symbol(current_token.lexeme);
                    
                    if (sym != NULL && sym->defined) {
                        int offset = sym->address - (current_LC + 5); 
                        printf("Pass 2: Fixup aplicado para referencia '%s'. Offset hexadecimal calculado: 0x%02X\n", 
                               sym->name, (unsigned char)offset);
                        
                        fprintf(output_file, "0x%04X: E9 %02X %02X %02X %02X\n", 
                                current_LC, 
                                (unsigned char)(offset & 0xFF),
                                (unsigned char)((offset >> 8) & 0xFF),
                                (unsigned char)((offset >> 16) & 0xFF),
                                (unsigned char)((offset >> 24) & 0xFF));
                    } else {
                        printf("Error: Etiqueta '%s' no resuelta.\n", current_token.lexeme);
                        fclose(output_file);
                        exit(1);
                    }
                    advance();
                }
                current_LC += compute_instruction_size("JMP", TOKEN_IDENTIFIER, 0);
            }
        } else {
            advance(); 
        }
    }
    
    fclose(output_file);
    printf("\nPass 2 terminada. Archivo 'output.hex' generado con exito.\n");
}