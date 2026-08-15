# BlesKernOS 0.8 — compatibilidad Win32, fase 2

## Objetivo

Esta fase amplía la compatibilidad para aplicaciones comerciales de Windows
95/98 sobre la fase 1. El objetivo no es declarar compatibilidad completa con
Win32, sino reemplazar varios stubs por rutas funcionales y permitir cargar las
DLL originales que contienen MFC, STL, COM o controles específicos de una
aplicación.

La implementación fue adaptada a las primitivas de BlesKernOS y contrastada con
la semántica de Win32 y la organización de los módulos equivalentes de Wine.

## 1. Runtime de Visual C++

`MSVCRT.DLL`, `CRTDLL.DLL`, `MSVCRT20.DLL` y `MSVCRT40.DLL` comparten ahora una
capa CRT más amplia:

- descriptores CRT: `_open`, `_sopen`, `_wopen`, `_read`, `_write`, `_close`,
  `_lseek`, `_tell`, `_filelength`, `_commit`, `_isatty`, `_get_osfhandle` y
  `_open_osfhandle`;
- operaciones de archivos y directorios: `_unlink`, `_wunlink`, `_mkdir`,
  `_rmdir`, `rename` y `_access`;
- cadenas ANSI y wide: comparaciones sin distinguir mayúsculas, duplicación,
  cambio de mayúsculas/minúsculas y familia `wcs*` básica;
- `getenv`, `_putenv`, `rand`, `srand`, `clock`, `time`, `qsort`, `bsearch`,
  `floor`, `ceil` y `fmod`;
- helpers de Visual C++: `_purecall`, `_set_new_handler`, `__dllonexit`,
  `_controlfp`, `_control87`, `_ftol`, helpers x87 `_CI*`, operadores globales
  `new/delete`, `_except_handler3`, `__CxxFrameHandler` y `_CxxThrowException`.

El estado de descriptores, variables de entorno y errno se limpia por proceso o
thread según corresponda.

### Límite de C++

`_CxxThrowException` genera la excepción Microsoft, pero
`__CxxFrameHandler` todavía no implementa el recorrido completo de tablas EH,
destructores y bloques `catch`. Aplicaciones C++ que no lanzan excepciones
pueden avanzar; software que dependa del unwinding de Visual C++ todavía puede
fallar.

## 2. MFC42 y DLL nativas

MFC no se reimplementa dentro del kernel. El cargador PE permite usar la
`MFC42.DLL` original y otras DLL de runtime de 32 bits.

Orden de búsqueda de DLL:

1. ruta explícita;
2. directorio del EXE actual;
3. `/SYSTEM/LIBS/WIN32/`;
4. `/SYSTEM/LIBS/WINE/`;
5. `/SYSTEM/WIN32/`.

La tabla de módulos cargados aumenta a 32 entradas. La imagen ATA crea
`/SYSTEM/LIBS/WIN32`; además, el constructor copia automáticamente los archivos
`.DLL`, `.DRV` y `.OCX` colocados en `system/win32-libs/`.

Para aplicaciones Visual C++ 6 suelen ser útiles:

- `MFC42.DLL`;
- `MFC42LOC.DLL`;
- `MSVCP60.DLL`;
- DLL COM/ActiveX específicas del programa.

Esas DLL no se incluyen en el repositorio.

## 3. Common dialogs

`COMDLG32.DLL` incorpora:

- `GetOpenFileNameA/W` conectado al selector gráfico nativo;
- filtros por extensión, directorio inicial, título y validación de archivo;
- `GetSaveFileNameA/W`, extensión predeterminada y validación de ruta;
- `GetFileTitleA/W`;
- `FindTextA` y `ReplaceTextA` modeless con notificaciones al propietario;
- `ChooseFontA/W` y `ChooseColorA/W` con valores coherentes;
- error extendido por `CommDlgExtendedError`;
- respuesta correcta de “sin impresora” para `PrintDlg` y `PageSetupDlg`.

Todavía faltan hooks/templates completos, selección múltiple, diálogo visual de
fuente/color y subsistema de impresión.

## 4. Shell32 y asociaciones

- asociaciones leídas desde `HKEY_CLASSES_ROOT`;
- verbos `shell\\<verb>\\command` y expansión de `%1`, `%L`, `%l`, `%*` y `%%`;
- `ShellExecuteA/W` y `ShellExecuteExA/W`;
- `FindExecutableA/W`;
- `AssocQueryStringA/W` en SHLWAPI para comando, ejecutable, ProgID, nombre
  amigable, tipo MIME e icono predeterminado;
- `SHGetFileInfoA/W` con nombres de tipo tomados del registro;
- carpetas especiales, creación de directorios y selector de carpetas;
- PIDL mínimo basado en ruta para `SHBrowseForFolder` y
  `SHGetPathFromIDList`;
- extracción de iconos básica;
- `SHFileOperationA/W` para una operación simple de copiar, mover, renombrar o
  eliminar;
- `CommandLineToArgvW`.

No hay todavía accesos directos `.LNK`, namespace de Shell, `IShellFolder`,
papelera ni operaciones recursivas/multiarchivo completas.

## 5. RichEdit

El control RichEdit comparte el editor interno con `EDIT`, pero suma:

