#!/usr/bin/env python3
from pathlib import Path
import struct
import sys

TRANSPARENT_MAGENTA = 0xFF00FF

def rd16(data, off):
    return data[off] | (data[off + 1] << 8)

def rd32(data, off):
    return data[off] | (data[off + 1] << 8) | (data[off + 2] << 16) | (data[off + 3] << 24)

def s32(v):
    return v - 0x100000000 if v & 0x80000000 else v

def rgb_of(r, g, b):
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF)

def rgb_dist(a, b):
    ar = (a >> 16) & 0xFF
    ag = (a >> 8) & 0xFF
    ab = a & 0xFF
    br = (b >> 16) & 0xFF
    bg = (b >> 8) & 0xFF
    bb = b & 0xFF
    return abs(ar - br) + abs(ag - bg) + abs(ab - bb)

def detect_corner_key(raw_rgb, w, h):
    if w <= 0 or h <= 0:
        return None

    corners = [
        raw_rgb[0],
        raw_rgb[w - 1],
        raw_rgb[(h - 1) * w],
        raw_rgb[(h - 1) * w + (w - 1)],
    ]

    best = None
    best_count = 0

    for c in corners:
        count = 0
        for d in corners:
            if rgb_dist(c, d) <= 9:
                count += 1
        if count > best_count:
            best = c
            best_count = count

    # Si al menos 2 esquinas son casi iguales, asumimos fondo.
    if best_count >= 2:
        return best

    return None

def make_pixel(rgb, alpha=0xFF, key_rgb=None):
    if rgb == TRANSPARENT_MAGENTA:
        return 0x00000000

    if key_rgb is not None and rgb_dist(rgb, key_rgb) <= 9:
        return 0x00000000

    if alpha == 0:
        alpha = 0xFF

    return ((alpha & 0xFF) << 24) | (rgb & 0xFFFFFF)

