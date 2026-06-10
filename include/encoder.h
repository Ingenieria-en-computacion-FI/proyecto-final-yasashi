/* ============================================================
 * encoder.h — Interfaz pública del módulo Encoder IA-32
 * Integrante 4: Encoder IA-32
 *
 * Dependencias de entrada:  ia32_types.h
 * Consumidores:             backend.c (Integrante 3)
 * ============================================================ */

#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>
#include "ia32_types.h"

/* ----------------------------------------------------------
 * Buffer de salida para una instrucción codificada.
 * Máximo teórico por instrucción en el subconjunto: 11 bytes
 * (1 opcode + 1 ModRM + 1 SIB + 4 disp + 4 imm)
 * ---------------------------------------------------------- */
#define MAX_ENCODED_BYTES 15

typedef struct {
    uint8_t  bytes[MAX_ENCODED_BYTES]; /* Stream de bytes generado    */
    int      length;                   /* Número de bytes válidos     */
    int      error;                    /* 0 = OK, != 0 = código error */
    char     error_msg[128];           /* Descripción del error       */
} EncodedInstruction;

/* =============================================================
 * BLOQUE 1 — Primitivas de construcción de bytes de control
 * Usadas internamente y expuestas para pruebas unitarias.
 * ============================================================= */

/**
 * Construye el byte ModRM.
 * @param mod  Campo mod  (2 bits): modo de direccionamiento
 * @param reg  Campo reg  (3 bits): registro o dígito de opcode
 * @param rm   Campo r/m  (3 bits): registro base o modo SIB
 * @return     Byte ModRM ensamblado
 */
uint8_t encoder_build_modrm(uint8_t mod, uint8_t reg, uint8_t rm);

/**
 * Construye el byte SIB (Scale-Index-Base).
 * @param scale Factor de escala (2 bits): 0=×1, 1=×2, 2=×4, 3=×8
 * @param index Registro índice  (3 bits): ESP(4) = sin índice
 * @param base  Registro base    (3 bits)
 * @return      Byte SIB ensamblado
 */
uint8_t encoder_build_sib(uint8_t scale, uint8_t index, uint8_t base);

/**
 * Escribe un valor de 32 bits en little-endian sobre un buffer.
 * @param buf    Buffer destino (debe tener al menos 4 bytes disponibles)
 * @param value  Valor a escribir
 */
void    encoder_write_le32(uint8_t *buf, uint32_t value);

/**
 * Escribe un valor de 8 bits (desplazamiento corto) sobre un buffer.
 * @param buf    Buffer destino
 * @param value  Valor a escribir (se trunca a 8 bits)
 */
void    encoder_write_le8(uint8_t *buf, uint8_t value);

/* =============================================================
 * BLOQUE 2 — Resolución del modo de direccionamiento a bytes
 * Toma un Operand de tipo memoria y emite ModRM+SIB+Disp.
 * ============================================================= */

/**
 * Codifica los bytes ModRM, SIB y Displacement para un operando
 * de tipo memoria según el modo de direccionamiento detectado.
 *
 * @param out      Buffer de salida (se escribe a partir de out->bytes[out->length])
 * @param mem_op   Operando de memoria ya clasificado por el Parser
 * @param reg_field Valor del campo 'reg' del ModRM (registro opuesto o /digit)
 * @return         Número de bytes emitidos, -1 si error
 */
int encoder_encode_mem_operand(EncodedInstruction *out,
                               const Operand      *mem_op,
                               uint8_t             reg_field);

/* =============================================================
 * BLOQUE 3 — Encoders por familia de instrucción
 * Una función por patrón de codificación, no por mnemónico.
 * ============================================================= */

/**
 * Codifica instrucciones MOV en todas sus variantes:
 *   MOV r32, imm32  →  B8+rd  id
 *   MOV r/m32, r32  →  89 /r
 *   MOV r32, r/m32  →  8B /r
 *   MOV r/m32, imm32→  C7 /0  id
 */
EncodedInstruction encoder_encode_mov(const Instruction *instr);

/**
 * Codifica instrucciones aritméticas con formato uniforme:
 * ADD, SUB, AND, OR, XOR, CMP
 * (comparten estructura opcode: variante reg/mem + imm)
 */
EncodedInstruction encoder_encode_alu(const Instruction *instr);

/**
 * Codifica instrucciones de un solo operando:
 * INC, DEC, NEG, NOT, MUL, DIV, PUSH, POP
 */
EncodedInstruction encoder_encode_unary(const Instruction *instr);

/**
 * Codifica instrucciones de salto condicional e incondicional:
 * JMP, JE, JNE, JG, JL, JGE, JLE
 * El desplazamiento se calcula relativo a la instrucción siguiente.
 * Si el símbolo aún no está resuelto, se emite un placeholder y
 * se marca needs_reloc en el operando.
 */
EncodedInstruction encoder_encode_jump(const Instruction *instr);

/**
 * Codifica CALL (near, relativo a 32 bits).
 */
EncodedInstruction encoder_encode_call(const Instruction *instr);

/**
 * Codifica LEA r32, m  →  8D /r
 */
EncodedInstruction encoder_encode_lea(const Instruction *instr);

/**
 * Codifica instrucciones sin operandos o con operando fijo:
 * RET, NOP, INT n
 */
EncodedInstruction encoder_encode_misc(const Instruction *instr);

/* =============================================================
 * BLOQUE 4 — Punto de entrada unificado
 * El Backend (Integrante 3) llama solo a esta función.
 * ============================================================= */

/**
 * Dispatcher principal. Recibe cualquier instrucción del subconjunto
 * IA-32 soportado y delega al encoder específico correspondiente.
 *
 * @param instr  Instrucción completamente descrita por Parser+Backend
 * @return       Bytes codificados y metadatos de error/relocalización
 */
EncodedInstruction encoder_encode(const Instruction *instr);

/* =============================================================
 * BLOQUE 5 — Utilidades de diagnóstico
 * ============================================================= */

/**
 * Imprime el stream de bytes de una instrucción codificada
 * en formato hexadecimal. Útil para pruebas y bitácora.
 */
void encoder_dump(const EncodedInstruction *enc);

/**
 * Valida que una instrucción codificada sea coherente con
 * las restricciones del subconjunto IA-32 soportado.
 * @return 0 si válida, código de error si no.
 */
int  encoder_validate(const EncodedInstruction *enc,
                      const Instruction        *original);

#endif /* ENCODER_H */