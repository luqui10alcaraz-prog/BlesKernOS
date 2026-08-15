# Optional Win32 runtime libraries

Place compatible 32-bit Windows 95/98 runtime DLLs here before building the ATA
image. The image builder copies `.dll`, `.drv`, and `.ocx` files into
`/SYSTEM/LIBS/WIN32`.

Typical files for Visual C++ 6 applications are `MFC42.DLL`, `MSVCP60.DLL`,
`MFC42LOC.DLL`, and any application-specific COM/ActiveX DLLs. These proprietary
Microsoft binaries are not included in the BlesKernOS source tree.
