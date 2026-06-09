/* ============================================================
 * ia32_types.h  — Tipos compartidos entre Parser y Encoder
 * Integrante 2 llena estas estructuras.
 * Integrante 4 las consume para generar bytes.
 * ============================================================ */

#ifndef IA32_TYPES_H
#define IA32_TYPES_H

#include <stdint.h>

/* ----------------------------------------------------------
 * Registros de 32 bits soportados.
 * El valor numérico coincide EXACTAMENTE con la codificación
 * IA-32 (campo reg/r/m en ModRM y SIB). No cambiar el orden.
 * ---------------------------------------------------------- */
typedef enum {
    REG_EAX = 0,
    REG_ECX = 1,
    REG_EDX = 2,
    REG_EBX = 3,
    REG_ESP = 4,   /* En SIB.index: indica "sin índice" */
    REG_EBP = 5,   /* En ModRM r/m con mod=00: indica disp32 puro */
    REG_ESI = 6,
    REG_EDI = 7,
    REG_NONE = -1  /* Ausencia de registro */
} Reg32;

/* ----------------------------------------------------------
 * Escalas SIB soportadas.
 * Valor numérico = bits de escala en el byte SIB.
 * ---------------------------------------------------------- */
typedef enum {
    SCALE_1 = 0,  /* 00b */
    SCALE_2 = 1,  /* 01b */
    SCALE_4 = 2,  /* 10b */
    SCALE_8 = 3   /* 11b */
} SibScale;

/* ----------------------------------------------------------
 * Tipos de operando.
 * El Parser etiqueta cada operando con uno de estos tipos.
 * ---------------------------------------------------------- */
typedef enum {
    OP_NONE = 0,
    OP_REG,          /* Registro directo: EAX, EBX, ...       */
    OP_IMM,          /* Valor inmediato: 10, 0xFF, ...         */
    OP_MEM_DIRECT,   /* Dirección directa: [1000]              */
    OP_MEM_BASE,     /* Base sola: [EBX]                       */
    OP_MEM_BASE_DISP,/* Base + desplazamiento: [EBP+4]         */
    OP_MEM_BASE_IDX, /* Base + índice: [EBX+ECX]               */
    OP_MEM_SIB,      /* Base + índice escalado + disp: [EBX+ECX*4+8] */
    OP_LABEL         /* Referencia a etiqueta (para saltos/CALL) */
} OperandType;

/* ----------------------------------------------------------
 * Representación de un operando individual.
 * ---------------------------------------------------------- */
typedef struct {
    OperandType type;

    /* Campos de registro */
    Reg32       reg;        /* Registro directo o base */
    Reg32       index_reg;  /* Registro índice (SIB)   */
    SibScale    scale;      /* Escala SIB              */

    /* Campos de valor numérico */
    int32_t     disp;       /* Desplazamiento (con signo) */
    int32_t     imm;        /* Valor inmediato            */

    /* Referencia simbólica (saltos, CALL, EXTERN) */
    char        label[64];  /* Nombre del símbolo si aplica */
    int         needs_reloc;/* 1 si requiere relocalización */
} Operand;

/* ----------------------------------------------------------
 * Mnemónicos soportados.
 * ---------------------------------------------------------- */
typedef enum {
    /* Transferencia */
    MN_MOV = 0, MN_PUSH, MN_POP, MN_LEA,
    /* Aritméticas */
    MN_ADD, MN_SUB, MN_INC, MN_DEC,
    MN_CMP, MN_NEG, MN_MUL, MN_DIV,
    /* Lógicas */
    MN_AND, MN_OR, MN_XOR, MN_NOT,
    /* Saltos */
    MN_JMP, MN_JE,  MN_JNE, MN_JG,
    MN_JL,  MN_JGE, MN_JLE,
    /* Control */
    MN_CALL, MN_RET, MN_NOP, MN_INT,
    MN_UNKNOWN
} Mnemonic;

/* ----------------------------------------------------------
 * Instrucción completa: lo que el Parser entrega al Encoder.
 * ---------------------------------------------------------- */
typedef struct {
    Mnemonic    mnemonic;
    Operand     dst;         /* Operando destino (puede ser OP_NONE) */
    Operand     src;         /* Operando fuente  (puede ser OP_NONE) */
    uint32_t    address;     /* Dirección asignada por el Backend (Integrante 3) */
} Instruction;

#endif /* IA32_TYPES_H */