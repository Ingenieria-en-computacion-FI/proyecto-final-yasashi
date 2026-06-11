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

// Función auxiliar extendida para calcular el tamaño real de las instrucciones en IA-32
int compute_instruction_size(const char *opcode, int op1_type, int op2_type) {
    // --- 1. Grupo de Transferencia, Aritméticas y Lógicas de dos operandos ---
    if (strcmp(opcode, "MOV") == 0 || strcmp(opcode, "ADD") == 0 || 
        strcmp(opcode, "SUB") == 0 || strcmp(opcode, "CMP") == 0 ||
        strcmp(opcode, "AND") == 0 || strcmp(opcode, "OR") == 0  || 
        strcmp(opcode, "XOR") == 0) {
        
        // Reg, Inmediato -> Opcode + ModR/M + Inmediato de 32 bits = 5 o 6 bytes
        if (op1_type == TOKEN_REGISTER && op2_type == TOKEN_NUMBER) {
            return (strcmp(opcode, "MOV") == 0) ? 5 : 6;
        }
        // Reg, Reg -> Opcode + ModR/M = 2 bytes
        if (op1_type == TOKEN_REGISTER && op2_type == TOKEN_REGISTER) {
            return 2;
        }
    } 
    // --- 2. Grupo de Instrucciones Unarias (Un solo operando) ---
    else if (strcmp(opcode, "INC") == 0  || strcmp(opcode, "DEC") == 0 || 
             strcmp(opcode, "PUSH") == 0 || strcmp(opcode, "POP") == 0) {
        if (op1_type == TOKEN_REGISTER) {
            return 1; // IA-32 optimiza INC/DEC/PUSH/POP de registros a un solo byte de opcode
        }
    } 
    else if (strcmp(opcode, "MUL") == 0 || strcmp(opcode, "DIV") == 0) {
        if (op1_type == TOKEN_REGISTER) {
            return 2; // Opcode + ModR/M
        }
    }
    // --- 3. Grupo de Control de Flujo (Saltos e Invocaciones) ---
    else if (strcmp(opcode, "JMP") == 0  || strcmp(opcode, "CALL") == 0) {
        // Opcode (1 byte) + Dirección Relativa de 32 bits (4 bytes) = 5 bytes
        return 5; 
    }
    else if (strcmp(opcode, "JE") == 0   || strcmp(opcode, "JNE") == 0 || 
             strcmp(opcode, "JG") == 0   || strcmp(opcode, "JL") == 0) {
        // Opcode extendido (2 bytes: 0x0F 0x8X) + Dirección Relativa de 32 bits (4 bytes) = 6 bytes
        return 6;
    }
    
    return 2; // Tamaño de respaldo seguro por defecto
}

// Analizador SIB y expresiones de memoria
void parse_memory_expression(ParsedInstruction *inst) {
    match(TOKEN_LBRACKET); 
    
    if (current_token.type == TOKEN_REGISTER) {
        strcpy(inst->base_reg, current_token.lexeme);
        printf("%s ", current_token.lexeme);
        advance();
    } else if (current_token.type == TOKEN_NUMBER) {
        inst->has_displacement = 1;
        inst->immediate = atoi(current_token.lexeme);
        printf("%d ", inst->immediate);
        advance();
    }

    while (current_token.type != TOKEN_RBRACKET && current_token.type != TOKEN_EOF) {
        if (current_token.type == TOKEN_PLUS || strcmp(current_token.lexeme, "-") == 0) {
            int is_minus = (strcmp(current_token.lexeme, "-") == 0);
            printf("%s ", current_token.lexeme);
            advance(); 

            if (current_token.type == TOKEN_REGISTER) {
                char temp_reg[16];
                strcpy(temp_reg, current_token.lexeme);
                printf("%s ", current_token.lexeme);
                advance();

                if (current_token.type == TOKEN_STAR) {
                    printf("%s ", current_token.lexeme);
                    advance(); 
                    if (current_token.type == TOKEN_NUMBER) {
                        strcpy(inst->index_reg, temp_reg);
                        inst->scale = atoi(current_token.lexeme);
                        printf("%s ", current_token.lexeme);
                        advance();
                    } else {
                        printf("Error Sintactico: Se esperaba una escala numerica valida tras '*'.\n");
                        exit(1);
                    }
                } else {
                    strcpy(inst->index_reg, temp_reg);
                    inst->scale = 1; 
                }
            } else if (current_token.type == TOKEN_NUMBER) {
                inst->has_displacement = 1;
                int val = atoi(current_token.lexeme);
                inst->immediate = is_minus ? -val : val;
                printf("%s ", current_token.lexeme);
                advance();
            }
        } else {
            printf("Error Sintactico: Token inesperado '%s' dentro de la memoria.\n", current_token.lexeme);
            exit(1);
        }
    }
    match(TOKEN_RBRACKET); 
}

