#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/lexer.h"   
#include "../include/parser.h"
#include "../include/symtab.h"

Token current_token;
FILE *source_file;
int LC = 0;
FILE *output_file = NULL; // Variable global para poder escribir en Pass 2 desde las funciones unificadas

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
    // NOP es una instrucción de control de un solo byte (0x90)
    if (strcmp(opcode, "NOP") == 0) {
        return 1;
    }
    // INT de software (ej: INT 0x80) requiere 2 bytes (0xCD [int_num])
    if (strcmp(opcode, "INT") == 0) {
        return 2;
    }
    // RET es una instrucción de control de un byte (0xC3)
    if (strcmp(opcode, "RET") == 0) {
        return 1;
    }

    // --- 1. Grupo de Transferencia, Aritméticas y Lógicas de dos operandos ---
    if (strcmp(opcode, "MOV") == 0 || strcmp(opcode, "ADD") == 0 || 
        strcmp(opcode, "SUB") == 0 || strcmp(opcode, "CMP") == 0 ||
        strcmp(opcode, "AND") == 0 || strcmp(opcode, "OR") == 0  || 
        strcmp(opcode, "XOR") == 0 || strcmp(opcode, "LEA") == 0) {
        
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
             strcmp(opcode, "PUSH") == 0 || strcmp(opcode, "POP") == 0 ||
             strcmp(opcode, "NEG") == 0  || strcmp(opcode, "NOT") == 0) {
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
             strcmp(opcode, "JG") == 0   || strcmp(opcode, "JL") == 0  ||
             strcmp(opcode, "JGE") == 0  || strcmp(opcode, "JLE") == 0) {
        // Opcode extendido (2 bytes: 0x0F 0x8X) + Dirección Relativa de 32 bits (4 bytes) = 6 bytes
        return 6;
    }
    
    return 2; // Tamaño de respaldo seguro por defecto
}

// Analizador de directivas de datos para calcular cuántos bytes añaden al LC
int get_directive_size(const char *directive) {
    if (strcmp(directive, "DB") == 0 || strcmp(directive, "RESB") == 0) return 1;
    if (strcmp(directive, "DW") == 0 || strcmp(directive, "RESW") == 0) return 2;
    if (strcmp(directive, "DD") == 0 || strcmp(directive, "RESD") == 0) return 4;
    return 0;
}

// Verifica si un lexema es una directiva de asignación de datos válida
int is_data_directive(const char *lexeme) {
    return (strcmp(lexeme, "DB") == 0   || strcmp(lexeme, "DW") == 0   || strcmp(lexeme, "DD") == 0 ||
            strcmp(lexeme, "RESB") == 0 || strcmp(lexeme, "RESW") == 0 || strcmp(lexeme, "RESD") == 0 ||
            strcmp(lexeme, "EQU") == 0);
}

// Función auxiliar defensiva para convertir enteros en base 10 o base 16 (0x...) de forma segura
int parse_int_value(const char *lexeme) {
    if (lexeme[0] == '0' && (lexeme[1] == 'x' || lexeme[1] == 'X')) {
        return (int)strtol(lexeme, NULL, 16);
    }
    // Si es una 'x' huérfana mandada por error, saltarla e intentar leer base 16
    if (lexeme[0] == 'x' || lexeme[0] == 'X') {
        return (int)strtol(lexeme + 1, NULL, 16);
    }
    return atoi(lexeme);
}

// Helper para encapsular la lectura de tokens numéricos o pseudo-hexadecimales rotos del lexer
int consume_numeric_token(ParsedInstruction *inst) {
    (void)inst; // Silencia el warning de GCC: parámetro no usado intencionalmente
    char combined_hex[64] = "";
    
    // Si viene un '0' y el token que sigue empieza con 'x', ¡los pegamos defensivamente!
    if (strcmp(current_token.lexeme, "0") == 0) {
        strcpy(combined_hex, current_token.lexeme);
        advance();
        if (current_token.lexeme[0] == 'x' || current_token.lexeme[0] == 'X') {
            strcat(combined_hex, current_token.lexeme);
            advance();
            return parse_int_value(combined_hex);
        } else {
            // Era un cero legítimo entero solo
            return 0;
        }
    }
    
    // Lectura normalizada
    int val = parse_int_value(current_token.lexeme);
    advance();
    return val;
}

