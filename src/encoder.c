/*
 * encoder.c  —  Módulo Encoder IA-32
 * Integrante 4: Encoder IA-32
 *
 * Estándar: C99/C11
 * Dependencias: encoder.h, ia32_types.h
 *
 * PASO 1 — Primitivas base + familia MOV completa.
 */

#include <string.h>
#include <stdio.h>
#include "encoder.h"

/* ================================================================
 * HELPERS INTERNOS — Macros de error
 * ================================================================ */

/*
 * Escribe un mensaje de error en el struct de salida y marca
 * length = 0 para que el Backend detecte el fallo sin ambigüedad.
 */
#define ENCODE_ERROR(enc, msg)              \
    do {                                    \
        (enc).error = 1;                    \
        (enc).length = 0;                   \
        strncpy((enc).error_msg, (msg),     \
                sizeof((enc).error_msg)-1); \
    } while (0)

/*
 * Escribe un byte en el buffer de salida e incrementa length.
 * No hace bounds-check explícito: MAX_ENCODED_BYTES (15) cubre
 * cualquier instrucción del subconjunto soportado.
 */
#define EMIT(enc, byte) \
    ((enc).bytes[(enc).length++] = (uint8_t)(byte))


/* ================================================================
 * BLOQUE 1 — Primitivas de construcción de bytes de control
 * ================================================================ */

/*
 * encoder_build_modrm
 *
 * Construye el byte ModRM a partir de sus tres campos.
 *
 *  7   6 | 5   4   3 | 2   1   0
 * [ mod  |    reg    |    r/m   ]
 *
 * mod (2 bits): modo de acceso
 *   11 → registro directo
 *   10 → memoria + disp32
 *   01 → memoria + disp8
 *   00 → memoria sin desplazamiento
 *
 * reg (3 bits): registro fuente/destino o dígito /n del opcode
 * r/m (3 bits): registro base, o 100 para activar SIB
 */
uint8_t encoder_build_modrm(uint8_t mod, uint8_t reg, uint8_t rm)
{
    return (uint8_t)(((mod & 0x3) << 6) |
                     ((reg & 0x7) << 3) |
                      (rm  & 0x7));
}

/*
 * encoder_build_sib
 *
 * Construye el byte SIB a partir de sus tres campos.
 *
 *  7   6 | 5   4   3 | 2   1   0
 * [scale |   index   |   base  ]
 *
 * scale (2 bits): factor de escala del índice
 *   00 → ×1 | 01 → ×2 | 10 → ×4 | 11 → ×8
 *
 * index (3 bits): registro índice; 100 (ESP) = sin índice
 * base  (3 bits): registro base;  101 (EBP) con mod=00 = sin base + disp32
 */
uint8_t encoder_build_sib(uint8_t scale, uint8_t index, uint8_t base)
{
    return (uint8_t)(((scale & 0x3) << 6) |
                     ((index & 0x7) << 3) |
                      (base  & 0x7));
}

/*
 * encoder_write_le32
 *
 * Serializa un valor de 32 bits en formato little-endian
 * sobre los 4 bytes consecutivos a partir de buf.
 *
 * IA-32 es nativo little-endian, así que todos los immediates
 * y displacements de 32 bits siguen este orden de bytes.
 *
 * Ejemplo: 0x00001000 → buf[0]=0x00, [1]=0x10, [2]=0x00, [3]=0x00
 */
void encoder_write_le32(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)( value        & 0xFF);
    buf[1] = (uint8_t)((value >>  8) & 0xFF);
    buf[2] = (uint8_t)((value >> 16) & 0xFF);
    buf[3] = (uint8_t)((value >> 24) & 0xFF);
}

/*
 * encoder_write_le8
 *
 * Escribe un desplazamiento de 8 bits (sign-extended en IA-32).
 * Usado cuando mod=01 en ModRM.
 */
void encoder_write_le8(uint8_t *buf, uint8_t value)
{
    buf[0] = value;
}


/* ================================================================
 * BLOQUE 2 — Codificador de operandos de memoria
 *
 * Función interna: emite ModRM + SIB + Displacement para
 * cualquier operando de tipo memoria del subconjunto soportado.
 *
 * reg_field: valor que irá en el campo 'reg' del byte ModRM.
 *   En instrucciones de dos operandos, es el número del registro
 *   opuesto (src o dst según el opcode).
 *   En instrucciones con /digit (ej. MOV r/m,imm → /0), es ese dígito.
 *
 * Retorna el número de bytes emitidos, o -1 si hay error.
 * ================================================================ */

