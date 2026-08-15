#!/usr/bin/env python3
# bvi_viewer.py
#
# Visualiza archivos BVI1 generados por bmp_to_bvi.py.
# Puede:
#   - abrir un BVI en una ventana
#   - exportarlo a PNG
#   - generar una galería con todos los BVI de una carpeta
#
# Requiere:
#   sudo apt install -y python3-pil python3-pil.imagetk

from __future__ import annotations

import argparse
import math
import sys
from dataclasses import dataclass
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont, ImageTk
except ImportError as exc:
    raise SystemExit(
        "ERROR: falta Pillow/Tk.\n"
        "Instalalo con:\n"
        "  sudo apt install -y python3-pil python3-pil.imagetk python3-tk"
    ) from exc


@dataclass(frozen=True, slots=True)
class BVI:
    width: int
    height: int
    colors: dict[int, tuple[int, int, int, int]]
    rects: list[tuple[int, int, int, int, int]]


def load_bvi(path: Path) -> BVI:
    lines = [
        line.strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]

    if not lines or lines[0] != "BVI1":
        raise ValueError(f"{path}: no es un BVI1 válido")

    width = height = 0
    colors: dict[int, tuple[int, int, int, int]] = {}
    rects: list[tuple[int, int, int, int, int]] = []

    for line_number, line in enumerate(lines[1:], start=2):
        parts = line.split()
        if not parts:
            continue

        command = parts[0]

        try:
            if command == "canvas":
                width, height = int(parts[1]), int(parts[2])

            elif command == "color":
                index = int(parts[1])
                colors[index] = tuple(map(int, parts[2:6]))  # type: ignore[assignment]

            elif command == "rect":
                x, y, w, h, color_id = map(int, parts[1:6])
                rects.append((x, y, w, h, color_id))

            elif command in {"source", "colors", "rects", "end"}:
                continue

        except (IndexError, ValueError) as exc:
            raise ValueError(
                f"{path}:{line_number}: línea inválida: {line}"
            ) from exc

    if width <= 0 or height <= 0:
        raise ValueError(f"{path}: canvas inválido")

    return BVI(width, height, colors, rects)


def render_bvi(
    bvi: BVI,
    width: int,
    height: int,
    background: tuple[int, int, int, int] = (0, 0, 0, 0),
) -> Image.Image:
    if width <= 0 or height <= 0:
        raise ValueError("el tamaño de salida debe ser positivo")

    image = Image.new("RGBA", (width, height), background)
    draw = ImageDraw.Draw(image)

    for x, y, w, h, color_id in bvi.rects:
        color = bvi.colors.get(color_id)
        if color is None or color[3] == 0:
            continue

        # Escalado por bordes, no por ancho aislado. Evita huecos entre rects.
        x0 = (x * width) // bvi.width
        y0 = (y * height) // bvi.height
        x1 = ((x + w) * width + bvi.width - 1) // bvi.width
        y1 = ((y + h) * height + bvi.height - 1) // bvi.height

        if x1 <= x0:
            x1 = x0 + 1
        if y1 <= y0:
            y1 = y0 + 1

        draw.rectangle((x0, y0, x1 - 1, y1 - 1), fill=color)

    return image


def parse_size(value: str) -> tuple[int, int]:
    value = value.lower().strip()
    if "x" not in value:
        side = int(value)
        return side, side

    width, height = value.split("x", 1)
    return int(width), int(height)


