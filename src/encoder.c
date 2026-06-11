/*
 * encoder.c  —  Módulo Encoder IA-32 Corregido para Pipeline Unificado
 * Estándar: C99/C11
 */

#include <string.h>
#include <stdio.h>
#include "encoder.h"

/* ================================================================
 * HELPERS INTERNOS — Macros de error y emisión
 * ================================================================ */

#define ENCODE_ERROR(enc, msg)              \
    do {                                    \
        (enc).error = 1;                    \
        (enc).length = 0;                   \
        strncpy((enc).error_msg, (msg),     \
                sizeof((enc).error_msg)-1); \
    } while (0)

#define EMIT(enc, byte) \
    ((enc).bytes[(enc).length++] = (uint8_t)(byte))

/* ================================================================
 * BLOQUE 1 — Primitivas de control ModRM y SIB
 * ================================================================ */

uint8_t encoder_build_modrm(uint8_t mod, uint8_t reg, uint8_t rm)
{
    return (uint8_t)(((mod & 0x3) << 6) |
                     ((reg & 0x7) << 3) |
                      (rm  & 0x7));
}

uint8_t encoder_build_sib(uint8_t scale, uint8_t index, uint8_t base)
{
    return (uint8_t)(((scale & 0x3) << 6) |
                     ((index & 0x7) << 3) |
                      (base  & 0x7));
}

void encoder_write_le32(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)( value        & 0xFF);
    buf[1] = (uint8_t)((value >>  8) & 0xFF);
    buf[2] = (uint8_t)((value >> 16) & 0xFF);
    buf[3] = (uint8_t)((value >> 24) & 0xFF);
}

/* ================================================================
 * BLOQUE 2 — Codificador de operandos de memoria nativo de ia32_types
 * ================================================================ */
int encoder_encode_mem_operand(EncodedInstruction *out,
                               const Operand      *mem_op,
                               uint8_t             reg_field)
{
    int before = out->length;

    switch (mem_op->type) {
        
        /* Caso 1: Dirección directa pura, ej: MOV EAX, [1000] o [var1] */
        case OP_MEM_DIRECT: {
            EMIT(*out, encoder_build_modrm(0x0, reg_field, 0x5));
            encoder_write_le32(&out->bytes[out->length], (uint32_t)mem_op->disp);
            out->length += 4;
            break;
        }

        /* Caso 2: Base sola, ej: [EBX] */
        case OP_MEM_BASE: {
            uint8_t base_rm = (uint8_t)mem_op->reg;
            if (base_rm == REG_ESP) {
                EMIT(*out, encoder_build_modrm(0x0, reg_field, 0x4));
                EMIT(*out, encoder_build_sib(0x0, 0x4, (uint8_t)REG_ESP));
            } else if (base_rm == REG_EBP) {
                EMIT(*out, encoder_build_modrm(0x1, reg_field, (uint8_t)REG_EBP));
                EMIT(*out, 0x00);
            } else {
                EMIT(*out, encoder_build_modrm(0x0, reg_field, base_rm));
            }
            break;
        }

        /* Caso 3: Base + Desplazamiento, ej: [EBP + 4] o [ESP - 8] */
        case OP_MEM_BASE_DISP: {
            uint8_t base_rm = (uint8_t)mem_op->reg;
            int32_t d = mem_op->disp;
            uint8_t mod = (d >= -128 && d <= 127) ? 0x1 : 0x2;

            if (base_rm == REG_ESP) {
                EMIT(*out, encoder_build_modrm(mod, reg_field, 0x4));
                EMIT(*out, encoder_build_sib(0x0, 0x4, (uint8_t)REG_ESP));
            } else {
                EMIT(*out, encoder_build_modrm(mod, reg_field, base_rm));
            }

            if (mod == 0x1) {
                EMIT(*out, (uint8_t)(d & 0xFF));
            } else {
                encoder_write_le32(&out->bytes[out->length], (uint32_t)d);
                out->length += 4;
            }
            break;
        }

        /* Caso 4: Base + Índice (Escala tácita 1), ej: [EBX + ECX] */
        case OP_MEM_BASE_IDX: {
            EMIT(*out, encoder_build_modrm(0x0, reg_field, 0x4));
            uint8_t sib = encoder_build_sib(0x0, (uint8_t)mem_op->index_reg, (uint8_t)mem_op->reg);
            EMIT(*out, sib);
            break;
        }

        /* Caso 5: SIB Complejo completo con escala e inmediato, ej: [EBX + ECX*4 + 8] */
        case OP_MEM_SIB: {
            int32_t d = mem_op->disp;
            uint8_t mod;

            if (d == 0 && mem_op->reg != REG_EBP) {
                mod = 0x0;
            } else if (d >= -128 && d <= 127) {
                mod = 0x1;
            } else {
                mod = 0x2;
            }

            EMIT(*out, encoder_build_modrm(mod, reg_field, 0x4));
            uint8_t sib = encoder_build_sib((uint8_t)mem_op->scale, 
                                            (uint8_t)mem_op->index_reg, 
                                            (uint8_t)mem_op->reg);
            EMIT(*out, sib);

            if (mod == 0x1) {
                EMIT(*out, (uint8_t)(d & 0xFF));
            } else if (mod == 0x2) {
                encoder_write_le32(&out->bytes[out->length], (uint32_t)d);
                out->length += 4;
            }
            break;
        }

        default:
            return -1;
    }

    return out->length - before;
}