int encoder_encode_mem_operand(EncodedInstruction *out,
                               const Operand      *mem_op,
                               uint8_t             reg_field)
{
    int before = out->length;  /* Para calcular bytes emitidos al final */

    switch (mem_op->type) {

    /* ----------------------------------------------------------
     * MOD=00, R/M=101 → Dirección absoluta de 32 bits
     * Ejemplo: MOV EAX, [1000]
     * Codifica: ModRM(00, reg, 101) + disp32
     * ---------------------------------------------------------- */
    case OP_MEM_DIRECT:
        EMIT(*out, encoder_build_modrm(0x0, reg_field, 0x5));
        encoder_write_le32(&out->bytes[out->length], (uint32_t)mem_op->disp);
        out->length += 4;
        break;

    /* ----------------------------------------------------------
     * Base sin desplazamiento: [EBX], [ECX], etc.
     * Casos especiales:
     *   base=ESP(4) → mod=00, r/m=100, requiere SIB
     *                 SIB(00, 100, base): índice=ESP=sin índice
     *   base=EBP(5) → mod=00 + r/m=101 significa disp32 absoluto,
     *                 por ello se fuerza a mod=01 con disp8=0x00
     * ---------------------------------------------------------- */
    case OP_MEM_BASE:
        if (mem_op->reg == REG_ESP) {
            /* [ESP] requiere SIB obligatorio */
            EMIT(*out, encoder_build_modrm(0x0, reg_field, 0x4));
            EMIT(*out, encoder_build_sib(SCALE_1, 0x4, (uint8_t)REG_ESP));
        } else if (mem_op->reg == REG_EBP) {
            /* [EBP] se codifica como [EBP+0] para evitar ambigüedad */
            EMIT(*out, encoder_build_modrm(0x1, reg_field, (uint8_t)REG_EBP));
            EMIT(*out, 0x00);  /* disp8 = 0 */
        } else {
            EMIT(*out, encoder_build_modrm(0x0, reg_field, (uint8_t)mem_op->reg));
        }
        break;

    /* ----------------------------------------------------------
     * Base + desplazamiento: [EBP+4], [EBX-8], etc.
     * Si el desplazamiento cabe en 8 bits con signo → mod=01 + disp8
     * Si no                                         → mod=10 + disp32
     *
     * Caso especial: base=ESP siempre requiere SIB.
     * ---------------------------------------------------------- */
    case OP_MEM_BASE_DISP: {
        int32_t d = mem_op->disp;
        uint8_t mod = (d >= -128 && d <= 127) ? 0x1 : 0x2;

        if (mem_op->reg == REG_ESP) {
            EMIT(*out, encoder_build_modrm(mod, reg_field, 0x4));
            EMIT(*out, encoder_build_sib(SCALE_1, 0x4, (uint8_t)REG_ESP));
        } else {
            EMIT(*out, encoder_build_modrm(mod, reg_field, (uint8_t)mem_op->reg));
        }

        if (mod == 0x1) {
            EMIT(*out, (uint8_t)(d & 0xFF));
        } else {
            encoder_write_le32(&out->bytes[out->length], (uint32_t)d);
            out->length += 4;
        }
        break;
    }

    /* ----------------------------------------------------------
     * Base + índice sin desplazamiento: [EBX+ECX]
     * Siempre requiere SIB. Scale=1 (SCALE_1=00).
     * Si base=EBP con mod=00 → problema: lo forzamos a mod=01+disp8=0
     * ---------------------------------------------------------- */
    case OP_MEM_BASE_IDX: {
        uint8_t mod = (mem_op->reg == REG_EBP) ? 0x1 : 0x0;
        EMIT(*out, encoder_build_modrm(mod, reg_field, 0x4));
        EMIT(*out, encoder_build_sib(SCALE_1,
                                     (uint8_t)mem_op->index_reg,
                                     (uint8_t)mem_op->reg));
        if (mod == 0x1) {
            EMIT(*out, 0x00);  /* disp8 = 0 */
        }
        break;
    }

    /* ----------------------------------------------------------
     * SIB completo: [EBX + ECX*4 + 8]
     * El campo 'scale' ya viene como SibScale (0-3) desde el Parser.
     * Si hay desplazamiento:
     *   cabe en 8 bits con signo → mod=01 + disp8
     *   no cabe                  → mod=10 + disp32
     * Si no hay desplazamiento   → mod=00
     *   (salvo base=EBP, que fuerza mod=01+0)
     * ---------------------------------------------------------- */
    case OP_MEM_SIB: {
        int32_t d   = mem_op->disp;
        uint8_t mod;

        if (d == 0 && mem_op->reg != REG_EBP) {
            mod = 0x0;
        } else if (d >= -128 && d <= 127) {
            mod = 0x1;
        } else {
            mod = 0x2;
        }

        EMIT(*out, encoder_build_modrm(mod, reg_field, 0x4));
        EMIT(*out, encoder_build_sib((uint8_t)mem_op->scale,
                                     (uint8_t)mem_op->index_reg,
                                     (uint8_t)mem_op->reg));
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
 * BLOQUE 3 — encoder_encode_mov
 *
 * Cubre las cuatro variantes del subconjunto IA-32:
 *
 *  A) MOV r32, imm32    →  (0xB8 + rd)  id
 *  B) MOV r/m32, r32    →  0x89  /r
 *  C) MOV r32, r/m32    →  0x8B  /r
 *  D) MOV r/m32, imm32  →  0xC7  /0     id
 *
 * Selección de variante:
 *  dst=REG, src=IMM                → variante A
 *  dst=REG, src=REG                → variante B (reg→reg es caso especial de r/m)
 *  dst=REG, src=MEM*               → variante C
 *  dst=MEM*, src=REG               → variante B
 *  dst=MEM*, src=IMM               → variante D
 *  dst=REG, src=REG (también B)    → mod=11 en ModRM
 * ================================================================ */

