#!/usr/bin/env python3
"""Build a deterministic ICONS.PAK containing raw BVI1 vector documents."""

from __future__ import annotations

import struct
import sys
from pathlib import Path


MAGIC = b"BKVP"
VERSION = 1
ENTRY_SIZE = 24


def build(source_dir: Path, output: Path) -> None:
    entries: list[tuple[str, bytes]] = []
    for path in sorted(source_dir.glob("*.bvi"), key=lambda item: item.name.upper()):
        name = path.stem.upper()
        if not name or len(name.encode("ascii")) > 15:
            raise ValueError(f"nombre BVI no compatible con el paquete: {path.name}")
        data = path.read_bytes()
        if not data.startswith(b"BVI1"):
            raise ValueError(f"BVI1 invalido: {path}")
        entries.append((name, data))

    if not entries:
        raise ValueError(f"no hay archivos .bvi en {source_dir}")

    payload_offset = 12 + len(entries) * ENTRY_SIZE
    table = bytearray()
    payload = bytearray()
    for name, data in entries:
        encoded = name.encode("ascii")
        table += encoded + b"\0" * (16 - len(encoded))
        table += struct.pack("<II", payload_offset + len(payload), len(data))
        payload += data

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(MAGIC + struct.pack("<II", VERSION, len(entries)) +
                       table + payload)
    print(f"[BVIPAK] {output}: {len(entries)} iconos, {output.stat().st_size} bytes")


def main() -> int:
    if len(sys.argv) != 3:
        print("uso: build_bvi_pak.py <icons-bvi> <ICONS.PAK>", file=sys.stderr)
        return 2
    build(Path(sys.argv[1]), Path(sys.argv[2]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
