#!/usr/bin/env python3
"""Generate a relocatable PE32 program importing USER32!MessageBoxA."""
import struct
import sys
from pathlib import Path

BASE = 0x400000
def p(buf, off, fmt, *values): struct.pack_into("<" + fmt, buf, off, *values); return off + struct.calcsize("<" + fmt)

def build():
    b = bytearray(0xC00); text, idata, data, reloc = 0x1000, 0x2000, 0x3000, 0x4000
    b[:2] = b"MZ"; struct.pack_into("<I", b, 0x3c, 0x80); b[0x80:0x84] = b"PE\0\0"; o = 0x84
    o = p(b, o, "HHIIIHH", 0x14c, 4, 0, 0, 0, 0xe0, 0x102); start = o
    o = p(b, o, "HBB", 0x10b, 1, 0); o = p(b, o, "III", 0x200, 0x600, 0)
    o = p(b, o, "III", text, text, data); o = p(b, o, "III", BASE, 0x1000, 0x200)
    o = p(b, o, "HHHHHHI", 4, 0, 0, 0, 4, 0, 0)
    o = p(b, o, "IIIHHIIIIII", 0x5000, 0x400, 0, 2, 0x40, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
    dirs = [(0, 0)] * 16; dirs[1] = (idata, 0x3c); dirs[5] = (reloc, 16)
    for rva, size in dirs: o = p(b, o, "II", rva, size)
    assert o - start == 0xe0
    for name, vs, va, raw, ptr, flags in [(b".text\0\0\0",0x40,text,0x200,0x400,0x60000020),(b".idata\0\0",0x180,idata,0x200,0x600,0xc0000040),(b".data\0\0\0",0x100,data,0x200,0x800,0xc0000040),(b".reloc\0\0",16,reloc,0x200,0xa00,0x42000040)]:
        b[o:o+8] = name; o += 8; o = p(b, o, "IIIIIIHHI", vs, va, raw, ptr, 0, 0, 0, 0, flags)
    i = memoryview(b)[0x600:0x800]
    # Two import descriptors: USER32 and KERNEL32, followed by a null descriptor.
    struct.pack_into("<IIIII", i, 0x00, idata+0x50,0,0,idata+0x90,idata+0x60)
    struct.pack_into("<IIIII", i, 0x14, idata+0x58,0,0,idata+0xa0,idata+0x68)
    struct.pack_into("<II", i, 0x50, idata+0xb0, 0); struct.pack_into("<II", i, 0x60, idata+0xb0, 0)
    struct.pack_into("<II", i, 0x58, idata+0xc0, 0); struct.pack_into("<II", i, 0x68, idata+0xc0, 0)
    i[0x90:0x9b] = b"USER32.DLL\0"; i[0xa0:0xad] = b"KERNEL32.DLL\0"
    struct.pack_into("<H", i, 0xb0, 0); i[0xb2:0xbe] = b"MessageBoxA\0"
    struct.pack_into("<H", i, 0xc0, 0); i[0xc2:0xce] = b"ExitProcess\0"
    d = memoryview(b)[0x800:0xa00]; message = b"Primer programa grafico de Windows\0"; caption = b"BlesKernOS Win32\0"
    d[:len(message)] = message; d[0x40:0x40+len(caption)] = caption
    code = bytearray(b"\x6a\x00\x68") + struct.pack("<I",BASE+data+0x40) + b"\x68" + struct.pack("<I",BASE+data) + b"\x6a\x00\xff\x15" + struct.pack("<I",BASE+idata+0x60) + b"\x6a\x00\xff\x15" + struct.pack("<I",BASE+idata+0x68) + b"\xc3"
    b[0x400:0x400+len(code)] = code
    # Absolute operands at offsets 3, 8, 16 and 24.
    r = memoryview(b)[0xa00:0xc00]; struct.pack_into("<IIHHHH", r, 0, text, 16, 0x3003, 0x3008, 0x3010, 0x3018)
    return bytes(b)

if __name__ == "__main__":
    out = Path(sys.argv[1]); out.parent.mkdir(parents=True, exist_ok=True); out.write_bytes(build()); print(f"[PEGEN] {out}: 3072 bytes")
