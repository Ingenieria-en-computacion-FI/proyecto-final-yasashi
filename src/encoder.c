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