def decode_bmp_raw_rgb(path):
    data = path.read_bytes()

    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError(f"{path}: no es BMP")

    pixel_off = rd32(data, 10)
    dib = rd32(data, 14)

    if dib < 40:
        raise ValueError(f"{path}: DIB header no soportado")

    w = s32(rd32(data, 18))
    h_raw = s32(rd32(data, 22))
    planes = rd16(data, 26)
    bpp = rd16(data, 28)
    compression = rd32(data, 30)

    if planes != 1:
        raise ValueError(f"{path}: planes inválidos")
    if compression != 0:
        raise ValueError(f"{path}: BMP comprimido no soportado")
    if w <= 0 or h_raw == 0:
        raise ValueError(f"{path}: dimensiones inválidas")

    top_down = h_raw < 0
    h = -h_raw if top_down else h_raw

    palette = []
    if bpp <= 8:
        colors_used = rd32(data, 46) if len(data) >= 50 else 0
        count = colors_used if colors_used else (1 << bpp)
        pal_off = 14 + dib

        for i in range(count):
            off = pal_off + i * 4
            if off + 3 >= len(data):
                break
            b, g, r = data[off], data[off + 1], data[off + 2]
            palette.append(rgb_of(r, g, b))

    row_bits = w * bpp
    row_bytes = ((row_bits + 31) // 32) * 4

    raw_rgb = [0] * (w * h)
    raw_alpha = [0xFF] * (w * h)

    for y in range(h):
        src_y = y if top_down else (h - 1 - y)
        row = pixel_off + src_y * row_bytes

        for x in range(w):
            dst = y * w + x

            if bpp == 32:
                off = row + x * 4
                if off + 3 >= len(data):
                    continue
                b = data[off]
                g = data[off + 1]
                r = data[off + 2]
                a = data[off + 3]
                raw_rgb[dst] = rgb_of(r, g, b)
                raw_alpha[dst] = a

            elif bpp == 24:
                off = row + x * 3
                if off + 2 >= len(data):
                    continue
                b = data[off]
                g = data[off + 1]
                r = data[off + 2]
                raw_rgb[dst] = rgb_of(r, g, b)
                raw_alpha[dst] = 0xFF

            elif bpp == 8:
                off = row + x
                if off >= len(data):
                    continue
                idx = data[off]
                raw_rgb[dst] = palette[idx] if idx < len(palette) else 0
                raw_alpha[dst] = 0xFF

            elif bpp == 4:
                off = row + x // 2
                if off >= len(data):
                    continue
                byte = data[off]
                idx = (byte >> 4) if (x % 2 == 0) else (byte & 0x0F)
                raw_rgb[dst] = palette[idx] if idx < len(palette) else 0
                raw_alpha[dst] = 0xFF

            elif bpp == 1:
                off = row + x // 8
                if off >= len(data):
                    continue
                bit = 7 - (x % 8)
                idx = (data[off] >> bit) & 1
                raw_rgb[dst] = palette[idx] if idx < len(palette) else 0
                raw_alpha[dst] = 0xFF

            else:
                raise ValueError(f"{path}: bpp {bpp} no soportado")

    key_rgb = detect_corner_key(raw_rgb, w, h)

    pixels = []
    opaque = 0
    transparent = 0

    for rgb, alpha in zip(raw_rgb, raw_alpha):
        px = make_pixel(rgb, alpha, key_rgb)
        if (px >> 24) == 0:
            transparent += 1
        else:
            opaque += 1
        pixels.append(px)

    return w, h, pixels, key_rgb, opaque, transparent

def make_name16(name):
    nb = name.encode("ascii", "replace")[:15]
    return nb + bytes(16 - len(nb))

def build_pak(icon_dir, out_path):
    icon_dir = Path(icon_dir)
    out_path = Path(out_path)

    bmps = sorted(icon_dir.glob("*.BMP")) + sorted(icon_dir.glob("*.bmp"))

    seen = set()
    unique = []

    for bmp in bmps:
        key = bmp.name.upper()
        if key in seen:
            continue
        seen.add(key)
        unique.append(bmp)

    if not unique:
        raise SystemExit(f"No encontré BMPs en {icon_dir}")

    entries = []
    payload = bytearray()

    header_size = 12
    entry_size = 32
    data_base = header_size + entry_size * len(unique)

    for bmp in unique:
        name = bmp.stem.upper()[:15]
        w, h, pixels, key_rgb, opaque, transparent = decode_bmp_raw_rgb(bmp)

        while (data_base + len(payload)) & 3:
            payload.append(0)

        offset = data_base + len(payload)

        raw = bytearray()
        for px in pixels:
            raw += struct.pack("<I", px & 0xFFFFFFFF)

        size = len(raw)
        payload += raw
        entries.append((name, w, h, offset, size, bmp.name, key_rgb, opaque, transparent))

    out = bytearray()
    out += b"BKIP"
    out += struct.pack("<II", 1, len(entries))

    for name, w, h, offset, size, original, key_rgb, opaque, transparent in entries:
        out += make_name16(name)
        out += struct.pack("<IIII", w, h, offset, size)

    expected_payload_start = 12 + 32 * len(entries)
    if len(out) != expected_payload_start:
        raise SystemExit(f"Tabla rota: len(out)={len(out)} expected={expected_payload_start}")

    out += payload
    out_path.write_bytes(out)

    print(f"[ICONPAK] {out_path} creado")
    print(f"[ICONPAK] {len(entries)} iconos, {len(out)} bytes")
    print(f"[ICONPAK] payload_start={expected_payload_start}")

    for name, w, h, offset, size, original, key_rgb, opaque, transparent in entries:
        key = "none" if key_rgb is None else f"#{key_rgb:06X}"
        print(
            f"  {name:<12} <- {original:<16} "
            f"{w}x{h:<4} off={offset:<7} size={size:<6} "
            f"key={key:<8} opaque={opaque:<6} transparent={transparent}"
        )

def main(argv):
    if len(argv) < 2:
        icon_dir = Path("assets/icons")
        out_path = icon_dir / "ICONS.PAK"
    elif len(argv) == 2:
        icon_dir = Path(argv[1])
        out_path = icon_dir / "ICONS.PAK"
    else:
        icon_dir = Path(argv[1])
        out_path = Path(argv[2])

    build_pak(icon_dir, out_path)
    return 0

if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
