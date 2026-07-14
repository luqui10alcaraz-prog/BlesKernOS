#!/usr/bin/env python3
"""Build TESTDLL.DLL: PE32 DLL with DllMain and exported ShowBlesMessage."""
import struct, sys
from pathlib import Path
from build_win32_msgbox import build as template
BASE, TEXT, IDATA, DATA = 0x500000, 0x1000, 0x2000, 0x3000
def build():
    b=bytearray(template()); struct.pack_into("<H",b,0x96,0x2102); struct.pack_into("<I",b,0xb4,BASE)
    struct.pack_into("<II",b,0xf8,DATA,0xb0); struct.pack_into("<I",b,0x104,0x28)
    i=memoryview(b)[0x600:0x800]; i[0x14:0x3c]=b"\0"*0x28
    d=memoryview(b)[0x800:0xa00]; d[:]=b"\0"*len(d)
    struct.pack_into("<IIHHIIIIIII",d,0,0,0,0,0,DATA+0x50,1,1,1,DATA+0x60,DATA+0x64,DATA+0x68)
    struct.pack_into("<IIIH",d,0x60,TEXT+0x10,DATA+0x90,0,0)
    d[0x50:0x5c]=b"TESTDLL.DLL\0"; d[0x90:0xa0]=b"ShowBlesMessage\0"
    msg=b"Mensaje ejecutado desde una DLL PE real\0"; cap=b"BlesKernOS TESTDLL.DLL\0"
    d[0xb0:0xb0+len(msg)]=msg; d[0x100:0x100+len(cap)]=cap
    code=bytearray(b"\xb8\x01\0\0\0\xc2\x0c\0").ljust(0x10,b"\x90")
    code+=b"\x6a\x00\x68"+struct.pack("<I",BASE+DATA+0x100)+b"\x68"+struct.pack("<I",BASE+DATA+0xb0)
    code+=b"\x6a\x00\xff\x15"+struct.pack("<I",BASE+IDATA+0x60)+b"\xc3"
    b[0x400:0x600]=b"\0"*0x200; b[0x400:0x400+len(code)]=code
    r=memoryview(b)[0xa00:0xc00]; r[:]=b"\0"*len(r)
    struct.pack_into("<IIHHHH",r,0,TEXT,16,0x3013,0x3018,0x301a,0)
    return bytes(b)
if __name__=="__main__":
    out=Path(sys.argv[1]); out.parent.mkdir(parents=True,exist_ok=True); out.write_bytes(build()); print(f"[PEGEN] {out}: 3072 bytes")