// Analizador SIB y expresiones de memoria (BLINDADO contra fallos del lexer en Hexadecimales)
void parse_memory_expression(ParsedInstruction *inst) {
    match(TOKEN_LBRACKET); 
    
    if (current_token.type == TOKEN_REGISTER) {
        strcpy(inst->base_reg, current_token.lexeme);
        if (!output_file) printf("%s ", current_token.lexeme);
        advance();
    } else if (current_token.type == TOKEN_NUMBER || strcmp(current_token.lexeme, "0") == 0) {
        inst->has_displacement = 1;
        inst->immediate = consume_numeric_token(inst);
        if (!output_file) printf("%d ", inst->immediate);
    } else if (current_token.type == TOKEN_IDENTIFIER) {
        // Soporte para variables globales directas como [var1]
        inst->has_displacement = 1;
        inst->immediate = 0; // Se resolverá en fase de Linkeo
        if (!output_file) printf("%s (variable) ", current_token.lexeme);
        advance();
    } else {
        printf("Error Sintactico: Se esperaba registro, numero o variable al inicio del direccionamiento. Encontrado: '%s'\n", current_token.lexeme);
        exit(1);
    }

    while (current_token.type != TOKEN_RBRACKET && current_token.type != TOKEN_EOF) {
        if (current_token.type == TOKEN_PLUS || strcmp(current_token.lexeme, "-") == 0) {
            int is_minus = (strcmp(current_token.lexeme, "-") == 0);
            if (!output_file) printf("%s ", current_token.lexeme);
            advance(); 

            if (current_token.type == TOKEN_REGISTER) {
                char temp_reg[16];
                strcpy(temp_reg, current_token.lexeme);
                if (!output_file) printf("%s ", current_token.lexeme);
                advance();

                if (current_token.type == TOKEN_STAR) {
                    if (!output_file) printf("%s ", current_token.lexeme);
                    advance(); 
                    if (current_token.type == TOKEN_NUMBER || strcmp(current_token.lexeme, "0") == 0) {
                        strcpy(inst->index_reg, temp_reg);
                        inst->scale = consume_numeric_token(inst);
                        if (!output_file) printf("%d ", inst->scale);
                    } else {
                        printf("Error Sintactico: Se esperaba una escala numerica valida tras '*'.\n");
                        exit(1);
                    }
                } else {
                    strcpy(inst->index_reg, temp_reg);
                    inst->scale = 1; 
                }
            } else if (current_token.type == TOKEN_NUMBER || strcmp(current_token.lexeme, "0") == 0) {
                inst->has_displacement = 1;
                int val = consume_numeric_token(inst);
                inst->immediate = is_minus ? -val : val;
                if (!output_file) printf("%d ", inst->immediate);
            } else if (current_token.type == TOKEN_IDENTIFIER) {
                // Soporte para variables en expresiones complejas (ej: [EBX + var1])
                inst->has_displacement = 1;
                inst->immediate = 0;
                if (!output_file) printf("%s ", current_token.lexeme);
                advance();
            }
        } else {
            printf("Error Sintactico: Token inesperado '%s' dentro de la memoria.\n", current_token.lexeme);
            exit(1);
        }
    }
    match(TOKEN_RBRACKET); 
}

