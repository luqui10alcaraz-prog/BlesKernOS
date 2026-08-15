#!/usr/bin/env python3
"""Build the BlesKernOS multi-floppy BKL3 installer set.

BKL3 uses a classic 4 KiB-window LZSS stream (12-bit distance, 4-bit
length) with per-file STORE fallback and CRC32.  The container and disk
splitting remain BlesKernOS-specific and stream safely across floppy parts.
"""
from __future__ import annotations

import argparse
import binascii
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

FLOPPY_SIZE = 1_474_560
PART_SIZE = 1_300_000
WINDOW_SIZE = 4096
MIN_MATCH = 3
MAX_MATCH = 18
MAX_CHAIN = 32
METHOD_STORE = 0
METHOD_LZSS = 1


def run(*args: str) -> None:
    subprocess.run(args, check=True)


def _key3(data: bytes, pos: int) -> int:
    return (data[pos] << 16) | (data[pos + 1] << 8) | data[pos + 2]


def encode_lzss(data: bytes) -> bytes:
    """Greedy LZSS using a bounded 16-bit hash chain."""
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
                    while length < limit and data[candidate + length] == data[pos + length]:
                        length += 1
                    if length > best_len:
                        best_len = length
                        best_dist = distance
                        if length == limit:
                            break
                    candidate = previous[candidate]
                    attempts += 1
            if best_len >= MIN_MATCH:
                code = ((best_dist - 1) << 4) | (best_len - MIN_MATCH)
                out += struct.pack("<H", code)
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
    # STORE avoids expansion on GIF/JPEG/WAV-compressed/otherwise random data.
    if len(packed) >= len(raw):
        return METHOD_STORE, raw
    return METHOD_LZSS, packed


def build_archive(root: Path) -> tuple[bytes, int, int, int, int]:
    directories: list[Path] = []
    files: list[Path] = []
    for path in sorted(root.rglob("*"), key=lambda p: (len(p.relative_to(root).parts), str(p).upper())):
        rel = path.relative_to(root)
        if any(part.upper() in {"SYSTEM VOLUME INFORMATION"} for part in rel.parts):
            continue
        if path.is_dir():
            directories.append(path)
        elif path.is_file():
            files.append(path)

    # magic, version, file count, flags
    out = bytearray(b"BKL3" + struct.pack("<III", 1, len(files), 0))
    for directory in directories:
        rel = "/" + directory.relative_to(root).as_posix()
        encoded = rel.encode("utf-8")
        if len(encoded) >= 260:
            raise RuntimeError(f"Path too long: {rel}")
        out.append(1)
        out += struct.pack("<H", len(encoded))
        out += encoded

    raw_total = 0
    stored_files = 0
    lzss_files = 0
    for file in files:
        rel = "/" + file.relative_to(root).as_posix()
        path_bytes = rel.encode("utf-8")
        if len(path_bytes) >= 260:
            raise RuntimeError(f"Path too long: {rel}")
        raw = file.read_bytes()
        method, packed = pack_file(raw)
        crc = binascii.crc32(raw) & 0xFFFFFFFF
        raw_total += len(raw)
        stored_files += method == METHOD_STORE
        lzss_files += method == METHOD_LZSS
        # type, path length, path, method, original, encoded, CRC32
        out.append(2)
        out += struct.pack("<H", len(path_bytes))
        out += path_bytes
        out += struct.pack("<BIII", method, len(raw), len(packed), crc)
        out += packed
    out.append(0)
    return bytes(out), len(files), raw_total, stored_files, lzss_files


def make_blank_fat12(path: Path, label: str) -> None:
    path.write_bytes(b"\0" * FLOPPY_SIZE)
    run("mformat", "-i", str(path), "-f", "1440", "-v", label[:11], "::")


def write_disk_id(folder: Path, number: int, total: int) -> None:
    (folder / "DISK.ID").write_bytes(b"BKD1" + struct.pack("<III", number, total, 0))


