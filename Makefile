# ================================================================
#  Makefile  –  Ensamblador/Linker IA-32
#  Plataforma destino : Windows con MinGW/GCC  (funciona también
#                       en Linux/macOS sin cambios salvo EXE)
#  Uso:
#    make          → compila el ejecutable
#    make all      → ídem
#    make clean    → elimina todos los artefactos generados
#    make test     → ejecuta el programa con test.asm
# ================================================================

# ── Compilador y banderas ──────────────────────────────────────
CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=gnu99 -Iinclude
# -std=gnu99 permite funciones anidadas (GCC extension) usadas
# en main.c::run_pass2().  Cambiar a -std=c99 si se refactoriza
# run_pass2() en funciones no-anidadas.

# En modo debug: make DEBUG=1
ifdef DEBUG
  CFLAGS += -g -O0 -DDEBUG
else
  CFLAGS += -O2
endif

# ── Rutas ──────────────────────────────────────────────────────
SRC_DIR  = src
INC_DIR  = include
OBJ_DIR  = obj

# ── Nombre del ejecutable final ────────────────────────────────
# Windows usa .exe; en Linux/macOS quitar la extensión.
TARGET   = assembler.exe

# ── Lista de fuentes y objetos ─────────────────────────────────
SOURCES  = $(SRC_DIR)/main.c    \
           $(SRC_DIR)/lexer.c   \
           $(SRC_DIR)/parser.c  \
           $(SRC_DIR)/symtab.c  \
           $(SRC_DIR)/encoder.c \
           $(SRC_DIR)/object.c  \
           $(SRC_DIR)/linker.c

# Transforma src/foo.c → obj/foo.o
OBJECTS  = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

# ── Regla por defecto ──────────────────────────────────────────
.PHONY: all clean test

all: $(OBJ_DIR) $(TARGET)

# ── Enlazado final ─────────────────────────────────────────────
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^
	@echo ""
	@echo "  ✓  Ejecutable generado: $(TARGET)"
	@echo ""

# ── Compilación modular: src/*.c → obj/*.o ─────────────────────
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# ── Crear carpeta obj/ si no existe ───────────────────────────
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# ── Prueba rápida con el archivo de ejemplo ────────────────────
test: all
	./$(TARGET) test.asm output.bin
	@echo ""
	@echo "  Binario generado en output.bin"
	@echo ""

# ── Limpieza ───────────────────────────────────────────────────
clean:
	rm -f $(OBJ_DIR)/*.o $(TARGET) output.bin output.hex
	@echo "  Limpieza completada."

# ── Dependencias de cabeceras (regeneración automática) ────────
#
# Cada .o depende de las cabeceras que su .c incluye.
# Añadir manualmente las dependencias críticas para que
# 'make' recompile cuando cambie un .h.
#
$(OBJ_DIR)/main.o:    $(INC_DIR)/lexer.h    \
                      $(INC_DIR)/parser.h   \
                      $(INC_DIR)/symtab.h   \
                      $(INC_DIR)/ia32_types.h \
                      $(INC_DIR)/encoder.h  \
                      $(INC_DIR)/object.h   \
                      $(INC_DIR)/linker.h

$(OBJ_DIR)/lexer.o:   $(INC_DIR)/lexer.h

$(OBJ_DIR)/parser.o:  $(INC_DIR)/lexer.h   \
                      $(INC_DIR)/parser.h   \
                      $(INC_DIR)/symtab.h

$(OBJ_DIR)/symtab.o:  $(INC_DIR)/symtab.h

$(OBJ_DIR)/encoder.o: $(INC_DIR)/encoder.h \
                      $(INC_DIR)/ia32_types.h

$(OBJ_DIR)/object.o:  $(INC_DIR)/object.h

$(OBJ_DIR)/linker.o:  $(INC_DIR)/linker.h  \
                      $(INC_DIR)/object.h