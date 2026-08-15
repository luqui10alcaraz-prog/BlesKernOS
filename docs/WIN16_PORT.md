# Capa Win16/NE para BlesKernOS 0.8

## Alcance de este parche

Este parche convierte el cargador NE que ya existía en una primera capa de
compatibilidad Win16 ejecutable. No incorpora un Wine completo ni pretende
hacer funcionar todavía todo Windows 3.x. Adapta a la arquitectura de
BlesKernOS las piezas esenciales del diseño Win16 de Wine:

- carga persistente de segmentos NE de 16 bits;
- selectores GDT de código y datos con límite de 64 KiB;
- relocaciones internas e imports por ordinal o nombre;
- `equate` importados de KERNEL (`__AHSHIFT`, `__AHINCR`, `__WINFLAGS` y
  selectores históricos);
- relays 16-bit que entran al kernel mediante `int 0x80`;
- limpieza Pascal de la pila con `retf n`, usando las firmas de los archivos
  `.spec` de Wine;
- creación de una tarea Ring 3 con `CS:IP`, `SS:SP`, `DS` y `ES` de 16 bits;
- adaptación de la entrada por registros `KERNEL.InitTask`;
- memoria local, memoria global y selectores alias básicos;
- liberación de segmentos, relays y memoria al terminar el proceso;
- puente inicial de APIs KERNEL/USER/GDI, incluido `MessageBox` sobre la GUI
  que BlesKernOS ya tenía para Win32.

## Archivos principales

- `kernel/ne_loader.c`: cargador, relays y dispatcher Win16.
- `kernel/include/ne_loader.h`: interfaz de ejecución y cleanup.
- `kernel/gdt.c` y `kernel/include/gdt.h`: actualización y consulta de
  selectores dinámicos de 16 bits.
- `kernel/task.c` y `kernel/include/task.h`: contexto inicial de tareas Win16.
- `kernel/isr_stubs.asm`: conserva selectores Ring 3 dinámicos al volver de
  una interrupción, en vez de forzar siempre los selectores planos Win32.
- `kernel/syscall.c` y `kernel/include/syscall.h`: syscall `SYS_WIN16_RELAY`.
- `kernel/win32/user32.c` y `kernel/win32/win32.h`: exporta el MessageBox ya
  portado para poder reutilizarlo desde USER.EXE16.
- `third_party/wine/win16_exports.inc`: metadatos generados a partir de los
  `.spec` de Wine, mantenidos bajo LGPL-2.1-or-later.

## Flujo de una llamada Win16

1. El loader resuelve un import, por ejemplo `USER.MessageBox`.
2. La relocación recibe un puntero lejano hacia un pequeño relay de 16 bits.
3. El relay carga `SYS_WIN16_RELAY` y su identificador, y ejecuta `int 0x80`.
4. El kernel obtiene los argumentos desde la pila `SS:SP` de la tarea Win16.
5. Se ejecuta la implementación nativa o un stub conocido.
6. El relay vuelve con `retf n`, donde `n` sale de la firma oficial de Wine.

Un import desconocido puede quedar relocado y permitir que el ejecutable se
cargue, pero si el programa intenta llamarlo la tarea se aborta con un mensaje
de debug. Esto evita continuar con una pila corrupta o fingir una API que no
existe.

## APIs con implementación inicial

### KERNEL

- versión, tarea actual, cantidad de tareas, ticks y último error;
- `LocalAlloc`, `LocalReAlloc`, `LocalFree`, `LocalLock`, `LocalSize`;
- `GlobalAlloc`, `GlobalReAlloc`, `GlobalFree`, `GlobalLock`, `GlobalSize`;
- `lstrcpy`, `lstrcat`, `lstrlen`, `lstrcmp`, `lstrcmpi` y `OutputDebugString`;
- `MakeProcInstance` como identidad inicial de puntero lejano;
- selectores y alias básicos (`AllocSelector`, `AllocCStoDSAlias`,
  `AllocDStoCSAlias`, `FreeSelector`, `GetSelectorLimit`);
- `LoadLibrary` básico para KERNEL, USER y GDI;
- `GetWinFlags`, `InitTask` por registros y los `equate` que usan ejecutables reales de Windows 3.x.

### USER/GDI

- `MessageBox` reutiliza la implementación visual existente;
- varias APIs de arranque, mensajes, diálogos y dibujo tienen stubs con firma
  y limpieza de pila correctas para permitir avanzar durante el port.

## Validación realizada

- Compilación correcta, sin advertencias, de:
  - `kernel/ne_loader.c`;
  - `kernel/gdt.c`;
  - `kernel/task.c`;
  - `kernel/syscall.c`;
  - `kernel/win32/user32.c`.
- Auditoría estructural de los 30 ejecutables NE presentes en
  `tests/fixtures/disk-images/exes.img`.
- Verificación específica de imports de `WINVER.EXE` y `CONTROL.EXE`:
  sus imports KERNEL/USER reciben firmas conocidas y `KERNEL.178` se trata
  correctamente como `__WINFLAGS = 0x413`, no como una función.
- El build completo se inició, pero el entorno de creación del parche no tenía
  `nasm` ni `mkfs.fat`; por eso se detuvo antes de generar la imagen. Los cinco
  objetos C modificados sí se compilaron. `kernel/isr_stubs.asm` debe validarse
  con el build normal del proyecto antes de arrancar la imagen final.

## Límites actuales

Todavía faltan, entre otras piezas:

- loader real de DLL NE y sus entrypoints;
- recursos Win16, plantillas de diálogo y `LoadString` real;
- clases y ventanas USER16 completas;
- cola de mensajes, timers y callbacks 16:16;
- GDI16 conectado a objetos gráficos reales;
- semántica DOS3Call, archivos Win16 y thunking DOS;
- thunks de callbacks desde código nativo hacia procedimientos Win16;
- módulos adicionales como SHELL, COMMDLG, MMSYSTEM y WINSOCK;
- aislamiento de memoria equivalente a un entorno Windows/Wine completo.

Por eso el resultado correcto de esta etapa es: **el kernel ya puede cargar,
relocar, lanzar y despachar una aplicación NE de 16 bits**, pero aplicaciones
complejas todavía se detendrán al llegar a APIs no implementadas.

## Aplicación y prueba

Desde la raíz del source original:

```sh
git apply BlesKernOS-0.8-win16-wine-port.patch
make clean
make -j2
```

Conviene probar primero un NE pequeño y arrancar QEMU con COM1 visible. Cuando
una API pendiente sea llamada aparecerá un mensaje con módulo, nombre/ordinal,
tamaño de limpieza y PID; esa salida permite implementar las funciones una a
una sin adivinar el punto de fallo.

## Origen y licencia

La arquitectura de relays y las firmas Win16 se adaptaron consultando estos
archivos del árbol oficial de Wine:

- `dlls/krnl386.exe16/relay.c`;
- `dlls/krnl386.exe16/kernel.c`;
- `dlls/krnl386.exe16/krnl386.exe16.spec`;
- `dlls/user.exe16/user.exe16.spec`;
- `dlls/gdi.exe16/gdi.exe16.spec`.

Los metadatos derivados de esos `.spec` y las partes adaptadas de Wine se
mantienen bajo **LGPL-2.1-or-later**. El texto completo está en
`third_party/wine/COPYING.LIB`. Los avisos de terceros prevalecen sobre la
licencia general del proyecto para esos archivos y porciones concretas.