EncodedInstruction encoder_encode_mov(const Instruction *instr)
{
    EncodedInstruction enc;
    memset(&enc, 0, sizeof(enc));

    const Operand *dst = &instr->dst;
    const Operand *src = &instr->src;

    /* ── Guardia de seguridad ──────────────────────────────────── */
    if (!dst || !src) {
        ENCODE_ERROR(enc, "MOV: operandos nulos");
        return enc;
    }

    /* ── Variante A: MOV r32, imm32 ────────────────────────────
     *
     *  Bytes: [ 0xB8 + reg_id ][ imm32 LE ]
     *
     *  Esta forma es la más compacta para cargar un inmediato en
     *  un registro: no necesita ModRM, el registro va codificado
     *  directamente en los 3 bits bajos del opcode.
     * ─────────────────────────────────────────────────────────── */
    if (dst->type == OP_REG && src->type == OP_IMM) {
        if (dst->reg == REG_NONE) {
            ENCODE_ERROR(enc, "MOV r32,imm32: registro destino inválido");
            return enc;
        }
        EMIT(enc, 0xB8 + (uint8_t)dst->reg);
        encoder_write_le32(&enc.bytes[enc.length], (uint32_t)src->imm);
        enc.length += 4;
        return enc;
    }

    /* ── Variante B: MOV r/m32, r32 ────────────────────────────
     *
     *  Opcode: 0x89
     *  ModRM:  campo 'reg' = registro fuente (src)
     *          campo 'r/m' = registro/memoria destino (dst)
     *
     *  Registro a registro: mod=11
     *  Registro a memoria:  mod según tipo de direccionamiento
     * ─────────────────────────────────────────────────────────── */
    if (src->type == OP_REG &&
        (dst->type == OP_REG || dst->type >= OP_MEM_DIRECT)) {

        if (src->reg == REG_NONE) {
            ENCODE_ERROR(enc, "MOV r/m32,r32: registro fuente inválido");
            return enc;
        }
        EMIT(enc, 0x89);

        if (dst->type == OP_REG) {
            /* mod=11: ambos operandos son registros */
            EMIT(enc, encoder_build_modrm(0x3,
                                          (uint8_t)src->reg,
                                          (uint8_t)dst->reg));
        } else {
            /* Destino en memoria: delegar al codificador de modos */
            int written = encoder_encode_mem_operand(&enc, dst,
                                                     (uint8_t)src->reg);
            if (written < 0) {
                ENCODE_ERROR(enc, "MOV r/m32,r32: modo de memoria inválido");
                return enc;
            }
        }
        return enc;
    }

    /* ── Variante C: MOV r32, r/m32 ────────────────────────────
     *
     *  Opcode: 0x8B
     *  ModRM:  campo 'reg' = registro destino (dst)
     *          campo 'r/m' = fuente (registro o memoria)
     *
     *  Simétrica a la variante B pero con opcode distinto.
     * ─────────────────────────────────────────────────────────── */
    if (dst->type == OP_REG &&
        (src->type == OP_MEM_DIRECT  ||
         src->type == OP_MEM_BASE    ||
         src->type == OP_MEM_BASE_DISP ||
         src->type == OP_MEM_BASE_IDX  ||
         src->type == OP_MEM_SIB)) {

        if (dst->reg == REG_NONE) {
            ENCODE_ERROR(enc, "MOV r32,r/m32: registro destino inválido");
            return enc;
        }
        EMIT(enc, 0x8B);

        int written = encoder_encode_mem_operand(&enc, src,
                                                 (uint8_t)dst->reg);
        if (written < 0) {
            ENCODE_ERROR(enc, "MOV r32,r/m32: modo de memoria inválido");
            return enc;
        }
        return enc;
    }

    /* ── Variante D: MOV r/m32, imm32 ──────────────────────────
     *
     *  Opcode: 0xC7
     *  ModRM:  campo 'reg' = /0 (dígito de opcode, no un registro)
     *          campo 'r/m' = destino (registro o memoria)
     *
     *  El immediate de 4 bytes se emite AL FINAL, después del
     *  ModRM/SIB/Displacement. Orden crítico.
     * ─────────────────────────────────────────────────────────── */
    if (src->type == OP_IMM &&
        (dst->type == OP_REG       ||
         dst->type == OP_MEM_DIRECT ||
         dst->type == OP_MEM_BASE    ||
         dst->type == OP_MEM_BASE_DISP ||
         dst->type == OP_MEM_BASE_IDX  ||
         dst->type == OP_MEM_SIB)) {

        EMIT(enc, 0xC7);

        if (dst->type == OP_REG) {
            /* mod=11, /0, registro destino */
            if (dst->reg == REG_NONE) {
                ENCODE_ERROR(enc, "MOV r/m32,imm32: registro destino inválido");
                return enc;
            }
            EMIT(enc, encoder_build_modrm(0x3, 0x0, (uint8_t)dst->reg));
        } else {
            /* Destino en memoria: /digit = 0 */
            int written = encoder_encode_mem_operand(&enc, dst, 0x0);
            if (written < 0) {
                ENCODE_ERROR(enc, "MOV r/m32,imm32: modo de memoria inválido");
                return enc;
            }
        }

        /* Immediate siempre al final, en little-endian */
        encoder_write_le32(&enc.bytes[enc.length], (uint32_t)src->imm);
        enc.length += 4;
        return enc;
    }

    /* ── Combinación de operandos no soportada ─────────────────── */
    ENCODE_ERROR(enc, "MOV: combinacion de operandos no soportada");
    return enc;
}


