# API de aplicaciones de BlesKernOS

La ABI publica actual es la version 3. Las aplicaciones nativas deben incluir
`kernel/include/api.h` y llamar simbolos con prefijo `bk_`. Cabeceras como
`pit.h`, `task.h`, `vfs.h`, `mouse.h` y `sound.h` describen la implementacion
del kernel y no son una interfaz estable para aplicaciones.

`api.h` incluye tambien la fachada fuente de `api_compat.h`. Esta publica con
nombres `bk_gui_*`, `bk_device_*`, `bk_runtime_*`, `bk_app_*` y
`bk_screensaver_*` las operaciones que antiguamente obligaban a incluir GUI o
drivers. Las aplicaciones nunca deben incluir `api_compat.h` directamente.

## Compatibilidad

`bk_sys_api_version()` devuelve la version de ABI y
`bk_sys_capabilities()` devuelve una mascara de `BK_API_CAP_*`. Las versiones
nuevas conservan las funciones de versiones anteriores. Una aplicacion debe
comprobar al arrancar la version minima que necesita.

## Areas cubiertas

- `bk_sys_*`: version, capacidades, log, PID, yield, espera por ticks o
  milisegundos, reloj monotono, memoria y estadisticas de memoria.
- `bk_file_*`: descriptores, lectura/escritura completa, directorios, cwd,
  mkdir, borrado, renombrado y espacio libre.
- `bk_gui_*`: escritorio, ventanas, menus, estado y geometria de ventanas,
  invalidacion y dibujo sobre superficies.
- `bk_gfx_*` y `bk_input_*`: pantalla, primitivas graficas, teclado y mouse.
- `bk_sound_*` y `bk_time_*`: PCM, tonos y fecha/hora.
- `bk_proc_*`: snapshots publicos de procesos, cierre, argumentos, memoria,
  asociacion de ventanas e hilos del proceso actual.
- `bk_app_launch`: ejecucion de otra aplicacion ELF o PE por ruta.
- `bk_device_*`: enumeracion controlada de bloques, PCI y drivers, lectura de
  bloques y refresco de medios para herramientas de administracion.
- `bk_image_*`, `bk_desktop_*` y `bk_screensaver_*`: recursos graficos y
  preferencias del escritorio sin depender de sus modulos internos.

Los tipos `bk_proc_info_t` y similares son copias publicas. No conservan
punteros a estructuras mutables del kernel.

## Ejemplo minimo

```c
#include "kernel/include/api.h"

void bleskernos_program_main(gui_desktop_t *desktop) {
    gui_window_t *window;

    if (bk_sys_api_version() < 3 ||
        !(bk_sys_capabilities() & BK_API_CAP_GUI))
        return;

    window = bk_gui_create_window(desktop, 80, 60, 320, 180, "Mi app");
    if (!window) return;

    bk_gui_window_set_owner(window, bk_sys_getpid());
    bk_proc_bind_window(window);
    while (bk_gui_window_is_open(window) && !bk_proc_exit_requested())
        bk_sys_sleep_ms(10);

    bk_proc_bind_window(NULL);
    bk_gui_destroy_window(desktop, window);
}
```

`programs/apitest.c` es el ejemplo ejecutable. Su objeto solo depende de
funciones `bk_*` y de la biblioteca C; no usa simbolos directos de PIT, VFS,
memoria, tareas o GUI interna.

## Regla para extender la API

Una funcion publica nueva requiere cuatro cambios: declaracion en `api.h`,
implementacion en `kernel/api.c`, exportacion en `kernel/elf_loader.c` y una
prueba o uso en `programs/apitest.c`. La version se incrementa cuando se
agregan funciones o tipos; una ruptura incompatible requiere una ABI nueva.

Los alias de compatibilidad se declaran exclusivamente en `api_compat.h` y
siempre deben comenzar con `bk_`. No se aceptan llamadas `gui_*`, `task_*`,
`vfs_*` o a funciones de drivers dentro de una aplicacion.
