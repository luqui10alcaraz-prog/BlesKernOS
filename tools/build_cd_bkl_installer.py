#!/usr/bin/env python3
"""Build the staged BKL3 payload used by the BlesKernOS CD installer.

The text-mode installer copies only a small bootable bootstrap plus four BKL3
packages.  On the first HDD boot, native SETUP.BEX chooses the user settings
and extracts CORE, PROGRAMS and ASSETS; WINE is optional.
"""
from __future__ import annotations

import argparse
import binascii
import shutil
import struct
from pathlib import Path

WINDOW_SIZE = 4096
MIN_MATCH = 3
MAX_MATCH = 18
MAX_CHAIN = 32
METHOD_STORE = 0
METHOD_LZSS = 1

TERMS_TEXT = """BlesKernOS 0.8 - Terminos y condiciones\r\n\r\nBlesKernOS se distribuye bajo la licencia MIT. Se permite usar, copiar, modificar, combinar, publicar, distribuir, sublicenciar y vender copias del software, siempre que se conserve el aviso de copyright y el texto de la licencia.\r\n\r\nEl software se entrega "tal cual", sin garantias expresas ni implicitas. Los autores no son responsables por danos, perdida de datos o cualquier reclamo derivado de su uso.\r\n\r\nAlgunos componentes de terceros conservan sus propias licencias y avisos. La instalacion no cambia esas condiciones. Consulte README.TXT y la documentacion incluida para conocer sus atribuciones.\r\n\r\nEl codigo de licencia de esta version es solamente una interfaz de prueba: cualquier codigo no vacio es aceptado y no activa ni restringe funciones.\r\n"""

START_INI = """[START]\r\nBoot=Setup\r\nProgram=/SYSTEM/PROGRAMS/SETUP.BEX\r\nPackages=/SYSTEM/SETUP\r\n"""

PACKAGE_INI = """[PACKAGES]\r\nCore=/SYSTEM/SETUP/CORE.BKL\r\nPrograms=/SYSTEM/SETUP/PROGRAMS.BKL\r\nAssets=/SYSTEM/SETUP/ASSETS.BKL\r\nWine=/SYSTEM/SETUP/WINE.BKL\r\n"""


def encode_lzss(data: bytes) -> bytes:
    """Greedy LZSS: 4 KiB window, 3..18-byte matches, 16-bit tokens."""
    out = bytearray()
    head = [-1] * 65536
    previous = [-1] * len(data)
    pos = 0

    def hash3(p: int) -> int:
        return ((data[p] * 251 + data[p + 1]) * 251 + data[p + 2]) & 0xFFFF

    def remember(p: int) -> None:
        if p + MIN_MATCH > len(data):
            return
        h = hash3(p)
        previous[p] = head[h]
        head[h] = p

    while pos < len(data):
        flag_at = len(out)
        out.append(0)
        flags = 0
        for bit in range(8):
            if pos >= len(data):
                break
            best_len = 0
            best_dist = 0
            if pos + MIN_MATCH <= len(data):
                candidate = head[hash3(pos)]
                limit = min(MAX_MATCH, len(data) - pos)
                attempts = 0
                while candidate >= 0 and attempts < MAX_CHAIN:
                    distance = pos - candidate
                    if distance > WINDOW_SIZE:
                        break
                    if (data[candidate] != data[pos] or
                            data[candidate + 1] != data[pos + 1] or
                            data[candidate + 2] != data[pos + 2]):
                        candidate = previous[candidate]
                        attempts += 1
                        continue
                    length = MIN_MATCH
                    while (length < limit and
                           data[candidate + length] == data[pos + length]):
                        length += 1
                    if length > best_len:
                        best_len = length
                        best_dist = distance
                        if length == limit:
                            break
                    candidate = previous[candidate]
                    attempts += 1
            if best_len >= MIN_MATCH:
                token = ((best_dist - 1) << 4) | (best_len - MIN_MATCH)
                out += struct.pack("<H", token)
                for p in range(pos, pos + best_len):
                    remember(p)
                pos += best_len
            else:
                flags |= 1 << bit
                out.append(data[pos])
                remember(pos)
                pos += 1
        out[flag_at] = flags
    return bytes(out)


def pack_file(raw: bytes) -> tuple[int, bytes]:
    packed = encode_lzss(raw)
    return (METHOD_STORE, raw) if len(packed) >= len(raw) else (METHOD_LZSS, packed)


def classify(relative: Path) -> str | None:
    """Return package name, or None when the file belongs to the bootstrap."""
    parts = tuple(part.upper() for part in relative.parts)
    text = "/".join(parts)

    # Required before the graphical first-boot setup starts.
    if text.startswith("SYSTEM/DRIVERS/"):
        return None
    if text.startswith("SYSTEM/LENG/"):
        return None
    if text == "SYSTEM/WALLPAPR/NOCHE.BMP":
        return None

    if (text.startswith("SYSTEM/LIBS/WINE/") or
            text.startswith("SYSTEM/LIBS/WIN32/") or
            text.startswith("SYSTEM/WIN32/") or
            text == "SYSTEM/PROGRAMS/WINE.O"):
        return "WINE"

    if (text.startswith("SYSTEM/PROGRAMS/") or
            text.startswith("SYSTEM/COMMANDS/") or
            text.startswith("SYSTEM/CONTROL/") or
            text.startswith("SYSTEM/SCREENS/") or
            text.startswith("SYSTEM/SERVICES/")):
        return "PROGRAMS"

    if (text.startswith("ICONS/") or
            text == "SYSTEM/GRAPHICS.PAK" or
            text.startswith("DESKTOP/") or
            text.startswith("DOCUMENTS/") or
            text.startswith("DOCS/") or
            text.startswith("MISC/") or
            text.startswith("SYSTEM/WALLPAPR/") or
            text.startswith("SYSTEM/SOUNDS/") or
            not text.startswith("SYSTEM/")):
        return "ASSETS"

    return "CORE"