// Procesador Unificado para instrucciones de dos operandos (MOV, ADD, SUB, CMP, etc.)
void process_two_operands(const char *opname, int is_pass2, int current_LC) {
    ParsedInstruction inst;
    memset(&inst, 0, sizeof(ParsedInstruction));
    strcpy(inst.opcode, opname);
    
    int op1_type = 0; 
    int op2_type = 0;

    // Operando 1: Destino
    if (current_token.type == TOKEN_REGISTER) {
        if (!is_pass2) printf("Parser: Registro destino detectado -> %s\n", current_token.lexeme);
        op1_type = TOKEN_REGISTER;
        advance();
    } else if (current_token.type == TOKEN_LBRACKET) {
        if (!is_pass2) printf("Parser: Memoria destino detectada -> [ ");
        op1_type = TOKEN_LBRACKET;
        inst.has_memory = 1;
        parse_memory_expression(&inst);
        if (!is_pass2) printf("]\n");
    }

    match(TOKEN_COMMA);

    // Operando 2: Origen
    if (current_token.type == TOKEN_REGISTER) {
        if (!is_pass2) printf("Parser: Registro origen detectado -> %s\n", current_token.lexeme);
        op2_type = TOKEN_REGISTER;
        advance();
    } else if (current_token.type == TOKEN_NUMBER || strcmp(current_token.lexeme, "0") == 0) {
        inst.immediate = consume_numeric_token(&inst);
        if (!is_pass2) printf("Parser: Valor inmediato detectado -> %d\n", inst.immediate);
        op2_type = TOKEN_NUMBER;
    } else if (current_token.type == TOKEN_IDENTIFIER) {
        if (!is_pass2) printf("Parser: Identificador (Constante/Variable) origen detectado -> %s\n", current_token.lexeme);
        op2_type = TOKEN_NUMBER; 
        advance();
    } else if (current_token.type == TOKEN_LBRACKET) {
        if (!is_pass2) printf("Parser: Memoria origen detectada -> [ ");
        op2_type = TOKEN_LBRACKET;
        inst.has_memory = 1;
        parse_memory_expression(&inst);
        if (!is_pass2) printf("]\n");
    }

    if (!is_pass2) printf("Parser: Instruccion %s analizada con exito\n", opname);
    
    int bytes_size = 0;
    if (inst.has_memory) {
        bytes_size = 1; // Opcode base (MOV o ALU r/m)
        if (strlen(inst.index_reg) > 0) bytes_size += 2; // ModRM + SIB
        else bytes_size += 1; // ModRM
        if (inst.has_displacement) {
            if (inst.immediate >= -128 && inst.immediate <= 127 && inst.immediate != 0) bytes_size += 1;
            else bytes_size += 4;
        }
        if (op2_type == TOKEN_NUMBER) bytes_size += 4; // Inmediato de 32 bits
    } else {
        bytes_size = compute_instruction_size(opname, op1_type, op2_type);
    }

    if (is_pass2) {
        printf("Pass 2: Traduciendo %s en LC = 0x%04X\n", opname, current_LC);
        fprintf(output_file, "0x%04X: [OPCODE_%s]\n", current_LC, opname);
    }

    LC += bytes_size;
}

// Procesador Unificado para instrucciones unarias y de un solo operando
void process_one_operand(const char *opname, int is_pass2, int current_LC) {
    int op_type = 0;
    if (current_token.type == TOKEN_REGISTER) {
        if (!is_pass2) printf("Parser: Registro operando detectado -> %s\n", current_token.lexeme);
        op_type = TOKEN_REGISTER;
        advance();
    } else if (current_token.type == TOKEN_NUMBER || strcmp(current_token.lexeme, "0") == 0) {
        int val = consume_numeric_token(NULL);
        if (!is_pass2) printf("Parser: Numero operando detectado -> %d\n", val);
        op_type = TOKEN_NUMBER;
    } else {
        printf("Error Sintactico: La instruccion %s requiere un operando valido (Registro/Inmediato). Encontrado: '%s'\n", opname, current_token.lexeme);
        exit(1);
    }

    if (!is_pass2) printf("Parser: Instruccion %s analizada con exito\n", opname);
    
    if (is_pass2) {
        printf("Pass 2: Traduciendo %s en LC = 0x%04X\n", opname, current_LC);
        fprintf(output_file, "0x%04X: [OPCODE_%s]\n", current_LC, opname);
    }
    LC += compute_instruction_size(opname, op_type, 0);
}

