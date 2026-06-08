[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/YOaAtGl8)
[![Open in Visual Studio Code](https://classroom.github.com/assets/open-in-vscode-2e0aaae1b6195c2367325f4f02e2d04e9abb55f0b24a779b69b11b9e10269abc.svg)](https://classroom.github.com/online_ide?assignment_repo_id=24101818&assignment_repo_type=AssignmentRepo)
# IA-32 Assembler & Mini Linker

## Proyecto integrador para la implementación de:

- Ensamblador de 1 pasada.
- Ensamblador de 2 pasadas.
- Generador de archivos objeto.
- Mini linker.
- Soporte parcial IA-32.
- Soporte para ModRM y SIB.

---

## Objetivo

El objetivo principal es comprender internamente:

- Cómo funciona un ensamblador.
- Cómo se genera código máquina.
- Cómo funcionan los archivos objeto.
- Cómo funcionan las relocaciones.
- Cómo funciona un linker.
- Cómo se construyen ejecutables.

---
## Características del Proyecto

### Ensamblador de 1 pasada

Debe:

- Leer el archivo una sola vez.
- Generar código máquina parcial.
- Manejar referencias adelantadas mediante fixups.
- Mantener tabla de símbolos.

---
### Ensamblador de 2 pasadas
Pass 1

Debe:

- Construir tabla de símbolos.
- Calcular offsets.
- Determinar tamaños.
- Detectar redefiniciones.

Pass 2

Debe:

- Generar código máquina definitivo.
- Resolver referencias.
- Generar archivo objeto.


## Arquitectura Objetivo

### Registros soportados

```asm
EAX
EBX
ECX
EDX
ESI
EDI
EBP
ESP
```

### Instrucciones mínimas

Transferencia

```asm
MOV
PUSH
POP
LEA
```

Aritméticas
```asm
ADD
SUB
INC
DEC
CMP
NEG
MUL
DIV
IMUL
IDIV
```
Lógicas
```asm
AND
OR
XOR
NOT
```

Saltos y control
```asm
JMP
JE
JNE
JG
JL
JGE
JLE
CALL
RET
NOP
INT
```

Directivas mínimas
```asm
SECTION
GLOBAL
EXTERN
DB
DW
DD
RESB
RESW
RESD
ORG
EQU
```

## Modos de direccionamiento

Inmediato
```asm
MOV EAX, 10
```
Registro a registro
```asm
MOV EAX, EBX
```
Memoria directa
```asm
MOV EAX, [1000]
```
Base + desplazamiento
```asm
MOV EAX, [EBP+4]
```
Base + índice
```asm
MOV EAX, [EBX+ECX]
```
Base + índice escalado
```asm
MOV EAX, [EBX+ECX*4]
```
Base + índice escalado + desplazamiento
```asm
MOV EAX, [EBX+ECX*4+8]
```

## Soporte obligatorio SIB

El proyecto debe implementar soporte parcial para el byte SIB (Scale Index Base).

Escalas mínimas soportadas
```text
1
2
4
8
```
Ejemplo SIB
```asm
MOV EAX, [EBX+ECX*4+8]
```

Debe generar internamente:

```text
Opcode
ModRM
SIB
Displacement
Immediate
```
# Restricciones
No permitido
- yacc
- bison
- flex
- ensambladores externos
- linkers externos
- bibliotecas automáticas de parsing

Todo debe implementarse manualmente.

# Lenguaje obligatorio
## Parte principal

Debe implementarse en:
```text
C
```

Permitido en Python

Python únicamente puede utilizarse para:

- Scripts de prueba.
- Comparadores.
- Herramientas auxiliares.
- Visualización hexadecimal.
- Automatización.

# Organización del Equipo

El proyecto podrá desarrollarse en equipos de hasta 5 integrantes.

## Integrante 1 — Lexer y Tokens

Responsable de:

- Lectura de archivos ASM.
- Tokenización.
- Reconocimiento de registros.
- Reconocimiento de números.
- Manejo de comentarios.
- Validación léxica.

## Integrante 2 — Parser y Representación Interna

Responsable de:

- Parsing de instrucciones.
- Parsing de operandos.
- Parsing de directivas.
- Validación sintáctica.
- Representación interna de instrucciones.
- Modos de direccionamiento.

## Integrante 3 — Backend del Ensamblador

Responsable de:

- Tabla de símbolos.
- Pass 1.
- Pass 2.
- Fixups.
- Referencias adelantadas.
- Manejo de offsets.

## Integrante 4 — Encoder IA-32

Responsable de:

- Opcodes.
- ModRM.
- SIB.
- Displacement.
- Immediate.
- Generación de bytes.


## Integrante 5 — Object Format y Linker

Responsable de:

- Archivo objeto.
- Relocaciones.
- Linker.
- Resolución de símbolos.
- Binario final.
- Responsabilidad Compartida

## Todo el equipo debe:

- Comprender el flujo completo.
- Participar en pruebas.
- Participar en integración.
- Mantener modularidad.
- Mantener consistencia.
- Flujo General

# ASM
```text
 ↓
Lexer
 ↓
Parser
 ↓
Tabla de símbolos
 ↓
Encoder
 ↓
Archivo objeto
 ↓
Linker
 ↓
Binario final
```
# Estructura del Repositorio
```text
ia32-assembler-linker/
│
├── include/
├── src/
├── tests/
├── docs/
├── scripts/
├── examples/
└── Makefile
```

# Sugerencias de Código Inicial
## TokenType
```C
typedef enum {
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_REGISTER,
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_PLUS,
    TOKEN_STAR,
    TOKEN_NEWLINE,
    TOKEN_EOF
} TokenType;
```
# Token
```C
typedef struct {
    TokenType type;
    char lexeme[64];
} Token;
```

## Construcción ModRM
```C
unsigned char build_modrm(
    unsigned char mod,
    unsigned char reg,
    unsigned char rm
) {
    return (mod << 6) | (reg << 3) | rm;
}
```

## Construcción SIB
```C
unsigned char build_sib(
    unsigned char scale,
    unsigned char index,
    unsigned char base
) {
    return (scale << 6) | (index << 3) | base;
}
```

## Tabla de símbolos sugerida
```C
typedef struct {
    char name[64];
    int address;
    int defined;
} Symbol;
```

## Ejemplo de codificación MOV
```C
void encode_mov_reg_imm(int reg, int imm) {


    unsigned char opcode = 0xB8 + reg;


    printf("Opcode: %02X\n", opcode);
    printf("Immediate: %d\n", imm);
}
```

# Casos de prueba mínimos
- MOV inmediato.
- Saltos.
- CALL.
- EXTERN.
- Referencias adelantadas.
- Relocaciones.
- Múltiples módulos.
- SIB.


# Bitácora IA

Todos los equipos deberán documentar:

- Qué prompts utilizaron.
- Qué herramientas IA utilizaron.
- Qué código fue generado automáticamente.
- Qué modificaciones realizaron.
- Qué errores encontraron.


# Entregables

- Código fuente.
- Repositorio Git.
- Reporte técnico.
- Bitácora IA.
- Casos de prueba.
- Makefile.

# Recomendación Importante

No intenten implementar todo al mismo tiempo.

Orden sugerido:

- Lexer.
- Tokens.
- Parser.
- Tabla de símbolos.
- Pass 1.
- Pass 2.
- Encoder.
- ModRM.
- SIB.
- Archivo objeto.
- Relocaciones.
- Linker.

# Filosofía del Proyecto

La prioridad principal será:

- Correctitud.
- Modularidad.
- Comprensión.
- Arquitectura limpia.

Antes que implementar características extremadamente complejas.

Proyecto base para:
- Ensamblador de 1 pasada
- Ensamblador de 2 pasadas
- Formato objeto
- Mini linker
- Soporte ModRM y SIB

## Compilar

```bash
make
```

## Ejecutar

```bash
./assembler examples/hello.asm
```