// Analizador genérico para cualquier instrucción de dos operandos (MOV, ADD, SUB, CMP, etc.)
void parse_two_operands(const char *opname) {
    ParsedInstruction inst;
    memset(&inst, 0, sizeof(ParsedInstruction));
    strcpy(inst.opcode, opname);
    
    int op1_type = 0; 
    int op2_type = 0;

    // Operando 1: Destino
    if (current_token.type == TOKEN_REGISTER) {
        printf("Parser: Registro destino detectado -> %s\n", current_token.lexeme);
        op1_type = TOKEN_REGISTER;
        advance();
    } else if (current_token.type == TOKEN_LBRACKET) {
        printf("Parser: Memoria destino detectada -> [ ");
        op1_type = TOKEN_LBRACKET;
        inst.has_memory = 1;
        parse_memory_expression(&inst);
        printf("]\n");
    }

    match(TOKEN_COMMA);

    // Operando 2: Origen
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
        parse_memory_expression(&inst);
        printf("]\n");
    }

    printf("Parser: Instruccion %s analizada con exito\n", opname);
    
    if (inst.has_memory) {
        int bytes_size = 1; // Opcode base (MOV o ALU r/m)
        
        if (strlen(inst.index_reg) > 0) {
            bytes_size += 2; // Requiere bytes ModRM + SIB
        } else {
            bytes_size += 1; // Requiere únicamente byte ModRM
        }

        if (inst.has_displacement) {
            if (inst.immediate >= -128 && inst.immediate <= 127) {
                bytes_size += 1; // Desplazamiento corto (8 bits)
            } else {
                bytes_size += 4; // Desplazamiento largo (32 bits)
            }
        }
        LC += bytes_size;
    } else {
        LC += compute_instruction_size(opname, op1_type, op2_type);
    }
}

// Analizador genérico para instrucciones unarias (INC, DEC, PUSH, POP, MUL, DIV)
void parse_one_operand(const char *opname) {
    int op_type = 0;
    if (current_token.type == TOKEN_REGISTER) {
        printf("Parser: Registro operando detectado -> %s\n", current_token.lexeme);
        op_type = TOKEN_REGISTER;
        advance();
    } else {
        printf("Error Sintactico: La instruccion %s requiere un registro como operando.\n", opname);
        exit(1);
    }
    printf("Parser: Instruccion %s analizada con exito\n", opname);
    LC += compute_instruction_size(opname, op_type, 0);
}

// Analizador genérico para saltos (JMP, JE, JNE, JG, JL, CALL)
void parse_branch(const char *opname) {
    if (current_token.type == TOKEN_IDENTIFIER) {
        printf("Parser: Instruccion %s hacia etiqueta '%s' detectada\n", opname, current_token.lexeme);
        if (get_symbol(current_token.lexeme) == NULL) {
            add_symbol(current_token.lexeme, 0, 0); // Registro preliminar de la etiqueta
        }
        advance(); 
    } else {
        printf("Error Sintactico: %s requiere una etiqueta destino legítima.\n", opname);
        exit(1);
    }
    LC += compute_instruction_size(opname, TOKEN_IDENTIFIER, 0); 
}

