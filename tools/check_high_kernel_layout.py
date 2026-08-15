#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess

root = Path.cwd()
kernel_bin = root / "build/kernel.bin"
kernel_elf = root / "build/kernel.elf"
stage2_bin = root / "build/stage2.bin"
stage2_src = root / "boot/stage2.asm"
memory_h = root / "kernel/include/memory.h"
memory_c = root / "kernel/memory.c"
errors = []

if not kernel_bin.exists(): errors.append("falta build/kernel.bin")
if not kernel_elf.exists(): errors.append("falta build/kernel.elf")
if errors:
    for error in errors: print("ERROR:", error)
    raise SystemExit(1)

size = kernel_bin.stat().st_size
sectors = (size + 511) // 512
print(f"kernel.bin       : {size} bytes ({sectors} sectores)")
print("destino kernel   : 0x00100000")
print(f"fin kernel plano : 0x{0x00100000 + size:08X}")
print("límite PROGBITS  : 0x00200000")
if size > 0x100000:
    errors.append(f"kernel.bin supera 1 MiB por {size - 0x100000} bytes")

result = subprocess.run(
    ["readelf", "-SW", str(kernel_elf)],
    check=True, capture_output=True, text=True,
)
sections = {}
pattern = re.compile(
    r"\[\s*\d+\]\s+(\.\S+)\s+\S+\s+"
    r"([0-9A-Fa-f]+)\s+[0-9A-Fa-f]+\s+([0-9A-Fa-f]+)"
)
for line in result.stdout.splitlines():
    match = pattern.search(line)
    if match:
        sections[match.group(1)] = (int(match.group(2), 16), int(match.group(3), 16))

for required in (".text", ".data", ".bss"):
    if required not in sections: errors.append(f"no encontré {required} en kernel.elf")
if ".text" in sections:
    address, _ = sections[".text"]
    print(f".text            : 0x{address:08X}")
    if address != 0x00100000: errors.append(".text no comienza en 0x00100000")
if ".data" in sections:
    address, length = sections[".data"]
    print(f".data            : 0x{address:08X} .. 0x{address + length:08X}")
    if address + length > 0x00200000: errors.append(".data invade la región de BSS")
if ".bss" in sections:
    address, length = sections[".bss"]
    print(f".bss             : 0x{address:08X} .. 0x{address + length:08X}")
    if address != 0x00200000: errors.append(".bss no comienza en 0x00200000")
    if address + length >= 0x003F0000: errors.append(".bss invade la pila bootstrap")

stage2_text = stage2_src.read_text()
if not re.search(r"KERNEL_LOAD_ADDR\s+equ\s+0x00100000", stage2_text):
    errors.append("Stage 2 no apunta a 0x00100000")
if not re.search(r"KERNEL_BOUNCE_ADDR\s+equ\s+0x00010000", stage2_text):
    errors.append("Stage 2 no tiene bounce buffer en 0x10000")
memory_layout = memory_h.read_text() + "\n" + memory_c.read_text()
if not re.search(r"(?:HEAP_START|HEAP_MIN_START|heap_start)[^\n]*0x00400000", memory_layout):
    errors.append("el heap no comienza en 0x00400000")
if stage2_bin.exists():
    stage2_size = stage2_bin.stat().st_size
    print(f"stage2.bin       : {stage2_size} bytes")
    if stage2_size != 4096: errors.append(f"Stage 2 mide {stage2_size}, debería medir 4096 bytes")
if sectors > 2048: errors.append("kernel.bin supera la reserva de 2048 sectores")

if errors:
    print()
    for error in errors: print("ERROR:", error)
    raise SystemExit(1)
print()
print("OK: layout HighMem consistente")
