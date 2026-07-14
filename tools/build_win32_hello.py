#!/usr/bin/env python3
"""Build a tiny relocatable PE32/i386 console executable for BlesKernOS.

The generated program imports only GetStdHandle, WriteFile and ExitProcess
from KERNEL32.DLL. It is deliberately produced without a compiler so the
first PE loader smoke test is reproducible on any host with Python 3.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

FILE_ALIGNMENT = 0x200
SECTION_ALIGNMENT = 0x1000
IMAGE_BASE = 0x00400000


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def pack_into(buf: bytearray, offset: int, fmt: str, *values: int) -> int:
    struct.pack_into("<" + fmt, buf, offset, *values)
    return offset + struct.calcsize("<" + fmt)


def build_pe() -> bytes:
    text_rva = 0x1000
    idata_rva = 0x2000
    data_rva = 0x3000
    reloc_rva = 0x4000

    headers_size = 0x400
    text_raw = 0x400
    idata_raw = 0x600
    data_raw = 0x800
    reloc_raw = 0xA00
    file_size = 0xC00
    image_size = 0x5000

    image = bytearray(file_size)

    # DOS header and a minimal DOS stub area.
    image[0:2] = b"MZ"
    struct.pack_into("<I", image, 0x3C, 0x80)
    dos_stub = b"This program requires BlesKernOS.\r\n$"
    image[0x40:0x40 + len(dos_stub)] = dos_stub

    pe_offset = 0x80
    image[pe_offset:pe_offset + 4] = b"PE\0\0"
    off = pe_offset + 4

    # IMAGE_FILE_HEADER.
    off = pack_into(
        image,
        off,
        "HHIIIHH",
        0x014C,       # IMAGE_FILE_MACHINE_I386
        4,            # sections
        0, 0, 0,
        0x00E0,       # PE32 optional header size
        0x0102,       # executable | 32-bit machine
    )

    optional_start = off
    # IMAGE_OPTIONAL_HEADER32, standard fields.
    off = pack_into(image, off, "HBB", 0x010B, 1, 0)
    off = pack_into(image, off, "III", 0x200, 0x600, 0)
    off = pack_into(image, off, "III", text_rva, text_rva, data_rva)
    off = pack_into(image, off, "I", IMAGE_BASE)
    off = pack_into(image, off, "II", SECTION_ALIGNMENT, FILE_ALIGNMENT)
    off = pack_into(image, off, "HHHHHH", 4, 0, 0, 0, 4, 0)
    off = pack_into(image, off, "I", 0)
    off = pack_into(image, off, "III", image_size, headers_size, 0)
    off = pack_into(image, off, "HH", 3, 0x0040)  # console, dynamic base
    off = pack_into(image, off, "IIIIII", 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)

    # Data directories: export, import, resource, exception, security,
    # base relocation, then ten unused entries.
    directories = [(0, 0)] * 16
    directories[1] = (idata_rva, 0x28)
    directories[5] = (reloc_rva, 20)
    for rva, size in directories:
        off = pack_into(image, off, "II", rva, size)

    if off - optional_start != 0xE0:
        raise AssertionError("optional header size mismatch")

    sections = [
        (b".text\0\0\0", 0x40, text_rva, 0x200, text_raw, 0x60000020),
        (b".idata\0\0", 0x100, idata_rva, 0x200, idata_raw, 0xC0000040),
        (b".data\0\0\0", 0x80, data_rva, 0x200, data_raw, 0xC0000040),
        (b".reloc\0\0", 20, reloc_rva, 0x200, reloc_raw, 0x42000040),
    ]
    for name, virtual_size, virtual_address, raw_size, raw_offset, characteristics in sections:
        image[off:off + 8] = name
        off += 8
        off = pack_into(
            image,
            off,
            "IIIIIIHHI",
            virtual_size,
            virtual_address,
            raw_size,
            raw_offset,
            0, 0, 0, 0,
            characteristics,
        )

    if off > headers_size:
        raise AssertionError("headers exceed first file-aligned block")

    # .idata layout.
    descriptor_rva = idata_rva
    int_rva = idata_rva + 0x40
    iat_rva = idata_rva + 0x50
    dll_name_rva = idata_rva + 0x70
    getstd_name_rva = idata_rva + 0x80
    write_name_rva = idata_rva + 0x90
    exit_name_rva = idata_rva + 0xA0

    idata = memoryview(image)[idata_raw:idata_raw + 0x200]
    struct.pack_into("<IIIII", idata, 0x00, int_rva, 0, 0, dll_name_rva, iat_rva)
    # The descriptor at +0x14 remains zero as the terminator.
    thunks = (getstd_name_rva, write_name_rva, exit_name_rva, 0)
    struct.pack_into("<IIII", idata, 0x40, *thunks)
    struct.pack_into("<IIII", idata, 0x50, *thunks)
    idata[0x70:0x70 + len(b"KERNEL32.DLL\0")] = b"KERNEL32.DLL\0"
    struct.pack_into("<H", idata, 0x80, 0)
    idata[0x82:0x82 + len(b"GetStdHandle\0")] = b"GetStdHandle\0"
    struct.pack_into("<H", idata, 0x90, 0)
    idata[0x92:0x92 + len(b"WriteFile\0")] = b"WriteFile\0"
    struct.pack_into("<H", idata, 0xA0, 0)
    idata[0xA2:0xA2 + len(b"ExitProcess\0")] = b"ExitProcess\0"

    # Writable data used by WriteFile.
    message = b"Hello from Win32 on BlesKernOS!\r\n"
    written_va = IMAGE_BASE + data_rva
    message_va = IMAGE_BASE + data_rva + 4
    data = memoryview(image)[data_raw:data_raw + 0x200]
    struct.pack_into("<I", data, 0, 0)
    data[4:4 + len(message)] = message

    # x86 entrypoint. Every absolute image address below is covered by .reloc.
    getstd_iat_va = IMAGE_BASE + iat_rva
    write_iat_va = getstd_iat_va + 4
    exit_iat_va = getstd_iat_va + 8
    code = bytearray()
    code += b"\x6A\xF5"                                  # push -11
    code += b"\xFF\x15" + struct.pack("<I", getstd_iat_va)
    code += b"\x89\xC3"                                  # mov ebx,eax
    code += b"\x6A\x00"                                  # overlapped = NULL
    code += b"\x68" + struct.pack("<I", written_va)
    code += b"\x6A" + bytes([len(message)])
    code += b"\x68" + struct.pack("<I", message_va)
    code += b"\x53"                                       # handle
    code += b"\xFF\x15" + struct.pack("<I", write_iat_va)
    code += b"\x6A\x00"
    code += b"\xFF\x15" + struct.pack("<I", exit_iat_va)
    code += b"\xC3"
    image[text_raw:text_raw + len(code)] = code

    # IMAGE_BASE_RELOCATION block for the five absolute address operands.
    relocation_offsets = (0x004, 0x00D, 0x014, 0x01B, 0x023)
    reloc = memoryview(image)[reloc_raw:reloc_raw + 0x200]
    struct.pack_into("<II", reloc, 0, text_rva, 20)
    entries = [(3 << 12) | value for value in relocation_offsets]
    entries.append(0)  # IMAGE_REL_BASED_ABSOLUTE padding
    struct.pack_into("<HHHHHH", reloc, 8, *entries)

    return bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    payload = build_pe()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    print(f"[PEGEN] {args.output}: {len(payload)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