/* ================================================================
 * BLOQUE 5 — Utilidades de diagnóstico (implementación parcial)
 * ================================================================ */

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
 * encoder_encode_alu
 *
 * Codifica las instrucciones ALU de dos operandos del subconjunto
 * IA-32: ADD, SUB, AND, OR, XOR, CMP.
 *
 * Todas comparten la misma estructura de opcode en tres formas:
 *
 *   FORMA 1 — r/m32, r32   (fuente es registro)
 *     Opcode: OP_MR  (ej. ADD=0x01, SUB=0x29, ...)
 *     ModRM:  reg=src.reg,  r/m=dst
 *
 *   FORMA 2 — r32, r/m32   (destino es registro, fuente reg/mem)
 *     Opcode: OP_RM  (ej. ADD=0x03, SUB=0x2B, ...)
 *     ModRM:  reg=dst.reg,  r/m=src
 *
 *   FORMA 3 — r/m32, imm32  (fuente es inmediato)
 *     Opcode: 0x81  (común a todas las ALU)
 *     ModRM:  reg=/digit,   r/m=dst
 *     Immediate: imm32 LE al final
 *
 * Selección de forma:
 *   dst=REG,  src=REG  → FORMA 2 (convención: usar OP_RM)
 *   dst=MEM,  src=REG  → FORMA 1
 *   dst=REG,  src=MEM  → FORMA 2
 *   dst=REG o MEM, src=IMM → FORMA 3
 *
 * Tabla de opcodes por mnemónico:
 *
 *  Mnemónico | OP_MR (r/m,r) | OP_RM (r,r/m) | /digit (imm)
 *  ----------|---------------|---------------|-------------
 *  ADD       |   0x01        |   0x03        |   /0
 *  OR        |   0x09        |   0x0B        |   /1
 *  AND       |   0x21        |   0x23        |   /4
 *  SUB       |   0x29        |   0x2B        |   /5
 *  XOR       |   0x31        |   0x33        |   /6
 *  CMP       |   0x39        |   0x3B        |   /7
 *
 * ================================================================ */