def checkerboard(size: tuple[int, int], cell: int = 8) -> Image.Image:
    image = Image.new("RGBA", size, (224, 224, 224, 255))
    draw = ImageDraw.Draw(image)

    for y in range(0, size[1], cell):
        for x in range(0, size[0], cell):
            if ((x // cell) + (y // cell)) & 1:
                draw.rectangle(
                    (x, y, x + cell - 1, y + cell - 1),
                    fill=(176, 176, 176, 255),
                )

    return image


def composite_checker(icon: Image.Image) -> Image.Image:
    base = checkerboard(icon.size)
    base.alpha_composite(icon)
    return base


def export_one(
    source: Path,
    destination: Path,
    size: tuple[int, int],
    transparent: bool,
) -> None:
    bvi = load_bvi(source)
    icon = render_bvi(bvi, *size)

    destination.parent.mkdir(parents=True, exist_ok=True)
    (icon if transparent else composite_checker(icon)).save(destination)

    print(f"OK: {source} -> {destination} ({size[0]}x{size[1]})")


def build_gallery(
    folder: Path,
    destination: Path,
    icon_size: int,
    columns: int,
) -> None:
    files = sorted(
        path for path in folder.rglob("*")
        if path.is_file() and path.suffix.lower() == ".bvi"
    )

    if not files:
        raise ValueError(f"no se encontraron BVI en {folder}")

    columns = max(1, columns)
    rows = math.ceil(len(files) / columns)
    cell_width = max(icon_size + 24, 150)
    cell_height = icon_size + 50

    gallery = Image.new(
        "RGBA",
        (columns * cell_width, rows * cell_height),
        (238, 238, 238, 255),
    )
    draw = ImageDraw.Draw(gallery)
    font = ImageFont.load_default()

    for index, path in enumerate(files):
        column = index % columns
        row = index // columns
        cell_x = column * cell_width
        cell_y = row * cell_height

        bvi = load_bvi(path)
        icon = render_bvi(bvi, icon_size, icon_size)
        preview = composite_checker(icon)

        icon_x = cell_x + (cell_width - icon_size) // 2
        icon_y = cell_y + 8
        gallery.alpha_composite(preview, (icon_x, icon_y))

        label = path.stem
        if len(label) > 22:
            label = label[:19] + "..."

        bbox = draw.textbbox((0, 0), label, font=font)
        text_width = bbox[2] - bbox[0]
        draw.text(
            (cell_x + (cell_width - text_width) // 2, cell_y + icon_size + 16),
            label,
            fill=(0, 0, 0, 255),
            font=font,
        )

    destination.parent.mkdir(parents=True, exist_ok=True)
    gallery.save(destination)

    print(f"OK: galería de {len(files)} iconos -> {destination}")


def show_window(source: Path, size: tuple[int, int]) -> None:
    import tkinter as tk

    bvi = load_bvi(source)
    icon = composite_checker(render_bvi(bvi, *size))

    root = tk.Tk()
    root.title(f"BVI Viewer - {source.name}")
    root.resizable(False, False)

    photo = ImageTk.PhotoImage(icon)
    label = tk.Label(root, image=photo, bd=0)
    label.image = photo
    label.pack()

    info = tk.Label(
        root,
        text=(
            f"{source.name} | canvas {bvi.width}x{bvi.height} | "
            f"{len(bvi.colors)} colores | {len(bvi.rects)} rectángulos"
        ),
        padx=8,
        pady=6,
    )
    info.pack(fill="x")

    root.mainloop()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Visualizador y exportador de iconos BVI1."
    )
    parser.add_argument(
        "input",
        type=Path,
        help="archivo .bvi o carpeta para --gallery",
    )
    parser.add_argument(
        "--size",
        type=parse_size,
        default=(256, 256),
        help="tamaño de salida: 256 o 256x192",
    )
    parser.add_argument(
        "--png",
        type=Path,
        help="exportar un BVI a PNG",
    )
    parser.add_argument(
        "--transparent",
        action="store_true",
        help="mantener transparencia al exportar PNG",
    )
    parser.add_argument(
        "--gallery",
        type=Path,
        help="crear una galería PNG con todos los BVI de una carpeta",
    )
    parser.add_argument(
        "--columns",
        type=int,
        default=6,
        help="columnas de la galería",
    )
    parser.add_argument(
        "--icon-size",
        type=int,
        default=128,
        help="tamaño de cada icono en la galería",
    )

    args = parser.parse_args()

    try:
        if args.gallery:
            if not args.input.is_dir():
                parser.error("con --gallery, input debe ser una carpeta")
            build_gallery(
                args.input,
                args.gallery,
                args.icon_size,
                args.columns,
            )
            return 0

        if not args.input.is_file():
            parser.error(f"no existe: {args.input}")

        if args.png:
            export_one(
                args.input,
                args.png,
                args.size,
                args.transparent,
            )
            return 0

        show_window(args.input, args.size)
        return 0

    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