// --- PASS 1 ---
void parse_program(FILE *file) {
    source_file = file;
    advance(); 
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

            // Evaluar si es una Etiqueta
            if (current_token.type == TOKEN_COLON) {
                printf("Pass 1: Etiqueta '%s' registrada en LC = 0x%04X\n", temp_lexeme, LC);
                add_symbol(temp_lexeme, LC, 1);
                advance(); 
                if (current_token.type == TOKEN_NEWLINE) advance();
                continue; 
            } 
            // NUEVO: Interceptación y procesamiento de la directiva EXTERN
            else if (strcmp(temp_lexeme, "EXTERN") == 0) {
                if (current_token.type == TOKEN_IDENTIFIER) {
                    printf("Pass 1: Directiva EXTERN detectada. Registrando simbolo externo '%s' de forma provisional.\n", current_token.lexeme);
                    add_symbol(current_token.lexeme, 0, 1);
                    advance();
                } else {
                    printf("Error Sintactico: Se esperaba el nombre de un identificador despues de EXTERN.\n");
                    exit(1);
                }
                
                if (current_token.type != TOKEN_EOF) {
                    match(TOKEN_NEWLINE);
                }
                continue;
            }
            else {
                // RÚBRICA COMPLETA: Enrutador de Mnemónicos
                if (strcmp(temp_lexeme, "MOV") == 0 || strcmp(temp_lexeme, "ADD") == 0 || 
                    strcmp(temp_lexeme, "SUB") == 0 || strcmp(temp_lexeme, "CMP") == 0 ||
                    strcmp(temp_lexeme, "AND") == 0 || strcmp(temp_lexeme, "OR") == 0  || 
                    strcmp(temp_lexeme, "XOR") == 0) {
                    parse_two_operands(temp_lexeme);
                } 
                else if (strcmp(temp_lexeme, "INC") == 0 || strcmp(temp_lexeme, "DEC") == 0 ||
                         strcmp(temp_lexeme, "PUSH") == 0 || strcmp(temp_lexeme, "POP") == 0 ||
                         strcmp(temp_lexeme, "MUL") == 0 || strcmp(temp_lexeme, "DIV") == 0) {
                    parse_one_operand(temp_lexeme);
                }
                else if (strcmp(temp_lexeme, "JMP") == 0 || strcmp(temp_lexeme, "JE") == 0 ||
                         strcmp(temp_lexeme, "JNE") == 0 || strcmp(temp_lexeme, "JG") == 0 ||
                         strcmp(temp_lexeme, "JL") == 0 || strcmp(temp_lexeme, "CALL") == 0) {
                    parse_branch(temp_lexeme);
                } 
                else {
                    printf("Error Sintactico: Instruccion '%s' no reconocida por la arquitectura objetivo.\n", temp_lexeme);
                    exit(1);
                }
                
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
            } 
            // NUEVO: Ignorar limpiamente la línea EXTERN en la segunda pasada
            else if (strcmp(temp_lexeme, "EXTERN") == 0) {
                if (current_token.type == TOKEN_IDENTIFIER) {
                    advance(); 
                }
                if (current_token.type != TOKEN_EOF) {
                    match(TOKEN_NEWLINE);
                }
                continue; 
            }
            // Manejo en Pass 2 de Instrucciones de dos operandos
            else if (strcmp(temp_lexeme, "MOV") == 0 || strcmp(temp_lexeme, "ADD") == 0 || 
                     strcmp(temp_lexeme, "SUB") == 0 || strcmp(temp_lexeme, "CMP") == 0 ||
                     strcmp(temp_lexeme, "AND") == 0 || strcmp(temp_lexeme, "OR") == 0  || 
                     strcmp(temp_lexeme, "XOR") == 0) {
                
                printf("Pass 2: Traduciendo %s en LC = 0x%04X\n", temp_lexeme, current_LC);
                fprintf(output_file, "0x%04X: [OPCODE_%s]\n", current_LC, temp_lexeme);
                
                int local_op1 = 0, local_op2 = 0;
                int local_has_disp = 0, local_has_sib = 0;
                int local_imm_val = 0;

                if (current_token.type == TOKEN_REGISTER) local_op1 = TOKEN_REGISTER;
                else if (current_token.type == TOKEN_LBRACKET) local_op1 = TOKEN_LBRACKET;
                
                while(current_token.type != TOKEN_NEWLINE && current_token.type != TOKEN_EOF) {
                    if (current_token.type == TOKEN_NUMBER && local_op1 == TOKEN_LBRACKET) {
                        local_has_disp = 1;
                        local_imm_val = atoi(current_token.lexeme);
                    }
                    if (current_token.type == TOKEN_STAR) local_has_sib = 1;
                    if (current_token.type == TOKEN_NUMBER && local_op1 != TOKEN_LBRACKET) local_op2 = TOKEN_NUMBER;
                    else if (current_token.type == TOKEN_REGISTER && local_op1 != 0) local_op2 = TOKEN_REGISTER;
                    advance();
                }

                if (local_op1 == TOKEN_LBRACKET || local_op2 == TOKEN_LBRACKET) {
                    int b_size = 1; // Opcode base
                    if (local_has_sib) b_size += 2; // ModRM + SIB
                    else b_size += 1; // Solo ModRM

                    if (local_has_disp) {
                        if (local_imm_val >= -128 && local_imm_val <= 127) b_size += 1; // Desplazamiento corto
                        else b_size += 4; // Desplazamiento largo
                    }
                    current_LC += b_size;
                } else {
                    current_LC += compute_instruction_size(temp_lexeme, local_op1, local_op2);
                }
            } 
            // Manejo en Pass 2 de Instrucciones unarias
            else if (strcmp(temp_lexeme, "INC") == 0 || strcmp(temp_lexeme, "DEC") == 0 ||
                     strcmp(temp_lexeme, "PUSH") == 0 || strcmp(temp_lexeme, "POP") == 0 ||
                     strcmp(temp_lexeme, "MUL") == 0 || strcmp(temp_lexeme, "DIV") == 0) {
                
                printf("Pass 2: Traduciendo %s en LC = 0x%04X\n", temp_lexeme, current_LC);
                int local_op = 0;
                if (current_token.type == TOKEN_REGISTER) local_op = TOKEN_REGISTER;
                advance();
                current_LC += compute_instruction_size(temp_lexeme, local_op, 0);
            }
            // Manejo en Pass 2 de Saltos y Control de flujo
            else if (strcmp(temp_lexeme, "JMP") == 0 || strcmp(temp_lexeme, "JE") == 0 ||
                     strcmp(temp_lexeme, "JNE") == 0 || strcmp(temp_lexeme, "JG") == 0 ||
                     strcmp(temp_lexeme, "JL") == 0 || strcmp(temp_lexeme, "CALL") == 0) {
                
                printf("Pass 2: Procesando Salto %s en LC = 0x%04X\n", temp_lexeme, current_LC);
                if (current_token.type == TOKEN_IDENTIFIER) {
                    SymTabEntry *sym = get_symbol(current_token.lexeme);
                    if (sym != NULL && sym->defined) {
                        int instr_size = compute_instruction_size(temp_lexeme, TOKEN_IDENTIFIER, 0);
                        int offset = sym->address - (current_LC + instr_size); 
                        printf("Pass 2: Fixup en '%s' hacia '%s'. Offset: 0x%02X\n", temp_lexeme, sym->name, (unsigned char)offset);
                        fprintf(output_file, "0x%04X: [E9_REL32_TO_%s]\n", current_LC, sym->name);
                    } else {
                        printf("Error: Etiqueta '%s' no resuelta en el salto.\n", current_token.lexeme);
                        fclose(output_file);
                        exit(1);
                    }
                    advance();
                }
                current_LC += compute_instruction_size(temp_lexeme, TOKEN_IDENTIFIER, 0);
            }
        } else {
            advance(); 
        }
    }
    fclose(output_file);
    printf("\nPass 2 terminada. Soporte multi-instruccion generado con éxito.\n");
}