EncodedInstruction encoder_encode_alu(const Instruction *instr)
{
    EncodedInstruction enc;
    memset(&enc, 0, sizeof(enc));

    const Operand *dst = &instr->dst;
    const Operand *src = &instr->src;

    /* ── Guardia de seguridad ──────────────────────────────────── */
    if (!dst || !src) {
        ENCODE_ERROR(enc, "ALU: operandos nulos");
        return enc;
    }

    /* ── Tabla de dispatch por mnemónico ───────────────────────
     *
     * Tres valores por instrucción:
     *   opcode_mr  → forma r/m32, r32  (destino puede ser mem)
     *   opcode_rm  → forma r32, r/m32  (destino es registro)
     *   slash      → dígito /n para forma inmediata (opcode 0x81)
     * ─────────────────────────────────────────────────────────── */
    uint8_t opcode_mr, opcode_rm, slash;

    switch (instr->mnemonic) {
        case MN_ADD: opcode_mr = 0x01; opcode_rm = 0x03; slash = 0; break;
        case MN_OR:  opcode_mr = 0x09; opcode_rm = 0x0B; slash = 1; break;
        case MN_AND: opcode_mr = 0x21; opcode_rm = 0x23; slash = 4; break;
        case MN_SUB: opcode_mr = 0x29; opcode_rm = 0x2B; slash = 5; break;
        case MN_XOR: opcode_mr = 0x31; opcode_rm = 0x33; slash = 6; break;
        case MN_CMP: opcode_mr = 0x39; opcode_rm = 0x3B; slash = 7; break;
        default:
            ENCODE_ERROR(enc, "ALU: mnemónico no soportado en encoder_encode_alu");
            return enc;
    }

    /* ── FORMA 3: r/m32, imm32  (opcode 0x81 + /digit) ─────────
     *
     *  Bytes: [ 0x81 ][ ModRM ][ SIB? ][ Disp? ][ imm32 LE ]
     *
     *  El campo 'reg' del ModRM NO es un registro fuente aquí:
     *  es el dígito /n que extiende el opcode para distinguir
     *  ADD/OR/AND/SUB/XOR/CMP dentro del mismo opcode 0x81.
     *
     *  Esto lo checkamos primero porque src=IMM es exclusivo;
     *  no puede coexistir con las formas registro/memoria.
     * ─────────────────────────────────────────────────────────── */
    if (src->type == OP_IMM) {
        EMIT(enc, 0x81);

        if (dst->type == OP_REG) {
            /* mod=11, /digit, registro destino */
            if (dst->reg == REG_NONE) {
                ENCODE_ERROR(enc, "ALU imm: registro destino inválido");
                return enc;
            }
            EMIT(enc, encoder_build_modrm(0x3, slash, (uint8_t)dst->reg));

        } else {
            /* Destino en memoria: ModRM+SIB+Disp vía helper */
            int written = encoder_encode_mem_operand(&enc, dst, slash);
            if (written < 0) {
                ENCODE_ERROR(enc, "ALU imm: modo de memoria destino inválido");
                return enc;
            }
        }

        /* Immediate siempre al final, en little-endian */
        encoder_write_le32(&enc.bytes[enc.length], (uint32_t)src->imm);
        enc.length += 4;
        return enc;
    }

    /* ── FORMA 1: r/m32, r32  (destino memoria, fuente registro)
     *
     *  Opcode: opcode_mr
     *  ModRM:  reg = src.reg  (registro fuente)
     *          r/m = dst      (memoria o registro destino)
     *
     *  Bytes: [ opcode_mr ][ ModRM ][ SIB? ][ Disp? ]
     * ─────────────────────────────────────────────────────────── */
    if (src->type == OP_REG &&
        (dst->type == OP_MEM_DIRECT    ||
         dst->type == OP_MEM_BASE      ||
         dst->type == OP_MEM_BASE_DISP ||
         dst->type == OP_MEM_BASE_IDX  ||
         dst->type == OP_MEM_SIB)) {

        if (src->reg == REG_NONE) {
            ENCODE_ERROR(enc, "ALU r/m,r: registro fuente inválido");
            return enc;
        }
        EMIT(enc, opcode_mr);

        int written = encoder_encode_mem_operand(&enc, dst,
                                                 (uint8_t)src->reg);
        if (written < 0) {
            ENCODE_ERROR(enc, "ALU r/m,r: modo de memoria destino inválido");
            return enc;
        }
        return enc;
    }

    /* ── FORMA 2: r32, r/m32  (destino registro, fuente reg/mem)
     *
     *  Opcode: opcode_rm
     *  ModRM:  reg = dst.reg  (registro destino)
     *          r/m = src      (memoria o registro fuente)
     *
     *  Cubre:
     *    • reg ← reg   (src=OP_REG,      mod=11)
     *    • reg ← mem   (src=OP_MEM_*,    mod según direccionamiento)
     *
     *  Bytes: [ opcode_rm ][ ModRM ][ SIB? ][ Disp? ]
     * ─────────────────────────────────────────────────────────── */
    if (dst->type == OP_REG &&
        (src->type == OP_REG           ||
         src->type == OP_MEM_DIRECT    ||
         src->type == OP_MEM_BASE      ||
         src->type == OP_MEM_BASE_DISP ||
         src->type == OP_MEM_BASE_IDX  ||
         src->type == OP_MEM_SIB)) {

        if (dst->reg == REG_NONE) {
            ENCODE_ERROR(enc, "ALU r,r/m: registro destino inválido");
            return enc;
        }
        EMIT(enc, opcode_rm);

        if (src->type == OP_REG) {
            /* Registro a registro: mod=11, reg=dst, r/m=src */
            if (src->reg == REG_NONE) {
                ENCODE_ERROR(enc, "ALU r,r: registro fuente inválido");
                return enc;
            }
            EMIT(enc, encoder_build_modrm(0x3,
                                          (uint8_t)dst->reg,
                                          (uint8_t)src->reg));
        } else {
            /* Fuente en memoria: campo reg del ModRM = registro destino */
            int written = encoder_encode_mem_operand(&enc, src,
                                                     (uint8_t)dst->reg);
            if (written < 0) {
                ENCODE_ERROR(enc, "ALU r,r/m: modo de memoria fuente inválido");
                return enc;
            }
        }
        return enc;
    }

    /* ── Combinación no soportada ───────────────────────────────── */
    ENCODE_ERROR(enc, "ALU: combinación de operandos no soportada");
    return enc;
}
/* ================================================================
 * encoder_encode_unary
 *
 * Codifica instrucciones de un solo operando del subconjunto IA-32:
 * PUSH, POP, INC, DEC, NOT, NEG, MUL, DIV.
 *
 * El operando siempre llega en instr->dst.
 * instr->src.type == OP_NONE en todos los casos.
 *
 * Tres familias de codificación:
 *
 *   FAMILIA A — Opcode corto: 0xNN + reg_id  (sin ModRM)
 *     PUSH r32 → 0x50 + rd
 *     POP  r32 → 0x58 + rd
 *     INC  r32 → 0x40 + rd
 *     DEC  r32 → 0x48 + rd
 *
 *     Solo válida cuando dst=OP_REG. Un byte total.
 *     Esta forma es preferida sobre la forma larga cuando el
 *     operando es un registro: genera código más compacto y es
 *     lo que producen los ensambladores reales (NASM, GAS).
 *
 *   FAMILIA B — Opcode 0xF7 + ModRM /digit  (NOT, NEG, MUL, DIV)
 *     Comparten opcode; el campo 'reg' del ModRM distingue cuál:
 *
 *       Mnemónico | /digit
 *       ----------|-------
 *       NOT       |  /2
 *       NEG       |  /3
 *       MUL       |  /4
 *       DIV       |  /6
 *
 *     Si dst=REG  → mod=11, /digit, reg_id   (sin acceso a memoria)
 *     Si dst=MEM* → delegar a encoder_encode_mem_operand
 *
 * ================================================================ */

