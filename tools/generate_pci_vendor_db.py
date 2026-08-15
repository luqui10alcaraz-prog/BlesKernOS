#!/usr/bin/env python3
"""Convierte una tabla Vendor Id|Vendor Name en un header C ordenado."""

from pathlib import Path
import argparse


def c_string(text: str) -> str:
    return (text.replace("\\", "\\\\")
                .replace('"', '\\"')
                .replace("?", "\\?")
                .replace("\r", " ")
                .replace("\n", " "))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    vendors: dict[int, str] = {}
    text = args.input.read_text(encoding="utf-8-sig", errors="replace")
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line.lower().startswith("0x") or "|" not in line:
            continue
        raw_id, name = line.split("|", 1)
        try:
            vendor = int(raw_id.strip(), 16)
        except ValueError:
            continue
        name = " ".join(name.strip().split())
        if 0 <= vendor <= 0xFFFF and name:
            vendors[vendor] = name

    lines = [
        "#ifndef BLESKERNOS_DEVMGR_PCI_VENDOR_DB_H",
        "#define BLESKERNOS_DEVMGR_PCI_VENDOR_DB_H",
        "",
        "/* Generado por tools/generate_pci_vendor_db.py. */",
        "static const devmgr_pci_vendor_t g_devmgr_pci_vendors[] = {",
    ]
    for vendor, name in sorted(vendors.items()):
        lines.append(f'    {{0x{vendor:04X}, "{c_string(name)}"}},')
    lines.extend(["};", "", "#endif", ""])
    args.output.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"[PCI-DB] {len(vendors)} fabricantes -> {args.output}")


if __name__ == "__main__":
    main()