/* ================================================================
 * BLOQUE 3 — Familia MOV
 * ================================================================ */
EncodedInstruction encoder_encode_mov(const Instruction *instr)
{
    EncodedInstruction enc;
    memset(&enc, 0, sizeof(enc));

    const Operand *dst = &instr->dst;
    const Operand *src = &instr->src;

    if (!dst || !src) {
        ENCODE_ERROR(enc, "MOV: operandos nulos");
        return enc;
    }

    /* MOV r32, imm32 (Soporta OP_LABEL para referencias a variables) */
    if (dst->type == OP_REG && (src->type == OP_IMM || src->type == OP_LABEL)) {
        if (dst->reg == REG_NONE) {
            ENCODE_ERROR(enc, "MOV r32,imm32: registro destino inválido");
            return enc;
        }
        EMIT(enc, 0xB8 + (uint8_t)dst->reg);
        encoder_write_le32(&enc.bytes[enc.length], (uint32_t)src->imm);
        enc.length += 4;
        return enc;
    }

    /* MOV r/m32, r32 */
    if (src->type == OP_REG && (dst->type == OP_REG || 
        (dst->type >= OP_MEM_DIRECT && dst->type <= OP_MEM_SIB))) {
        
        if (src->reg == REG_NONE) {
            ENCODE_ERROR(enc, "MOV r/m32,r32: registro fuente inválido");
            return enc;
        }
        EMIT(enc, 0x89);

        if (dst->type == OP_REG) {
            EMIT(enc, encoder_build_modrm(0x3, (uint8_t)src->reg, (uint8_t)dst->reg));
        } else {
            int written = encoder_encode_mem_operand(&enc, dst, (uint8_t)src->reg);
            if (written < 0) {
                ENCODE_ERROR(enc, "MOV r/m32,r32: modo de memoria inválido");
                return enc;
            }
        }
        return enc;
    }

    /* MOV r32, r/m32 */
    if (dst->type == OP_REG && (src->type >= OP_MEM_DIRECT && src->type <= OP_MEM_SIB)) {
        if (dst->reg == REG_NONE) {
            ENCODE_ERROR(enc, "MOV r32,r/m32: registro destino inválido");
            return enc;
        }
        EMIT(enc, 0x8B);

        int written = encoder_encode_mem_operand(&enc, src, (uint8_t)dst->reg);
        if (written < 0) {
            ENCODE_ERROR(enc, "MOV r32,r/m32: modo de memoria inválido");
            return enc;
        }
        return enc;
    }

    /* MOV r/m32, imm32 (Soporta OP_LABEL) */
    if ((src->type == OP_IMM || src->type == OP_LABEL) && (dst->type == OP_REG || 
        (dst->type >= OP_MEM_DIRECT && dst->type <= OP_MEM_SIB))) {
        
        EMIT(enc, 0xC7);

        if (dst->type == OP_REG) {
            if (dst->reg == REG_NONE) {
                ENCODE_ERROR(enc, "MOV r/m32,imm32: registro destino inválido");
                return enc;
            }
            EMIT(enc, encoder_build_modrm(0x3, 0x0, (uint8_t)dst->reg));
        } else {
            int written = encoder_encode_mem_operand(&enc, dst, 0x0);
            if (written < 0) {
                ENCODE_ERROR(enc, "MOV r/m32,imm32: modo de memoria inválido");
                return enc;
            }
        }

        encoder_write_le32(&enc.bytes[enc.length], (uint32_t)src->imm);
        enc.length += 4;
        return enc;
    }

    ENCODE_ERROR(enc, "MOV: combinación de operandos no soportada");
    return enc;
}

