#!/usr/bin/env python3
"""Generate tiny deterministic BMP/ICO assets used by Stage 7 resource tests."""
from __future__ import annotations

import struct
import sys
from pathlib import Path


def build_bmp(path: Path) -> None:
    width = height = 16
    row_size = ((width * 3 + 3) // 4) * 4
    pixels = bytearray()
    for y in range(height):
        for x in range(width):
            # BGR checkerboard, deliberately simple.
            on = ((x // 4) + (y // 4)) & 1
            pixels += bytes((0x30 if on else 0xD0, 0x80, 0xE0 if on else 0x40))
        pixels += b"\0" * (row_size - width * 3)
    file_size = 14 + 40 + len(pixels)
    header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, 54)
    dib = struct.pack("<IIIHHIIIIII", 40, width, height, 1, 24, 0,
                      len(pixels), 2835, 2835, 0, 0)
    path.write_bytes(header + dib + pixels)


def build_ico(path: Path) -> None:
    width = height = 16
    xor = bytearray()
    # ICO DIB rows are bottom-up, BGRA.
    for y in range(height):
        for x in range(width):
            edge = x in (0, width - 1) or y in (0, height - 1)
            xor += bytes((0x20, 0x90 if edge else 0x40, 0xF0, 0xFF))
    and_row = ((width + 31) // 32) * 4
    and_mask = bytes(and_row * height)
    dib = struct.pack("<IIIHHIIIIII", 40, width, height * 2, 1, 32, 0,
                      len(xor) + len(and_mask), 0, 0, 0, 0)
    image = dib + xor + and_mask
    directory = struct.pack("<HHH", 0, 1, 1)
    entry = struct.pack("<BBBBHHII", width, height, 0, 0, 1, 32,
                        len(image), 6 + 16)
    path.write_bytes(directory + entry + image)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} OUTPUT_DIR", file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    build_bmp(out / "TESTBITMAP.BMP")
    build_ico(out / "TESTICON.ICO")
    (out / "resource-assets.stamp").write_text("ok\n", encoding="ascii")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