EncodedInstruction encoder_encode_unary(const Instruction *instr)
{
    EncodedInstruction enc;
    memset(&enc, 0, sizeof(enc));

    const Operand *dst = &instr->dst;

    /* ── Guardia de seguridad ──────────────────────────────────── */
    if (!dst) {
        ENCODE_ERROR(enc, "UNARY: operando nulo");
        return enc;
    }
    if (dst->type == OP_NONE) {
        ENCODE_ERROR(enc, "UNARY: instrucción requiere exactamente un operando");
        return enc;
    }

    /* ── FAMILIA A: PUSH / POP ──────────────────────────────────
     *
     * Forma corta de registro: un único byte, sin ModRM.
     * El identificador del registro se suma al opcode base.
     *
     *   PUSH EAX → 0x50   PUSH EBX → 0x53   PUSH ESP → 0x54 ...
     *   POP  EAX → 0x58   POP  EBX → 0x5B   POP  ESP → 0x5C ...
     *
     * Nota: PUSH/POP con operando de memoria (FF /6, 8F /0) queda
     * fuera del subconjunto inicial; se rechaza con error claro
     * para que el Parser no envíe esa forma silenciosamente.
     * ─────────────────────────────────────────────────────────── */
    if (instr->mnemonic == MN_PUSH || instr->mnemonic == MN_POP) {
        if (dst->type != OP_REG) {
            ENCODE_ERROR(enc, "PUSH/POP: solo se soporta operando de registro "
                              "en esta versión del encoder");
            return enc;
        }
        if (dst->reg == REG_NONE) {
            ENCODE_ERROR(enc, "PUSH/POP: registro destino inválido");
            return enc;
        }

        uint8_t base = (instr->mnemonic == MN_PUSH) ? 0x50 : 0x58;
        EMIT(enc, base + (uint8_t)dst->reg);
        return enc;
    }

    /* ── FAMILIA A: INC / DEC ───────────────────────────────────
     *
     * Forma corta de registro: un único byte, sin ModRM.
     *
     *   INC EAX → 0x40   INC ECX → 0x41   INC ESP → 0x44 ...
     *   DEC EAX → 0x48   DEC ECX → 0x49   DEC ESP → 0x4C ...
     *
     * Si el operando es memoria, caemos a FAMILIA B con el opcode
     * 0xFF (/0 para INC, /1 para DEC), que es la forma larga.
     * Aquí solo gestionamos la forma corta de registro.
     * ─────────────────────────────────────────────────────────── */
    if (instr->mnemonic == MN_INC || instr->mnemonic == MN_DEC) {
        if (dst->type == OP_REG) {
            if (dst->reg == REG_NONE) {
                ENCODE_ERROR(enc, "INC/DEC: registro destino inválido");
                return enc;
            }
            uint8_t base = (instr->mnemonic == MN_INC) ? 0x40 : 0x48;
            EMIT(enc, base + (uint8_t)dst->reg);
            return enc;
        }

        /* Operando de memoria → forma larga: 0xFF + ModRM /digit
         *   INC r/m32 → 0xFF /0
         *   DEC r/m32 → 0xFF /1                                  */
        uint8_t slash = (instr->mnemonic == MN_INC) ? 0 : 1;
        EMIT(enc, 0xFF);

        int written = encoder_encode_mem_operand(&enc, dst, slash);
        if (written < 0) {
            ENCODE_ERROR(enc, "INC/DEC: modo de memoria inválido");
            return enc;
        }
        return enc;
    }

    /* ── FAMILIA B: NOT / NEG / MUL / DIV ──────────────────────
     *
     * Todas comparten opcode 0xF7. El campo 'reg' del ModRM
     * actúa como extensión del opcode (/digit) para distinguirlas.
     *
     *   NOT r/m32 → 0xF7 /2
     *   NEG r/m32 → 0xF7 /3
     *   MUL r/m32 → 0xF7 /4   (multiplica EDX:EAX = EAX × r/m)
     *   DIV r/m32 → 0xF7 /6   (divide EDX:EAX entre r/m)
     *
     * Si dst=REG  → mod=11, /digit, reg_id   (1 byte ModRM)
     * Si dst=MEM* → encoder_encode_mem_operand hace ModRM+SIB+Disp
     * ─────────────────────────────────────────────────────────── */
    {
        uint8_t slash;

        switch (instr->mnemonic) {
            case MN_NOT: slash = 2; break;
            case MN_NEG: slash = 3; break;
            case MN_MUL: slash = 4; break;
            case MN_DIV: slash = 6; break;
            default:
                ENCODE_ERROR(enc,
                    "UNARY: mnemónico no soportado en encoder_encode_unary");
                return enc;
        }

        EMIT(enc, 0xF7);

        if (dst->type == OP_REG) {
            if (dst->reg == REG_NONE) {
                ENCODE_ERROR(enc, "UNARY 0xF7: registro destino inválido");
                return enc;
            }
            /* mod=11: operando es registro directo, no memoria */
            EMIT(enc, encoder_build_modrm(0x3, slash, (uint8_t)dst->reg));
        } else {
            int written = encoder_encode_mem_operand(&enc, dst, slash);
            if (written < 0) {
                ENCODE_ERROR(enc, "UNARY 0xF7: modo de memoria inválido");
                return enc;
            }
        }
        return enc;
    }
}
/* ================================================================
 * encoder_encode_jump
 *
 * Codifica instrucciones de salto near de 32 bits:
 * JMP, JE, JNE, JG, JGE, JL, JLE.
 *
 * Formato near de 32 bits:
 *
 *   JMP incondicional:
 *     [ 0xE9 ][ disp32 LE ]                     — 5 bytes totales
 *
 *   Saltos condicionales:
 *     [ 0x0F ][ 0x8X ][ disp32 LE ]             — 6 bytes totales
 *
 * Cálculo del desplazamiento relativo:
 *
 *   El CPU calcula el destino sumando el desplazamiento al valor
 *   de EIP *después* de haber leído la instrucción completa, es
 *   decir, EIP ya apunta a la instrucción siguiente.
 *
 *   Por tanto:
 *     disp32 = dir_destino - (instr->address + tamaño_instrucción)
 *
 *   Tamaños:
 *     JMP incondicional : 5 bytes  (1 opcode  + 4 disp)
 *     Salto condicional : 6 bytes  (2 opcodes + 4 disp)
 *
 * Placeholder de relocación:
 *
 *   Si dst.needs_reloc == 1  (símbolo externo o adelantado)
 *   o  dst.type == OP_LABEL  (el Parser no pudo resolver la dir.)
 *   → se emiten 4 bytes 0x00 como placeholder.
 *     El Backend (Integrante 3) completará el valor en la segunda
 *     pasada al resolver la tabla de símbolos.
 *
 * ================================================================ */