/* ================================================================
 * BLOQUE 4 — Familia ALU (ADD, SUB, AND, OR, XOR, CMP)
 * ================================================================ */
EncodedInstruction encoder_encode_alu(const Instruction *instr)
{
    EncodedInstruction enc;
    memset(&enc, 0, sizeof(enc));

    const Operand *dst = &instr->dst;
    const Operand *src = &instr->src;

    if (!dst || !src) {
        ENCODE_ERROR(enc, "ALU: operandos nulos");
        return enc;
    }

    uint8_t opcode_mr, opcode_rm, slash;

    switch (instr->mnemonic) {
        case MN_ADD: opcode_mr = 0x01; opcode_rm = 0x03; slash = 0; break;
        case MN_OR:  opcode_mr = 0x09; opcode_rm = 0x0B; slash = 1; break;
        case MN_AND: opcode_mr = 0x21; opcode_rm = 0x23; slash = 4; break;
        case MN_SUB: opcode_mr = 0x29; opcode_rm = 0x2B; slash = 5; break;
        case MN_XOR: opcode_mr = 0x31; opcode_rm = 0x33; slash = 6; break;
        case MN_CMP: opcode_mr = 0x39; opcode_rm = 0x3B; slash = 7; break;
        default:
            ENCODE_ERROR(enc, "ALU: mnemónico no soportado");
            return enc;
    }

    /* FORMA 3: r/m32, imm32 (Soporta OP_LABEL para variables) */
    if (src->type == OP_IMM || src->type == OP_LABEL) {
        EMIT(enc, 0x81);

        if (dst->type == OP_REG) {
            if (dst->reg == REG_NONE) {
                ENCODE_ERROR(enc, "ALU imm: registro destino inválido");
                return enc;
            }
            EMIT(enc, encoder_build_modrm(0x3, slash, (uint8_t)dst->reg));
        } else {
            int written = encoder_encode_mem_operand(&enc, dst, slash);
            if (written < 0) {
                ENCODE_ERROR(enc, "ALU imm: modo de memoria inválido");
                return enc;
            }
        }

        encoder_write_le32(&enc.bytes[enc.length], (uint32_t)src->imm);
        enc.length += 4;
        return enc;
    }

    /* FORMA 1: r/m32, r32 (Destino memoria, Fuente registro) */
    if (src->type == OP_REG && (dst->type >= OP_MEM_DIRECT && dst->type <= OP_MEM_SIB)) {
        if (src->reg == REG_NONE) {
            ENCODE_ERROR(enc, "ALU r/m,r: registro fuente inválido");
            return enc;
        }
        EMIT(enc, opcode_mr);

        int written = encoder_encode_mem_operand(&enc, dst, (uint8_t)src->reg);
        if (written < 0) {
            ENCODE_ERROR(enc, "ALU r/m,r: memoria destino inválida");
            return enc;
        }
        return enc;
    }

    /* FORMA 2: r32, r/m32 (Destino registro) */
    if (dst->type == OP_REG && (src->type == OP_REG || 
        (src->type >= OP_MEM_DIRECT && src->type <= OP_MEM_SIB))) {
        
        if (dst->reg == REG_NONE) {
            ENCODE_ERROR(enc, "ALU r,r/m: registro destino inválido");
            return enc;
        }
        EMIT(enc, opcode_rm);

        if (src->type == OP_REG) {
            if (src->reg == REG_NONE) {
                ENCODE_ERROR(enc, "ALU r,r: registro fuente inválido");
                return enc;
            }
            EMIT(enc, encoder_build_modrm(0x3, (uint8_t)dst->reg, (uint8_t)src->reg));
        } else {
            int written = encoder_encode_mem_operand(&enc, src, (uint8_t)dst->reg);
            if (written < 0) {
                ENCODE_ERROR(enc, "ALU r,r/m: memoria fuente inválida");
                return enc;
            }
        }
        return enc;
    }

    ENCODE_ERROR(enc, "ALU: combinación de operandos no soportada");
    return enc;
}

/* ================================================================
 * BLOQUE 5 — Familia Unaria (PUSH, POP, INC, DEC, NOT, NEG, MUL, DIV)
 * ================================================================ */
