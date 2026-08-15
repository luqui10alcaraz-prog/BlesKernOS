#!/usr/bin/env python3
"""PE32 test that resolves USER32!MessageBoxA through KERNEL32 at runtime."""
import struct, sys
from pathlib import Path
from build_win32_msgbox import build as build_template
BASE, IDATA, DATA, TEXT = 0x400000, 0x2000, 0x3000, 0x1000
def build():
    b=bytearray(build_template()); i=memoryview(b)[0x600:0x800]; i[:]=b"\0"*len(i)
    struct.pack_into("<IIIII",i,0,IDATA+0x40,0,0,IDATA+0x90,IDATA+0x60)
    names=(IDATA+0xb0,IDATA+0xc0,IDATA+0xe0,0)
    struct.pack_into("<IIII",i,0x40,*names); struct.pack_into("<IIII",i,0x60,*names)
    i[0x90:0x9d]=b"KERNEL32.DLL\0"
    for off,name in ((0xb0,b"LoadLibraryA\0"),(0xc0,b"GetProcAddress\0"),(0xe0,b"ExitProcess\0")):
        struct.pack_into("<H",i,off,0); i[off+2:off+2+len(name)]=name
    d=memoryview(b)[0x800:0xa00]; d[:]=b"\0"*len(d)
    message=b"MessageBoxA obtenida con GetProcAddress\0"; caption=b"BlesKernOS dynamic Win32\0"
    d[:len(message)]=message; d[0x50:0x50+len(caption)]=caption
    d[0x90:0x9b]=b"USER32.DLL\0"; d[0xb0:0xbc]=b"MessageBoxA\0"
    code=bytearray(b"\x68")+struct.pack("<I",BASE+DATA+0x90)
    code+=b"\xff\x15"+struct.pack("<I",BASE+IDATA+0x60)+b"\x89\xc3"
    code+=b"\x68"+struct.pack("<I",BASE+DATA+0xb0)+b"\x53\xff\x15"+struct.pack("<I",BASE+IDATA+0x64)
    code+=b"\x85\xc0\x74\x10\x6a\x00\x68"+struct.pack("<I",BASE+DATA+0x50)
    code+=b"\x68"+struct.pack("<I",BASE+DATA)+b"\x6a\x00\xff\xd0\x6a\x00\xff\x15"+struct.pack("<I",BASE+IDATA+0x68)+b"\xc3"
    b[0x400:0x600]=b"\0"*0x200; b[0x400:0x400+len(code)]=code
    r=memoryview(b)[0xa00:0xc00]; r[:]=b"\0"*len(r); entries=[0x3000|x for x in (1,7,14,21,32,37,49)]+[0]
    struct.pack_into("<II"+"H"*len(entries),r,0,TEXT,24,*entries)
    struct.pack_into("<I",b,0x104,0x28); struct.pack_into("<I",b,0x124,24)
    return bytes(b)
if __name__=="__main__":
    out=Path(sys.argv[1]); out.parent.mkdir(parents=True,exist_ok=True); out.write_bytes(build()); print(f"[PEGEN] {out}: 3072 bytes")