EncodedInstruction encoder_encode_jump(const Instruction *instr)
{
    EncodedInstruction enc;
    memset(&enc, 0, sizeof(enc));

    const Operand *dst = &instr->dst;

    /* ── Guardia de seguridad ──────────────────────────────────── */
    if (!dst) {
        ENCODE_ERROR(enc, "JMP/Jcc: operando nulo");
        return enc;
    }
    if (dst->type == OP_NONE) {
        ENCODE_ERROR(enc, "JMP/Jcc: requiere exactamente un operando destino");
        return enc;
    }

    /* ── Tabla de opcodes por mnemónico ─────────────────────────
     *
     * JMP usa un opcode de 1 byte (0xE9).
     * Los saltos condicionales usan un escape de 2 bytes (0x0F 0x8X).
     * Representamos ambos casos con:
     *   use_0f   : 1 si necesita el byte de escape 0x0F
     *   opcode2  : segundo byte (o único byte para JMP)
     * ─────────────────────────────────────────────────────────── */
    uint8_t use_0f  = 0;
    uint8_t opcode2 = 0;

    switch (instr->mnemonic) {
        case MN_JMP: use_0f = 0; opcode2 = 0xE9; break;
        case MN_JE:  use_0f = 1; opcode2 = 0x84; break;
        case MN_JNE: use_0f = 1; opcode2 = 0x85; break;
        case MN_JL:  use_0f = 1; opcode2 = 0x8C; break;
        case MN_JLE: use_0f = 1; opcode2 = 0x8E; break;
        case MN_JG:  use_0f = 1; opcode2 = 0x8F; break;
        case MN_JGE: use_0f = 1; opcode2 = 0x8D; break;
        default:
            ENCODE_ERROR(enc, "JMP/Jcc: mnemónico no soportado "
                              "en encoder_encode_jump");
            return enc;
    }

    /* ── Emisión del opcode ─────────────────────────────────────
     *
     * JMP:  [ 0xE9 ]
     * Jcc:  [ 0x0F ][ 0x8X ]
     *
     * El tamaño de la instrucción depende del opcode emitido:
     *   JMP  → instr_size = 5  (1 byte opcode  + 4 bytes disp)
     *   Jcc  → instr_size = 6  (2 bytes opcode + 4 bytes disp)
     * ─────────────────────────────────────────────────────────── */
    uint32_t instr_size;

    if (use_0f) {
        EMIT(enc, 0x0F);
        EMIT(enc, opcode2);
        instr_size = 6;
    } else {
        EMIT(enc, opcode2);   /* 0xE9 para JMP */
        instr_size = 5;
    }

    /* ── Resolución del desplazamiento ──────────────────────────
     *
     * CASO 1 — Símbolo no resuelto (requires relocation):
     *   Condición: dst->needs_reloc == 1  ||  dst->type == OP_LABEL
     *   Acción:    placeholder de 4 bytes en 0x00.
     *              El Backend localiza esta instrucción por su
     *              instr->address y aplica el parche en pasada 2.
     *
     * CASO 2 — Dirección ya conocida (símbolo resuelto):
     *   Condición: dst->type == OP_IMM  (dirección absoluta)
     *   Acción:    calcular disp32 relativo y serializar en LE.
     *
     *   disp32 = (uint32_t)(destino - (instr->address + instr_size))
     *
     *   El cast a int32_t antes de almacenar en uint32_t preserva
     *   el complemento a 2 para saltos hacia atrás (disp negativo).
     * ─────────────────────────────────────────────────────────── */
    if (dst->needs_reloc || dst->type == OP_LABEL) {
        /* Placeholder: el Backend escribirá el disp32 real aquí */
        encoder_write_le32(&enc.bytes[enc.length], 0x00000000);
        enc.length += 4;
    } else if (dst->type == OP_IMM) {
        uint32_t target   = (uint32_t)dst->imm;
        uint32_t next_eip = instr->address + instr_size;
        int32_t  disp     = (int32_t)(target - next_eip);

        encoder_write_le32(&enc.bytes[enc.length], (uint32_t)disp);
        enc.length += 4;
    } else {
        ENCODE_ERROR(enc, "JMP/Jcc: tipo de operando destino no soportado "
                          "(se esperaba OP_IMM o OP_LABEL)");
        return enc;
    }

    return enc;
}