EncodedInstruction encoder_encode_unary(const Instruction *instr)
{
    EncodedInstruction enc;
    memset(&enc, 0, sizeof(enc));

    const Operand *op = &instr->dst;

    if (!op || op->type == OP_NONE) {
        ENCODE_ERROR(enc, "UNARY: requiere un operando en dst");
        return enc;
    }

    /* PUSH y POP cortos */
    if (instr->mnemonic == MN_PUSH || instr->mnemonic == MN_POP) {
        if (op->type != OP_REG || op->reg == REG_NONE) {
            ENCODE_ERROR(enc, "PUSH/POP: solo se soporta registro directo");
            return enc;
        }
        uint8_t base = (instr->mnemonic == MN_PUSH) ? 0x50 : 0x58;
        EMIT(enc, base + (uint8_t)op->reg);
        return enc;
    }

    /* INC y DEC */
    if (instr->mnemonic == MN_INC || instr->mnemonic == MN_DEC) {
        if (op->type == OP_REG) {
            if (op->reg == REG_NONE) {
                ENCODE_ERROR(enc, "INC/DEC: registro inválido");
                return enc;
            }
            uint8_t base = (instr->mnemonic == MN_INC) ? 0x40 : 0x48;
            EMIT(enc, base + (uint8_t)op->reg);
            return enc;
        }

        uint8_t slash = (instr->mnemonic == MN_INC) ? 0 : 1;
        EMIT(enc, 0xFF);
        int written = encoder_encode_mem_operand(&enc, op, slash);
        if (written < 0) {
            ENCODE_ERROR(enc, "INC/DEC: memoria inválida");
            return enc;
        }
        return enc;
    }

    /* NOT, NEG, MUL, DIV */
    {
        uint8_t slash;
        switch (instr->mnemonic) {
            case MN_NOT: slash = 2; break;
            case MN_NEG: slash = 3; break;
            case MN_MUL: slash = 4; break;
            case MN_DIV: slash = 6; break;
            default:
                ENCODE_ERROR(enc, "UNARY: mnemónico no soportado");
                return enc;
        }

        EMIT(enc, 0xF7);
        if (op->type == OP_REG) {
            EMIT(enc, encoder_build_modrm(0x3, slash, (uint8_t)op->reg));
        } else {
            int written = encoder_encode_mem_operand(&enc, op, slash);
            if (written < 0) {
                ENCODE_ERROR(enc, "UNARY 0xF7: memoria inválida");
                return enc;
            }
        }
        return enc;
    }
}

/* ================================================================
 * BLOQUE 6 — Saltos y Llamadas Robustos (Soporta Labels e Inmediatos)
 * ================================================================ */
EncodedInstruction encoder_encode_jump(const Instruction *instr)
{
    EncodedInstruction enc;
    memset(&enc, 0, sizeof(enc));

    const Operand *op = &instr->dst;

    if (!op || op->type == OP_NONE) {
        ENCODE_ERROR(enc, "JMP/Jcc: requiere operando de destino");
        return enc;
    }

    uint8_t use_0f = 0, opcode2 = 0;
    switch (instr->mnemonic) {
        case MN_JMP: use_0f = 0; opcode2 = 0xE9; break;
        case MN_JE:  use_0f = 1; opcode2 = 0x84; break;
        case MN_JNE: use_0f = 1; opcode2 = 0x85; break;
        case MN_JL:  use_0f = 1; opcode2 = 0x8C; break;
        case MN_JLE: use_0f = 1; opcode2 = 0x8E; break;
        case MN_JG:  use_0f = 1; opcode2 = 0x8F; break;
        case MN_JGE: use_0f = 1; opcode2 = 0x8D; break;
        default:
            ENCODE_ERROR(enc, "JMP/Jcc: mnemónico no soportado");
            return enc;
    }

    uint32_t instr_size = use_0f ? 6 : 5;

    if (use_0f) {
        EMIT(enc, 0x0F);
    }
    EMIT(enc, opcode2);

    uint32_t relative_offset;
    
    if (op->type == OP_LABEL || op->needs_reloc || strlen(op->label) > 0) {
        uint32_t target_abs = (uint32_t)op->imm;
        uint32_t next_eip = instr->address + instr_size;
        relative_offset = target_abs - next_eip;
    } else {
        relative_offset = (uint32_t)op->imm;
    }

    encoder_write_le32(&enc.bytes[enc.length], relative_offset);
    enc.length += 4;

    return enc;
}

