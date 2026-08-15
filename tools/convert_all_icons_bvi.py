#!/usr/bin/env python3
# convert_all_icons_bvi.py
#
# Convierte recursivamente TODOS los BMP dentro de una carpeta a BVI:
# - 16 colores
# - color de la esquina superior izquierda como transparente
# - preserva subcarpetas
# - acepta .bmp, .BMP y cualquier combinación de mayúsculas/minúsculas
#
# Requiere que bmp_to_bvi.py esté en la misma carpeta o que se indique con
# --converter.

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convierte recursivamente todos los BMP de una carpeta a BVI."
    )
    parser.add_argument(
        "input_dir",
        nargs="?",
        type=Path,
        default=Path("icons"),
        help="carpeta de entrada (predeterminado: icons)",
    )
    parser.add_argument(
        "output_dir",
        nargs="?",
        type=Path,
        default=Path("icons-bvi"),
        help="carpeta de salida (predeterminado: icons-bvi)",
    )
    parser.add_argument(
        "--converter",
        type=Path,
        default=Path(__file__).with_name("bmp_to_bvi.py"),
        help="ruta a bmp_to_bvi.py",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="elimina BVI viejos de la carpeta de salida antes de convertir",
    )
    args = parser.parse_args()

    input_dir = args.input_dir.resolve()
    output_dir = args.output_dir.resolve()
    converter = args.converter.resolve()

    if not input_dir.is_dir():
        parser.error(f"no existe la carpeta de entrada: {input_dir}")

    if not converter.is_file():
        parser.error(f"no existe el conversor: {converter}")

    if args.clean and output_dir.exists():
        for old in output_dir.rglob("*.bvi"):
            old.unlink()

    bmp_files = sorted(
        path for path in input_dir.rglob("*")
        if path.is_file() and path.suffix.lower() == ".bmp"
    )

    if not bmp_files:
        print(f"No se encontraron BMP en {input_dir}")
        return 0

    converted = 0
    failed = 0

    print(f"Entrada: {input_dir}")
    print(f"Salida:  {output_dir}")
    print(f"BMP encontrados: {len(bmp_files)}")
    print()

    for source in bmp_files:
        relative = source.relative_to(input_dir)
        destination = output_dir / relative.with_suffix(".bvi")
        destination.parent.mkdir(parents=True, exist_ok=True)

        command = [
            sys.executable,
            str(converter),
            str(source),
            str(destination),
            "--transparent",
            "top-left",
            "--max-colors",
            "16",
        ]

        print(f"[{converted + failed + 1}/{len(bmp_files)}] {relative}")

        result = subprocess.run(command, check=False)
        if result.returncode == 0:
            converted += 1
        else:
            failed += 1
            print(f"ERROR: falló {relative}", file=sys.stderr)

    print()
    print("Conversión terminada")
    print(f"  OK:     {converted}")
    print(f"  Fallos: {failed}")
    print(f"  Total:  {len(bmp_files)}")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