- `EM_STREAMIN` y `EM_STREAMOUT` para texto y un subconjunto de RTF;
- lectura de escapes RTF básicos, párrafos, tabulaciones y caracteres hex;
- exportación RTF escapada;
- `EM_SETCHARFORMAT`/`EM_GETCHARFORMAT`;
- `EM_SETPARAFORMAT`/`EM_GETPARAFORMAT`;
- undo/redo de un nivel.

El formato se guarda actualmente a nivel de documento, no por runs. No hay OLE
embebido, layout Unicode complejo ni motor RTF completo.

## 6. COM básico

`OLE32.DLL` incorpora:

- estado `CoInitialize`/`CoInitializeEx` por thread;
- `CoRegisterClassObject` y `CoRevokeClassObject`;
- `CoGetClassObject` y `CoCreateInstance`;
- carga de servidores `InprocServer32` registrados en
  `HKCR\\CLSID\\{GUID}\\InprocServer32` mediante `DllGetClassObject`;
- `IUnknown` y `IClassFactory` consumidos mediante vtable;
- `CoGetMalloc` con un `IMalloc` funcional;
- `CreateStreamOnHGlobal` y `GetHGlobalFromStream` con un `IStream` básico;
- GUID parsing, formatting y generación;
- limpieza de class factories al finalizar el proceso.

No implementa marshaling, proxies/stubs, monikers, apartments reales,
structured storage, ROT ni OLE embedding.

## 7. WinMM y streaming PCM

`WINMM.DLL` incorpora:

- `waveOutOpen`, prepare/unprepare, cola de buffers, write, pause, restart,
  reset y close;
- callbacks `WOM_OPEN`, `WOM_DONE` y `WOM_CLOSE` por ventana, thread o función;
- PCM mono/estéreo de 8/16 bits convertido al mezclador interno de 8 bits;
- posición, volumen, descripción de errores y device caps;
- timers multimedia `timeSetEvent`/`timeKillEvent`;
- `PlaySoundA/W` y `sndPlaySoundA/W`.

El hardware expuesto sigue siendo una salida PCM física. La posición es
aproximada y la finalización de varios streams simultáneos depende del estado
global del mezclador SB16.

## 8. Winsock asíncrono

- `WSAAsyncSelect` fuerza modo no bloqueante y entrega mensajes
  `FD_READ`, `FD_WRITE`, `FD_ACCEPT`, `FD_CONNECT` y `FD_CLOSE`;
- `WSAAsyncGetHostByName` y `WSAAsyncGetHostByAddr` escriben un `HOSTENT` en el
  buffer del llamador y notifican por mensaje;
- `WSACancelAsyncRequest` está presente;
- `WSAGetLastError` utiliza el error asociado al thread Win32;
- el polling de red ocurre durante el message pump.

La resolución DNS se realiza de forma inmediata y la notificación se entrega de
forma asíncrona; no existe todavía una tarea DNS cancelable en segundo plano.

## Integración con el message pump

El bombeo de mensajes Win32 ejecuta también:

- polling de sockets asíncronos;
- avance de colas `waveOut`;
- timers multimedia.

Esto conserva el modelo esperado por aplicaciones GUI de la época: las
notificaciones se observan mientras el programa procesa su cola de mensajes.

## Compilación verificada

```sh
make -j2 build/kernel.elf
objcopy -O binary build/kernel.elf build/kernel.bin
```

Resultado de la compilación limpia usada para generar el parche:

- ELF32 Intel i386 enlazado sin símbolos indefinidos;
- `kernel.bin`: 576228 bytes;
- 1126 sectores ocupados de 1152 reservados;
- 26 sectores de margen.

Stage2 fue ampliado de 1088 a 1152 sectores. Ese es el límite práctico antes de
alcanzar la región física VGA al cargar el kernel desde `0x10000`.

Las únicas advertencias de la compilación limpia proceden de
`kernel/drivers/core/gfx.c` y ya existían antes de esta fase.

## Pruebas recomendadas

1. Colocar `MFC42.DLL` junto a una aplicación MFC pequeña y registrar cada import
   todavía faltante.
2. Probar Metapad/AkelPad temprano, un cliente FTP/IRC antiguo y una utilidad MFC
   de una sola ventana.
3. Registrar una extensión en HKCR y comprobar `AssocQueryString`,
   `FindExecutable` y `ShellExecute`.
4. Hacer streaming de varios `WAVEHDR` y comprobar `WOM_DONE`.
5. Abrir un socket con `WSAAsyncSelect` y verificar todos los mensajes.
6. Registrar un class factory COM local y luego una DLL `InprocServer32`.
7. Importar/exportar texto y RTF desde un control RichEdit.

## Límites heredados de la fase 1

Siguen vigentes la ausencia de espacios virtuales aislados, permisos de páginas
impuestos por CPU, mapeo directo en `ImageBase`, mappings por páginas reales y
módulos DLL privados por proceso. Esos puntos afectan especialmente a software
comercial grande y no pueden resolverse únicamente agregando exports Win32.

## Aplicación del parche

El parche de fase 2 se aplica sobre un árbol que ya contiene la fase 1:

```sh
cd "BlesKernOS 0.8 phase1"
patch -p1 < BlesKernOS-0.8-win32-phase2.patch
make -j2 build/kernel.elf
```

Para construir la imagen completa también se necesitan NASM, las herramientas
MinGW i686 usadas por las pruebas Win32 y `dosfstools`/mtools para FAT32.