/* ================================================================
 * encoder_encode_call
 *
 * Codifica CALL near relativo de 32 bits.
 *
 * Formato:
 *   [ 0xE8 ][ disp32 LE ]                       — 5 bytes totales
 *
 * Idéntico en estructura a JMP, salvo que:
 *   • El opcode es 0xE8 (no 0xE9).
 *   • El CPU empuja EIP en la pila antes de saltar (semántica,
 *     irrelevante para la codificación de bytes).
 *   • Es la instrucción más común con needs_reloc=1 en código
 *     multimodular, ya que las funciones externas declaradas con
 *     EXTERN siempre requerirán relocación en el linker.
 *
 * Cálculo del desplazamiento:
 *   disp32 = destino - (instr->address + 5)
 *
 * Placeholder de relocación:
 *   Mismas condiciones que encoder_encode_jump:
 *   dst->needs_reloc == 1  ||  dst->type == OP_LABEL
 *   → 4 bytes en 0x00, a parchar por el Integrante 5 (Linker).
 *
 * ================================================================ */

EncodedInstruction encoder_encode_call(const Instruction *instr)
{
    EncodedInstruction enc;
    memset(&enc, 0, sizeof(enc));

    const Operand *dst = &instr->dst;

    /* ── Guardia de seguridad ──────────────────────────────────── */
    if (!dst) {
        ENCODE_ERROR(enc, "CALL: operando nulo");
        return enc;
    }
    if (dst->type == OP_NONE) {
        ENCODE_ERROR(enc, "CALL: requiere exactamente un operando destino");
        return enc;
    }
    if (instr->mnemonic != MN_CALL) {
        ENCODE_ERROR(enc, "CALL: mnemónico incorrecto para encoder_encode_call");
        return enc;
    }

    /* ── Emisión del opcode ─────────────────────────────────────
     *
     * CALL near relativo: opcode fijo 0xE8.
     * Tamaño total de la instrucción: 5 bytes.
     * ─────────────────────────────────────────────────────────── */
    EMIT(enc, 0xE8);

    /* ── Resolución del desplazamiento ──────────────────────────
     *
     * CASO 1 — Símbolo externo o adelantado (needs_reloc / OP_LABEL):
     *   El Integrante 5 (Linker) o el Backend en pasada 2 aplicarán
     *   la relocación R_386_PC32 sobre estos 4 bytes.
     *   Se emite placeholder 0x00000000.
     *
     * CASO 2 — Dirección ya resuelta (OP_IMM):
     *   disp32 = (int32_t)(target - (instr->address + 5))
     *   El +5 refleja que EIP avanza al final de esta instrucción
     *   (1 byte opcode + 4 bytes disp) antes de que el CPU salte.
     * ─────────────────────────────────────────────────────────── */
    if (dst->needs_reloc || dst->type == OP_LABEL) {
        encoder_write_le32(&enc.bytes[enc.length], 0x00000000);
        enc.length += 4;
    } else if (dst->type == OP_IMM) {
        uint32_t target   = (uint32_t)dst->imm;
        uint32_t next_eip = instr->address + 5;
        int32_t  disp     = (int32_t)(target - next_eip);

        encoder_write_le32(&enc.bytes[enc.length], (uint32_t)disp);
        enc.length += 4;
    } else {
        ENCODE_ERROR(enc, "CALL: tipo de operando no soportado "
                          "(se esperaba OP_IMM o OP_LABEL)");
        return enc;
    }

    return enc;
}