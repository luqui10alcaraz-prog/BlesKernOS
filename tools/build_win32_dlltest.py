#!/usr/bin/env python3
"""Build DLLTEST.EXE, dynamically loading TESTDLL.DLL and its export."""
import struct, sys
from pathlib import Path
from build_win32_dynload import build as template
BASE, TEXT, IDATA, DATA = 0x400000,0x1000,0x2000,0x3000
def build():
    b=bytearray(template()); d=memoryview(b)[0x800:0xa00]; d[:]=b"\0"*len(d)
    d[0x90:0x9c]=b"TESTDLL.DLL\0"; d[0xb0:0xc0]=b"ShowBlesMessage\0"
    code=bytearray(b"\x68")+struct.pack("<I",BASE+DATA+0x90)
    code+=b"\xff\x15"+struct.pack("<I",BASE+IDATA+0x60)+b"\x89\xc3"
    code+=b"\x68"+struct.pack("<I",BASE+DATA+0xb0)+b"\x53\xff\x15"+struct.pack("<I",BASE+IDATA+0x64)
    code+=b"\x85\xc0\x74\x02\xff\xd0\x6a\x00\xff\x15"+struct.pack("<I",BASE+IDATA+0x68)+b"\xc3"
    b[0x400:0x600]=b"\0"*0x200; b[0x400:0x400+len(code)]=code
    r=memoryview(b)[0xa00:0xc00]; r[:]=b"\0"*len(r); entries=[0x3000|x for x in (1,7,14,21,35)]+[0]
    struct.pack_into("<II"+"H"*len(entries),r,0,TEXT,20,*entries); struct.pack_into("<I",b,0x124,20)
    return bytes(b)
if __name__=="__main__":
    out=Path(sys.argv[1]); out.parent.mkdir(parents=True,exist_ok=True); out.write_bytes(build()); print(f"[PEGEN] {out}: 3072 bytes")
