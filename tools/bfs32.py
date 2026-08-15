#!/usr/bin/env python3
"""Create and inspect the 1994-style Bles File System (BFS32)."""

import argparse
import binascii
import pathlib
import struct
import time

BLOCK = 512
MAGIC = 0x32534642
VERSION = 0x00010000
CLEAN = 0x434C454E
INODE_SIZE = 256
INODE_COUNT = 128
ATTR_DIRECTORY = 0x10
ATTR_ARCHIVE = 0x20


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def inode(file_id: int, parent: int, name: str, attributes: int,
          size: int = 0, extent=(0, 0)) -> bytes:
    now = int(time.time())
    name_data = name.encode("utf-8")[:127].ljust(128, b"\0")
    extents = [extent] + [(0, 0)] * 7
    raw = struct.pack(
        "<IIQIIQQQI" + "II" * 8 + "III128s",
        file_id, parent, size, attributes, 0, now, now, now,
        1 if extent[1] else 0,
        *(value for pair in extents for value in pair),
        0, 1, 0, name_data,
    )
    assert len(raw) == INODE_SIZE
    return raw[:124] + struct.pack("<I", crc32(raw)) + raw[128:]


def create(path: pathlib.Path, size_mib: int, label: str) -> None:
    blocks = size_mib * 1024 * 1024 // BLOCK
    bitmap_start = 2
    bitmap_blocks = (blocks + BLOCK * 8 - 1) // (BLOCK * 8)
    inode_start = bitmap_start + bitmap_blocks
    inode_blocks = INODE_COUNT * INODE_SIZE // BLOCK
    data_start = inode_start + inode_blocks
    readme = (
        b"BFS32 volume\r\n"
        b"Extents, 64-bit file sizes, native long names and metadata CRC32.\r\n"
    )
    used = data_start + 1
    image = bytearray(blocks * BLOCK)
    image[510:512] = b"\x55\xAA"
    for block in range(used):
        image[bitmap_start * BLOCK + block // 8] |= 1 << (block % 8)
    root = inode(1, 1, "/", ATTR_DIRECTORY)
    info = inode(2, 1, "BFS32 README.TXT", ATTR_ARCHIVE,
                 len(readme), (data_start, 1))
    image[inode_start * BLOCK:inode_start * BLOCK + INODE_SIZE] = root
    image[inode_start * BLOCK + INODE_SIZE:
          inode_start * BLOCK + 2 * INODE_SIZE] = info
    image[data_start * BLOCK:data_start * BLOCK + len(readme)] = readme
    volume = label.encode("ascii", "replace")[:31].ljust(32, b"\0")
    superblock = struct.pack(
        "<14IQ32sI",
        MAGIC, VERSION, BLOCK, blocks, blocks - used,
        bitmap_start, bitmap_blocks, inode_start, inode_blocks,
        INODE_COUNT, 0, 3, CLEAN, 1, int(time.time()), volume, 0,
    )
    superblock = superblock[:-4] + struct.pack("<I", crc32(superblock))
    image[BLOCK:BLOCK + len(superblock)] = superblock
    path.write_bytes(image)
    print(f"BFS32: {path} ({blocks} blocks, {blocks-used} free)")


def inspect(path: pathlib.Path) -> None:
    data = path.read_bytes()
    fields = struct.unpack("<14IQ32sI", data[BLOCK:BLOCK + 100])
    if fields[0] != MAGIC:
        raise SystemExit("not a BFS32 image")
    print(f"BFS32 v{fields[1] >> 16}.{fields[1] & 0xffff}")
    print(f"label={fields[15].split(bytes([0]), 1)[0].decode()}")
    print(f"blocks={fields[3]} free={fields[4]} inode_count={fields[9]}")


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    mkfs = sub.add_parser("mkfs")
    mkfs.add_argument("image", type=pathlib.Path)
    mkfs.add_argument("--size-mib", type=int, default=16)
    mkfs.add_argument("--label", default="BLES")
    info = sub.add_parser("info")
    info.add_argument("image", type=pathlib.Path)
    args = parser.parse_args()
    if args.command == "mkfs":
        create(args.image, args.size_mib, args.label)
    else:
        inspect(args.image)


if __name__ == "__main__":
    main()
