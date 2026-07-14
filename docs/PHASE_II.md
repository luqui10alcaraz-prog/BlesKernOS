# Fase II: user-space

BlesKernOS implementa la base de la Fase II mediante una ABI estable de
syscalls. Las aplicaciones nuevas pueden ejecutarse en Ring 3 sin incluir ni
enlazar headers o símbolos de drivers, GUI o kernel.

## Componentes

- Ring 3 mediante selectores de usuario, TSS y puerta `int 0x80` con DPL 3.
- Cargador ELF32 `ET_REL` y cargador PE/Win32.
- ABI de syscalls versión 2 para consola, VFS, directorios, memoria, tiempo y
  procesos (`spawn`, `waitpid`, `kill`, PID/PPID y código de salida).
- Validación central de rangos y cadenas recibidos desde user-space.
- Descriptores y asignaciones con propietario por proceso y limpieza al
  recolectar el último hilo del proceso.
- SDK autocontenido en `sdk/`, sin dependencias de headers internos.
- Aplicación de prueba `/SYSTEM/PROGRAMS/phase2.o`, enlazada por completo con
  `libblesk.a` y sin símbolos externos del kernel.
- Proxy automático del cargador: las referencias externas de todos los ELF
  nativos se convierten en stubs Ring 3 que entran por `int 0x80`; los drivers
  y módulos residentes conservan enlaces directos Ring 0.
- Puente compatible con funciones C `cdecl` y Win32 `stdcall`, incluyendo la
  limpieza de argumentos realizada por `ret N`.
- Upcalls Ring 3 para pintura, eventos, menús, widgets, salvapantallas y
  WndProc Win32. El compositor ya no invoca código de aplicación en CPL 0.
- Autoprueba `/SYSTEM/PROGRAMS/ring3proxy.o`; durante el arranque debe aparecer
  `[RING3] proxy API y retorno a user-space: OK`.
- Shell existente y lanzamiento de programas desde VFS.

## Modelo de compatibilidad

Las aplicaciones nativas antiguas que usan la API gráfica v3 siguen
funcionando, pero sus llamadas externas ahora cruzan el proxy de syscalls. El
formato recomendado para programas nuevos continúa siendo el SDK de syscalls.
El siguiente paso de endurecimiento es añadir paging y espacios de direcciones
separados; no cambia la ABI 2 ni el modelo Ring 3 implementado aquí.

## Verificación

```sh
make sdk
make -j2
nm -u build/programs/phase2.o
```

El último comando no debe imprimir símbolos.