EncodedInstruction encoder_encode_call(const Instruction *instr)
{
    EncodedInstruction enc;
    memset(&enc, 0, sizeof(enc));

    const Operand *op = &instr->dst;

    if (!op || op->type == OP_NONE) {
        ENCODE_ERROR(enc, "CALL: requiere operando destino en dst");
        return enc;
    }

    EMIT(enc, 0xE8);

    uint32_t instr_size = 5;
    uint32_t relative_offset;

    if (op->type == OP_LABEL || op->needs_reloc || strlen(op->label) > 0) {
        uint32_t target_abs = (uint32_t)op->imm;
        uint32_t next_eip = instr->address + instr_size;
        relative_offset = target_abs - next_eip;
    } else {
        relative_offset = (uint32_t)op->imm;
    }

    encoder_write_le32(&enc.bytes[enc.length], relative_offset);
    enc.length += 4;

    return enc;
}

/* LEA completamente implementada para soporte de Modo SIB/Memoria a Registro */
EncodedInstruction encoder_encode_lea(const Instruction *instr) {
    EncodedInstruction enc;
    memset(&enc, 0, sizeof(enc));
    
    const Operand *dst = &instr->dst;
    const Operand *src = &instr->src;

    if (dst->type != OP_REG || src->type < OP_MEM_DIRECT || src->type > OP_MEM_SIB) {
        ENCODE_ERROR(enc, "LEA: combinación de operandos no soportada (Requiere Destino Reg, Origen Memoria)");
        return enc;
    }
    
    EMIT(enc, 0x8D); // Opcode base de LEA
    
    int written = encoder_encode_mem_operand(&enc, src, (uint8_t)dst->reg);
    if (written < 0) {
        ENCODE_ERROR(enc, "LEA: expresión de memoria origen inválida");
        return enc;
    }
    
    return enc;
}

void encoder_dump(const EncodedInstruction *enc)
{
    if (!enc) return;
    if (enc->error) {
        fprintf(stderr, "[ENCODER ERROR] %s\n", enc->error_msg);
        return;
    }
    printf("[ENCODER] %d byte(s): ", enc->length);
    for (int i = 0; i < enc->length; i++) {
        printf("%02X ", enc->bytes[i]);
    }
    printf("\n");
}

/* ================================================================
 * DESPACHADOR PRINCIPAL (Punto de entrada de la Pasada 2)
 * ================================================================ */
EncodedInstruction encoder_encode(const Instruction *instr)
{
    if (!instr) {
        EncodedInstruction enc;
        memset(&enc, 0, sizeof(enc));
        ENCODE_ERROR(enc, "DISPATCHER: Instrucción nula");
        return enc;
    }

    switch (instr->mnemonic) {
        case MN_MOV:  return encoder_encode_mov(instr);
        case MN_ADD:
        case MN_SUB:
        case MN_AND:
        case MN_OR:
        case MN_XOR:
        case MN_CMP:  return encoder_encode_alu(instr);
        case MN_INC:
        case MN_DEC:
        case MN_NOT:
        case MN_NEG:
        case MN_MUL:
        case MN_DIV:
        case MN_PUSH:
        case MN_POP:  return encoder_encode_unary(instr);
        case MN_JMP:
        case MN_JE:
        case MN_JNE:
        case MN_JL:
        case MN_JLE:
        case MN_JG:
        case MN_JGE:  return encoder_encode_jump(instr);
        case MN_CALL: return encoder_encode_call(instr);
        case MN_LEA:  return encoder_encode_lea(instr);
        
        /* Instrucciones sin operandos (1 byte) */
        case MN_NOP: {
            EncodedInstruction enc; memset(&enc, 0, sizeof(enc));
            EMIT(enc, 0x90); 
            return enc;
        }
        case MN_RET: {
            EncodedInstruction enc; memset(&enc, 0, sizeof(enc));
            EMIT(enc, 0xC3); 
            return enc;
        }
        
        /* Interrupción por software (2 bytes) */
        case MN_INT: {
            EncodedInstruction enc; memset(&enc, 0, sizeof(enc));
            if (instr->dst.type == OP_IMM || instr->dst.type == OP_LABEL) {
                EMIT(enc, 0xCD);
                EMIT(enc, instr->dst.imm & 0xFF);
            } else {
                ENCODE_ERROR(enc, "INT requiere un operando inmediato");
            }
            return enc;
        }
        
        default: {
            EncodedInstruction enc;
            memset(&enc, 0, sizeof(enc));
            ENCODE_ERROR(enc, "DISPATCHER: Mnemónico no soportado");
            return enc;
        }
    }
}