def archive_for(root: Path, files: list[Path]) -> tuple[bytes, dict[str, int]]:
    directories: set[Path] = set()
    for file in files:
        parent = file.relative_to(root).parent
        while str(parent) not in ("", "."):
            directories.add(parent)
            parent = parent.parent

    out = bytearray(b"BKL3" + struct.pack("<III", 1, len(files), 0))
    for directory in sorted(directories, key=lambda p: (len(p.parts), str(p).upper())):
        encoded = ("/" + directory.as_posix()).encode("utf-8")
        if len(encoded) >= 260:
            raise RuntimeError(f"Path too long: {encoded!r}")
        out.append(1)
        out += struct.pack("<H", len(encoded))
        out += encoded

    raw_total = 0
    stored = 0
    lzss = 0
    for file in sorted(files, key=lambda p: str(p.relative_to(root)).upper()):
        rel = "/" + file.relative_to(root).as_posix()
        path_bytes = rel.encode("utf-8")
        if len(path_bytes) >= 260:
            raise RuntimeError(f"Path too long: {rel}")
        raw = file.read_bytes()
        method, packed = pack_file(raw)
        crc = binascii.crc32(raw) & 0xFFFFFFFF
        raw_total += len(raw)
        stored += method == METHOD_STORE
        lzss += method == METHOD_LZSS
        out.append(2)
        out += struct.pack("<H", len(path_bytes))
        out += path_bytes
        out += struct.pack("<BIII", method, len(raw), len(packed), crc)
        out += packed
    out.append(0)
    return bytes(out), {
        "files": len(files), "raw": raw_total, "packed": len(out),
        "stored": stored, "lzss": lzss,
    }


def copy_tree_if_present(source: Path, destination: Path) -> None:
    if source.is_dir():
        shutil.copytree(source, destination, dirs_exist_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--setup-exe", type=Path, required=True)
    args = parser.parse_args()

    source = args.source_root.resolve()
    output = args.output.resolve()
    if not source.is_dir():
        raise SystemExit(f"Missing source root: {source}")
    if not args.setup_exe.is_file():
        raise SystemExit(f"Missing SETUP.BEX object: {args.setup_exe}")

    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)

    # Minimal first-boot environment.
    copy_tree_if_present(source / "SYSTEM" / "DRIVERS",
                         output / "SYSTEM" / "DRIVERS")
    copy_tree_if_present(source / "SYSTEM" / "LENG",
                         output / "SYSTEM" / "LENG")
    (output / "SYSTEM" / "PROGRAMS").mkdir(parents=True, exist_ok=True)
    (output / "SYSTEM" / "USER" / "CONFIG").mkdir(parents=True, exist_ok=True)
    (output / "SYSTEM" / "WALLPAPR").mkdir(parents=True, exist_ok=True)
    (output / "SYSTEM" / "SETUP").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.setup_exe, output / "SYSTEM" / "PROGRAMS" / "SETUP.BEX")
    night = source / "SYSTEM" / "WALLPAPR" / "NOCHE.BMP"
    if not night.is_file():
        raise RuntimeError("NOCHE.BMP is required by graphical Setup")
    shutil.copyfile(night, output / "SYSTEM" / "WALLPAPR" / "NOCHE.BMP")
    (output / "SYSTEM" / "USER" / "START.INI").write_text(START_INI, encoding="ascii")
    (output / "SYSTEM" / "SETUP" / "PACKAGES.INI").write_text(PACKAGE_INI, encoding="ascii")
    (output / "SYSTEM" / "SETUP" / "TERMS.TXT").write_text(TERMS_TEXT, encoding="utf-8")

    groups: dict[str, list[Path]] = {name: [] for name in ("CORE", "PROGRAMS", "ASSETS", "WINE")}
    for path in source.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(source)
        if any(part.upper() == "SYSTEM VOLUME INFORMATION" for part in rel.parts):
            continue
        group = classify(rel)
        if group:
            groups[group].append(path)

    lines = ["BlesKernOS 0.8 staged CD installer", "Format: BKL3 (LZSS-4K + STORE + CRC32)"]
    grand_raw = 0
    grand_packed = 0
    for name in ("CORE", "PROGRAMS", "ASSETS", "WINE"):
        archive, stats = archive_for(source, groups[name])
        destination = output / "SYSTEM" / "SETUP" / f"{name}.BKL"
        destination.write_bytes(archive)
        grand_raw += stats["raw"]
        grand_packed += stats["packed"]
        ratio = stats["packed"] * 100.0 / stats["raw"] if stats["raw"] else 0.0
        lines.append(
            f"{name}: files={stats['files']} raw={stats['raw']} bkl={stats['packed']} "
            f"ratio={ratio:.2f}% lzss={stats['lzss']} stored={stats['stored']}"
        )
        print(f"[BKL3-CD] {name}: {stats['raw']} -> {stats['packed']} bytes ({ratio:.2f}%)")

    overall = grand_packed * 100.0 / grand_raw if grand_raw else 0.0
    lines.append(f"TOTAL: raw={grand_raw} bkl={grand_packed} ratio={overall:.2f}%")
    (output / "SYSTEM" / "SETUP" / "MANIFEST.TXT").write_text(
        "\n".join(lines) + "\n", encoding="utf-8")
    print(f"[BKL3-CD] Total: {grand_raw} -> {grand_packed} bytes ({overall:.2f}%)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