// Procesador Unificado para saltos (JMP, JE, JNE, JG, JL, CALL)
void process_branch(const char *opname, int is_pass2, int current_LC) {
    if (current_token.type == TOKEN_IDENTIFIER) {
        if (!is_pass2) {
            printf("Parser: Instruccion %s hacia etiqueta '%s' detectada\n", opname, current_token.lexeme);
            if (get_symbol(current_token.lexeme) == NULL) {
                add_symbol(current_token.lexeme, 0, 0); // Registro preliminar de la etiqueta
            }
        } else {
            printf("Pass 2: Procesando Salto %s en LC = 0x%04X\n", opname, current_LC);
            SymTabEntry *sym = get_symbol(current_token.lexeme);
            
            // CORREGIDO: Ahora permitimos que pase la validación si el símbolo es EXTERN (is_extern == 1)
            if (sym != NULL && (sym->defined || sym->is_extern)) {
                if (sym->is_extern) {
                    printf("Pass 2: Salto %s hacia simbolo EXTERN '%s'. Dejando espacio para Linker.\n", opname, sym->name);
                    fprintf(output_file, "0x%04X: [%s_TO_EXTERN_%s]\n", current_LC, opname, sym->name);
                } else {
                    int instr_size = compute_instruction_size(opname, TOKEN_IDENTIFIER, 0);
                    int offset = sym->address - (current_LC + instr_size); 
                    printf("Pass 2: Fixup en '%s' hacia '%s'. Offset: 0x%02X\n", opname, sym->name, (unsigned char)offset);
                    fprintf(output_file, "0x%04X: [E9_REL32_TO_%s]\n", current_LC, sym->name);
                }
            } else {
                printf("Error: Etiqueta '%s' no resuelta en el salto.\n", current_token.lexeme);
                if (output_file) fclose(output_file);
                exit(1);
            }
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

            // 1. Evaluar si es una Etiqueta estándar
            if (current_token.type == TOKEN_COLON) {
                printf("Pass 1: Etiqueta '%s' registrada en LC = 0x%04X\n", temp_lexeme, LC);
                add_symbol(temp_lexeme, LC, 1);
                advance(); 
                if (current_token.type == TOKEN_NEWLINE) advance();
                continue; 
            } 
            
            // 2. DETECCIÓN FLEXIBLE DE VARIABLES
            else if (is_data_directive(current_token.lexeme)) {
                char directive[16];
                strcpy(directive, current_token.lexeme);
                advance(); 
                
                int count = 1;
                if (strcmp(directive, "RESB") == 0 || strcmp(directive, "RESW") == 0 || strcmp(directive, "RESD") == 0) {
                    count = consume_numeric_token(NULL); 
                } else {
                    consume_numeric_token(NULL);
                }

                printf("Pass 1: Variable Global/Constante '%s' [%s] registrada en LC = 0x%04X.\n", temp_lexeme, directive, LC);
                add_symbol(temp_lexeme, LC, 1); 
                
                LC += (get_directive_size(directive) * count);

                while (current_token.type != TOKEN_NEWLINE && current_token.type != TOKEN_EOF) advance();
                if (current_token.type == TOKEN_NEWLINE) advance();
                continue;
            }

            // 3. Interceptación limpia de SECTION y GLOBAL
            else if (strcmp(temp_lexeme, "SECTION") == 0 || strcmp(temp_lexeme, "GLOBAL") == 0) {
                while (current_token.type != TOKEN_NEWLINE && current_token.type != TOKEN_EOF) advance();
                if (current_token.type == TOKEN_NEWLINE) advance();
                continue;
            }
            
            // 4. Interceptación de la directiva EXTERN
            else if (strcmp(temp_lexeme, "EXTERN") == 0) {
                if (current_token.type == TOKEN_IDENTIFIER) {
                    printf("Pass 1: Directiva EXTERN detectada. Registrando simbolo externo '%s'.\n", current_token.lexeme);
                    // CORREGIDO: Se llama a la nueva función especializada que gestiona símbolos externos genuinos sin darlos por definidos
                    add_extern_symbol(current_token.lexeme);
                    advance();
                } else {
                    printf("Error Sintactico: Se esperaba el nombre de un identificador despues de EXTERN.\n");
                    exit(1);
                }
                while (current_token.type != TOKEN_NEWLINE && current_token.type != TOKEN_EOF) advance();
                if (current_token.type == TOKEN_NEWLINE) advance();
                continue;
            }
            
            // 5. Enrutador de Mnemónicos Estándar
            else {
                if (strcmp(temp_lexeme, "MOV") == 0 || strcmp(temp_lexeme, "ADD") == 0 || 
                    strcmp(temp_lexeme, "SUB") == 0 || strcmp(temp_lexeme, "CMP") == 0 ||
                    strcmp(temp_lexeme, "AND") == 0 || strcmp(temp_lexeme, "OR") == 0  || 
                    strcmp(temp_lexeme, "XOR") == 0 || strcmp(temp_lexeme, "LEA") == 0) {
                    process_two_operands(temp_lexeme, 0, LC);
                } 
                else if (strcmp(temp_lexeme, "INC") == 0  || strcmp(temp_lexeme, "DEC") == 0 ||
                         strcmp(temp_lexeme, "PUSH") == 0 || strcmp(temp_lexeme, "POP") == 0 ||
                         strcmp(temp_lexeme, "MUL") == 0  || strcmp(temp_lexeme, "DIV") == 0 ||
                         strcmp(temp_lexeme, "NEG") == 0  || strcmp(temp_lexeme, "NOT") == 0 ||
                         strcmp(temp_lexeme, "INT") == 0) {
                    process_one_operand(temp_lexeme, 0, LC);
                }
                else if (strcmp(temp_lexeme, "NOP") == 0  || strcmp(temp_lexeme, "RET") == 0) {
                    printf("Parser: Instruccion de control %s analizada con exito\n", temp_lexeme);
                    LC += compute_instruction_size(temp_lexeme, 0, 0);
                }
                else if (strcmp(temp_lexeme, "JMP") == 0 || strcmp(temp_lexeme, "JE") == 0   ||
                         strcmp(temp_lexeme, "JNE") == 0 || strcmp(temp_lexeme, "JG") == 0   ||
                         strcmp(temp_lexeme, "JL") == 0  || strcmp(temp_lexeme, "CALL") == 0 ||
                         strcmp(temp_lexeme, "JGE") == 0 || strcmp(temp_lexeme, "JLE") == 0) {
                    process_branch(temp_lexeme, 0, LC);
                } 
                else {
                    printf("Error Sintactico: Instruccion '%s' no reconocida por la arquitectura.\n", temp_lexeme);
                    exit(1);
                }
                
                // Limpieza defensiva hasta la nueva línea para evitar que caracteres basura rompan el pase
                while (current_token.type != TOKEN_NEWLINE && current_token.type != TOKEN_EOF) advance();
                if (current_token.type == TOKEN_NEWLINE) advance();
            }
        } else {
            advance(); // Consumir cualquier token inesperado de forma segura
        }
    }
    printf("\nPass 1: El archivo completo ha sido analizado de forma correcta.\n");
    print_symbol_table();
}

