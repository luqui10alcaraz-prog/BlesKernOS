#!/usr/bin/env python3
"""
bmp_to_bvi.py
Convierte iconos BMP/PNG a BVI1, un formato vectorial simple para BlesKernOS.

El conversor es pixel-vectorial:
- conserva exactamente la silueta y colores del icono raster;
- agrupa pixeles iguales en rectangulos grandes;
- el renderer puede escalar esos rectangulos a cualquier tamano;
- no intenta adivinar circulos o curvas semanticas.

Dependencia:
    python3 -m pip install Pillow

Ejemplos:
    python3 bmp_to_bvi.py floppy.bmp floppy.bvi
    python3 bmp_to_bvi.py floppy.bmp floppy.bvi --transparent top-left
    python3 bmp_to_bvi.py floppy.bmp floppy.bvi --max-colors 32 --preview floppy-preview.png
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit(
        "ERROR: falta Pillow.\n"
        "Instalalo con:\n"
        "  python3 -m pip install Pillow"
    ) from exc


BVI_MAGIC = "BVI1"


@dataclass(frozen=True, slots=True)
class Rect:
    x: int
    y: int
    w: int
    h: int
    color: tuple[int, int, int, int]


def parse_hex_color(value: str) -> tuple[int, int, int, int]:
    value = value.strip().lstrip("#")
    if not re.fullmatch(r"[0-9a-fA-F]{6}([0-9a-fA-F]{2})?", value):
        raise argparse.ArgumentTypeError(
            "color invalido; usa #RRGGBB o #RRGGBBAA"
        )
    if len(value) == 6:
        value += "FF"
    return tuple(int(value[i:i + 2], 16) for i in range(0, 8, 2))  # type: ignore[return-value]


def choose_transparency(
    image: Image.Image,
    transparent: str | tuple[int, int, int, int],
    alpha_threshold: int,
) -> Image.Image:
    rgba = image.convert("RGBA")
    pixels = list(rgba.getdata())

    key: tuple[int, int, int, int] | None = None
    if transparent == "top-left":
        key = rgba.getpixel((0, 0))
    elif isinstance(transparent, tuple):
        key = transparent

    output: list[tuple[int, int, int, int]] = []
    for r, g, b, a in pixels:
        if a <= alpha_threshold:
            output.append((r, g, b, 0))
            continue
        if key is not None and (r, g, b) == key[:3]:
            output.append((r, g, b, 0))
            continue
        output.append((r, g, b, 255))

    rgba.putdata(output)
    return rgba


def quantize_rgba(image: Image.Image, max_colors: int) -> Image.Image:
    if max_colors <= 0 or max_colors > 256:
        raise ValueError("max_colors debe estar entre 1 y 256")

    alpha = image.getchannel("A")
    rgb = image.convert("RGB")
    quantized = rgb.quantize(
        colors=max_colors,
        method=Image.Quantize.FASTOCTREE,
        dither=Image.Dither.NONE,
    ).convert("RGB")

    result = Image.merge("RGBA", (*quantized.split(), alpha))
    normalized: list[tuple[int, int, int, int]] = []
    for r, g, b, a in result.getdata():
        normalized.append((r, g, b, 0 if a == 0 else 255))
    result.putdata(normalized)
    return result


def horizontal_runs(image: Image.Image) -> list[list[tuple[int, int, tuple[int, int, int, int]]]]:
    width, height = image.size
    rows: list[list[tuple[int, int, tuple[int, int, int, int]]]] = []

    for y in range(height):
        runs: list[tuple[int, int, tuple[int, int, int, int]]] = []
        x = 0
        while x < width:
            color = image.getpixel((x, y))
            if color[3] == 0:
                x += 1
                continue

            start = x
            x += 1
            while x < width and image.getpixel((x, y)) == color:
                x += 1
            runs.append((start, x, color))
        rows.append(runs)

    return rows


def merge_runs_to_rectangles(image: Image.Image) -> list[Rect]:
    rows = horizontal_runs(image)
    active: dict[tuple[int, int, tuple[int, int, int, int]], Rect] = {}
    finished: list[Rect] = []

    for y, runs in enumerate(rows):
        current_keys = {(x0, x1, color) for x0, x1, color in runs}
        next_active: dict[tuple[int, int, tuple[int, int, int, int]], Rect] = {}

        for key, rect in active.items():
            if key not in current_keys:
                finished.append(rect)

        for x0, x1, color in runs:
            key = (x0, x1, color)
            previous = active.get(key)
            if previous is not None:
                next_active[key] = Rect(
                    previous.x, previous.y, previous.w, previous.h + 1, previous.color
                )
            else:
                next_active[key] = Rect(x0, y, x1 - x0, 1, color)
        active = next_active

    finished.extend(active.values())
    finished.sort(key=lambda rect: (rect.y, rect.x, rect.color))
    return finished


def build_palette(rectangles: Iterable[Rect]) -> list[tuple[int, int, int, int]]:
    return sorted({rect.color for rect in rectangles})


def write_bvi(
    output_path: Path,
    width: int,
    height: int,
    rectangles: list[Rect],
    source_name: str,
) -> None:
    palette = build_palette(rectangles)
    color_ids = {color: index for index, color in enumerate(palette)}

    lines = [
        BVI_MAGIC,
        f"canvas {width} {height}",
        f'source "{source_name}"',
        f"colors {len(palette)}",
    ]
    for index, (r, g, b, a) in enumerate(palette):
        lines.append(f"color {index} {r} {g} {b} {a}")

    lines.append(f"rects {len(rectangles)}")
    for rect in rectangles:
        lines.append(
            f"rect {rect.x} {rect.y} {rect.w} {rect.h} {color_ids[rect.color]}"
        )
    lines.append("end")
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def render_preview(
    image: Image.Image,
    output_path: Path,
    scale: int,
    antialias: bool,
) -> None:
    if scale < 1:
        raise ValueError("preview-scale debe ser >= 1")
    target = (image.width * scale, image.height * scale)
    method = Image.Resampling.LANCZOS if antialias else Image.Resampling.NEAREST
    image.resize(target, method).save(output_path)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convierte BMP/PNG a BVI1 para BlesKernOS."
    )
    parser.add_argument("input", type=Path, help="BMP/PNG de entrada")
    parser.add_argument("output", type=Path, help="archivo .bvi de salida")
    parser.add_argument(
        "--transparent",
        default="alpha",
        help="alpha, none, top-left o #RRGGBB; predeterminado: alpha",
    )
    parser.add_argument(
        "--alpha-threshold", type=int, default=0,
        help="alpha <= este valor se vuelve transparente (0-255)",
    )
    parser.add_argument(
        "--max-colors", type=int, default=64,
        help="maximo de colores despues de cuantizar (1-256)",
    )
    parser.add_argument(
        "--no-quantize", action="store_true",
        help="no reducir la paleta",
    )
    parser.add_argument("--preview", type=Path, help="guardar preview PNG")
    parser.add_argument("--preview-scale", type=int, default=8)
    parser.add_argument("--preview-antialias", action="store_true")
    args = parser.parse_args()

    if not args.input.exists():
        parser.error(f"no existe: {args.input}")
    if not 0 <= args.alpha_threshold <= 255:
        parser.error("--alpha-threshold debe estar entre 0 y 255")

    transparent: str | tuple[int, int, int, int]
    if args.transparent in {"alpha", "none", "top-left"}:
        transparent = args.transparent
    else:
        transparent = parse_hex_color(args.transparent)

    with Image.open(args.input) as opened:
        image = opened.convert("RGBA")

    if transparent == "none":
        image.putalpha(255)
    else:
        image = choose_transparency(image, transparent, args.alpha_threshold)

    if not args.no_quantize:
        image = quantize_rgba(image, args.max_colors)

    rectangles = merge_runs_to_rectangles(image)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_bvi(args.output, image.width, image.height, rectangles, args.input.name)

    if args.preview:
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        render_preview(image, args.preview, args.preview_scale, args.preview_antialias)

    raw_pixels = image.width * image.height
    covered_pixels = sum(rect.w * rect.h for rect in rectangles)
    colors = len(build_palette(rectangles))

    print(f"OK: {args.input} -> {args.output}")
    print(f"canvas: {image.width}x{image.height}")
    print(f"colores: {colors}")
    print(f"rectangulos: {len(rectangles)}")
    print(f"pixeles opacos: {covered_pixels}/{raw_pixels}")
    if args.preview:
        print(f"preview: {args.preview}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nCancelado.", file=sys.stderr)
        raise SystemExit(130)