def copy_folder_to_image(folder: Path, image: Path) -> None:
    for child in folder.iterdir():
        run("mcopy", "-o", "-i", str(image), str(child), "::/")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ata-image", required=True, type=Path)
    parser.add_argument("--boot1-installer", required=True, type=Path)
    parser.add_argument("--boot-fat32", required=True, type=Path)
    parser.add_argument("--stage2", required=True, type=Path)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    for command in ("mcopy", "mformat"):
        if shutil.which(command) is None:
            raise SystemExit(f"Missing {command}; install mtools")

    args.output.mkdir(parents=True, exist_ok=True)
    for old in args.output.glob("disk*.img"):
        old.unlink()

    with tempfile.TemporaryDirectory(prefix="bles-floppy-") as tmp_name:
        tmp = Path(tmp_name)
        root = tmp / "root"
        root.mkdir()
        run("mcopy", "-s", "-n", "-i", str(args.ata_image), "::/*", str(root) + "/")
        archive, file_count, raw_total, stored_files, lzss_files = build_archive(root)
        parts = [archive[i:i + PART_SIZE] for i in range(0, len(archive), PART_SIZE)]
        total_disks = 2 + len(parts)

        disk1 = args.output / "disk01.img"
        boot1 = args.boot1_installer.read_bytes()
        stage2 = args.stage2.read_bytes()
        kernel = args.kernel.read_bytes()
        kernel_offset = 9 * 512
        if len(boot1) != 512:
            raise RuntimeError(f"Installer Stage 1 must be 512 bytes, got {len(boot1)}")
        if len(stage2) > 8 * 512:
            raise RuntimeError(f"Stage 2 exceeds its 8-sector slot: {len(stage2)} bytes")
        if kernel_offset + len(kernel) > FLOPPY_SIZE:
            raise RuntimeError(
                f"Kernel does not fit on boot floppy: {len(kernel)} bytes "
                f"(maximum {FLOPPY_SIZE - kernel_offset})"
            )
        image = bytearray(FLOPPY_SIZE)
        image[0:len(boot1)] = boot1
        image[512:512 + len(stage2)] = stage2
        image[kernel_offset:kernel_offset + len(kernel)] = kernel
        disk1.write_bytes(image)

        disk2 = args.output / "disk02.img"
        make_blank_fat12(disk2, "BLESBOOT")
        disk2_folder = tmp / "disk2"
        disk2_folder.mkdir()
        write_disk_id(disk2_folder, 2, total_disks)
        shutil.copyfile(args.boot_fat32, disk2_folder / "BOOTF32.BIN")
        shutil.copyfile(args.stage2, disk2_folder / "STAGE2.BIN")
        shutil.copyfile(args.kernel, disk2_folder / "KERNEL.BIN")
        copy_folder_to_image(disk2_folder, disk2)

        for index, part in enumerate(parts):
            number = index + 3
            image = args.output / f"disk{number:02d}.img"
            make_blank_fat12(image, f"BLESD{number:03d}")
            folder = tmp / f"disk{number:02d}"
            folder.mkdir()
            write_disk_id(folder, number, total_disks)
            (folder / "PART.BKL").write_bytes(part)
            copy_folder_to_image(folder, image)

    ratio = (len(archive) * 100.0 / raw_total) if raw_total else 0.0
    manifest = args.output / "MANIFEST.TXT"
    manifest.write_text(
        "BlesKernOS 0.8 multi-floppy installer\n"
        "Format: BKL3 (LZSS-4K + STORE fallback + CRC32)\n"
        f"Disks: {total_disks}\nFiles: {file_count}\n"
        f"Raw bytes: {raw_total}\nBKL3 bytes: {len(archive)}\n"
        f"Ratio: {ratio:.2f}%\nLZSS files: {lzss_files}\n"
        f"Stored files: {stored_files}\nPart bytes: {PART_SIZE}\n",
        encoding="utf-8",
    )
    print(f"[OK] {total_disks} images in {args.output}")
    print(f"[BKL3] {raw_total} -> {len(archive)} bytes ({ratio:.2f}%); {file_count} files")
    print(f"[BKL3] LZSS: {lzss_files}; stored: {stored_files}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
