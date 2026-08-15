# Prioridades Win32 para Windows 95 y Windows 98

La meta es ejecutar aplicaciones de escritorio de Win95/98, no reproducir
todas las capas de Windows modernos. El orden se basa en los imports típicos
de instaladores, utilidades, WinZip y programas Visual Basic de la época.

| Prioridad | DLL/API | Estado en BlesKernOS | Próximo trabajo |
| --- | --- | --- | --- |
| 1 | `KERNEL32`, `USER32`, `GDI32` | Base implementada | Imports ausentes detectados en programas reales; proceso, archivos, ventanas, mensajes y recursos deben tener implementación real. |
| 1 | `COMCTL32`, `COMDLG32`, `SHELL32` | Base implementada | Controles comunes, diálogos, asociaciones y operaciones de archivos. `CommandLineToArgvW` ya usa el parser adaptado de Wine. |
| 1 | `MSVCRT`/`CRTDLL`, `OLEAUT32`, `OLE32` | Base implementada | Priorizar runtime C, BSTR/VARIANT, automatización y COM mínimo, muy usados por VB y Office temprano. |
| 2 | `ADVAPI32`, perfiles/registro | Parcial | Registro, perfiles INI, tokens y seguridad mínima para instaladores. |
| 2 | `VERSION`, `LZ32`, `CABINET` | VERSION/LZ32 presentes; CABINET pendiente | Metadatos PE y compresión de instaladores; `CABINET.DLL` es el candidato siguiente más útil. |
| 2 | `WINSOCK`, `WININET`, `WINMM`, `MSACM32`, `AVIFIL32` | Base implementada | Red, sonido, multimedia y descargas; completar sólo las llamadas observadas. |
| 3 | `MPR`, `WINSPOOL`, `IMM32`, `SHLWAPI`, `RPCRT4` | Parcial | Red compartida, impresión, texto/IME, helpers de shell y RPC local. |
| 3 | `DDRAW`, `DSOUND`, `DINPUT` | Base parcial | Juegos y multimedia Win95/98; ampliar por juego probado. |
| No es sistema | DLL de cada aplicación, por ejemplo `WZINET32.DLL` | Stub libre cuando procede | No copiar DLLs propietarias. Son extensiones del programa y se resuelven caso por caso. |

## Regla de incorporación

Antes de adaptar Wine u otro proyecto libre, se debe identificar un import
faltante mediante un ejecutable de prueba o un log. Se incorpora la porción
mínima que no requiera WineServer/Unix, se conserva la atribución y se añade
la entrada correspondiente a `THIRD_PARTY_LICENSES.md`. No se incluyen DLL
extraídas de Windows.
