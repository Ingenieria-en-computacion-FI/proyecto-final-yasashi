#!/usr/bin/env python3
"""
inspect_obj.py - Herramienta para visualizar archivos objeto (.o)
Equipo Yasashi - Integrante 5
Uso: python3 scripts/inspect_obj.py archivo.o
"""

import sys
import struct
import os

# ============================================================
# CONSTANTES (deben coincidir con object.h)
# ============================================================
OBJ_MAGIC       = 0x4F424A21
MAX_SECTION_DATA = 4096

SYM_TYPES  = {0: "LOCAL", 1: "GLOBAL", 2: "EXTERN"}
RELOC_TYPES = {0: "REL32", 1: "ABS32"}

# ============================================================
# LEER EL ARCHIVO
# ============================================================
def read_obj(filename):
    if not os.path.exists(filename):
        print(f"Error: no se encontro '{filename}'")
        sys.exit(1)

    with open(filename, "rb") as f:
        data = f.read()

    offset = 0

    # --- Encabezado ---
    magic, num_sec, num_sym, num_rel = struct.unpack_from("<IIII", data, offset)
    offset += 16

    if magic != OBJ_MAGIC:
        print(f"Error: '{filename}' no es un archivo objeto valido")
        print(f"       Magic esperado: 0x{OBJ_MAGIC:08X}")
        print(f"       Magic encontrado: 0x{magic:08X}")
        sys.exit(1)

    print("=" * 50)
    print(f"  ARCHIVO OBJETO: {filename}")
    print("=" * 50)
    print(f"Magic:        0x{magic:08X}  (valido)")
    print(f"Secciones:    {num_sec}")
    print(f"Simbolos:     {num_sym}")
    print(f"Relocaciones: {num_rel}")

    # --- Secciones ---
    # Cada Section en C: name[16] + data[4096] + size(4) + offset(4)
    SEC_SIZE = 16 + MAX_SECTION_DATA + 4 + 4

    print("\n--- SECCIONES ---")
    sections = []
    for i in range(num_sec):
        name_bytes = data[offset:offset+16]
        name = name_bytes.split(b'\x00')[0].decode("utf-8", errors="replace")
        sec_data = data[offset+16 : offset+16+MAX_SECTION_DATA]
        size = struct.unpack_from("<I", data, offset+16+MAX_SECTION_DATA)[0]
        sec_offset = struct.unpack_from("<I", data, offset+16+MAX_SECTION_DATA+4)[0]

        sections.append({"name": name, "data": sec_data[:size], "size": size})

        print(f"\n[{i}] {name}  ({size} bytes)")
        # Imprimir hex dump
        for j in range(0, size, 16):
            chunk = sec_data[j:j+16]
            hex_part = " ".join(f"{b:02X}" for b in chunk)
            asc_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            print(f"  {j:04X}:  {hex_part:<48}  {asc_part}")

        offset += SEC_SIZE

    # --- Simbolos ---
    # Cada Symbol en C: name[64] + value(4) + section_idx(4) + type(4) + defined(4)
    SYM_SIZE = 64 + 4 + 4 + 4 + 4

    print("\n--- SIMBOLOS ---")
    for i in range(num_sym):
        name_bytes = data[offset:offset+64]
        name  = name_bytes.split(b'\x00')[0].decode("utf-8", errors="replace")
        value = struct.unpack_from("<I", data, offset+64)[0]
        sec   = struct.unpack_from("<i", data, offset+68)[0]
        stype = struct.unpack_from("<I", data, offset+72)[0]
        defnd = struct.unpack_from("<I", data, offset+76)[0]

        tipo_str = SYM_TYPES.get(stype, "?")
        print(f"[{i}] {name:<20}  val=0x{value:04X}  sec={sec}  "
              f"tipo={tipo_str:<6}  definido={defnd}")
        offset += SYM_SIZE

    # --- Relocaciones ---
    # Cada Relocation en C: offset(4) + sym_idx(4) + type(4) + addend(4)
    REL_SIZE = 4 + 4 + 4 + 4

    print("\n--- RELOCACIONES ---")
    if num_rel == 0:
        print("  (ninguna)")
    for i in range(num_rel):
        rel_off = struct.unpack_from("<I", data, offset)[0]
        sym_idx = struct.unpack_from("<I", data, offset+4)[0]
        rtype   = struct.unpack_from("<I", data, offset+8)[0]
        addend  = struct.unpack_from("<i", data, offset+12)[0]

        rtype_str = RELOC_TYPES.get(rtype, "?")
        print(f"[{i}] offset=0x{rel_off:04X}  sym={sym_idx}  "
              f"tipo={rtype_str}  addend={addend}")
        offset += REL_SIZE

    print("\n" + "=" * 50)


# ============================================================
# MAIN
# ============================================================
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Uso: python3 scripts/inspect_obj.py <archivo.o>")
        sys.exit(1)
    read_obj(sys.argv[1])