// --- PASS 2 ---
void pass2_program(FILE *file) {
    output_file = fopen("output.hex", "w");
    if (output_file == NULL) {
        printf("Error: No se pudo crear el archivo output.hex\n");
        exit(1);
    }

    source_file = file;
    advance(); 
    LC = 0; // Garantizamos usar la misma variable LC que en el Pass 1

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
            
            else if (is_data_directive(current_token.lexeme)) {
                char directive[16];
                strcpy(directive, current_token.lexeme);
                advance();
                
                int count = 1;
                if (strcmp(directive, "RESB") == 0 || strcmp(directive, "RESW") == 0 || strcmp(directive, "RESD") == 0) {
                    count = consume_numeric_token(NULL);
                } else {
                    consume_numeric_token(NULL);
                }
                
                LC += (get_directive_size(directive) * count);
                while (current_token.type != TOKEN_NEWLINE && current_token.type != TOKEN_EOF) advance();
                if (current_token.type == TOKEN_NEWLINE) advance();
                continue;
            }
            
            else if (strcmp(temp_lexeme, "SECTION") == 0 || strcmp(temp_lexeme, "GLOBAL") == 0) {
                while (current_token.type != TOKEN_NEWLINE && current_token.type != TOKEN_EOF) advance();
                if (current_token.type == TOKEN_NEWLINE) advance();
                continue;
            }
            
            else if (strcmp(temp_lexeme, "EXTERN") == 0) {
                while (current_token.type != TOKEN_NEWLINE && current_token.type != TOKEN_EOF) advance();
                if (current_token.type == TOKEN_NEWLINE) advance();
                continue; 
            }
            
            else {
                // Ejecución Simétrica del Pase 2 utilizando las mismas funciones
                if (strcmp(temp_lexeme, "MOV") == 0 || strcmp(temp_lexeme, "ADD") == 0 || 
                    strcmp(temp_lexeme, "SUB") == 0 || strcmp(temp_lexeme, "CMP") == 0 ||
                    strcmp(temp_lexeme, "AND") == 0 || strcmp(temp_lexeme, "OR") == 0  || 
                    strcmp(temp_lexeme, "XOR") == 0 || strcmp(temp_lexeme, "LEA") == 0) {
                    process_two_operands(temp_lexeme, 1, LC);
                } 
                else if (strcmp(temp_lexeme, "INC") == 0  || strcmp(temp_lexeme, "DEC") == 0 ||
                         strcmp(temp_lexeme, "PUSH") == 0 || strcmp(temp_lexeme, "POP") == 0 ||
                         strcmp(temp_lexeme, "MUL") == 0  || strcmp(temp_lexeme, "DIV") == 0 ||
                         strcmp(temp_lexeme, "NEG") == 0  || strcmp(temp_lexeme, "NOT") == 0 ||
                         strcmp(temp_lexeme, "INT") == 0) {
                    process_one_operand(temp_lexeme, 1, LC);
                }
                else if (strcmp(temp_lexeme, "NOP") == 0 || strcmp(temp_lexeme, "RET") == 0) {
                    printf("Pass 2: Traduciendo %s en LC = 0x%04X\n", temp_lexeme, LC);
                    fprintf(output_file, "0x%04X: [OPCODE_%s]\n", LC, temp_lexeme);
                    LC += compute_instruction_size(temp_lexeme, 0, 0);
                }
                else if (strcmp(temp_lexeme, "JMP") == 0 || strcmp(temp_lexeme, "JE") == 0   ||
                         strcmp(temp_lexeme, "JNE") == 0 || strcmp(temp_lexeme, "JG") == 0   ||
                         strcmp(temp_lexeme, "JL") == 0  || strcmp(temp_lexeme, "CALL") == 0 ||
                         strcmp(temp_lexeme, "JGE") == 0 || strcmp(temp_lexeme, "JLE") == 0) {
                    process_branch(temp_lexeme, 1, LC);
                }
                
                // Limpieza de línea idéntica al Pase 1
                while (current_token.type != TOKEN_NEWLINE && current_token.type != TOKEN_EOF) advance();
                if (current_token.type == TOKEN_NEWLINE) advance();
            }
        } else {
            advance(); 
        }
    }
    fclose(output_file);
    output_file = NULL;
    printf("\nPass 2 terminada. Soporte multi-instruccion generado con éxito.\n");
}