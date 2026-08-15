# API de aplicaciones de BlesKernOS

La ABI publica actual es la version 30. Las aplicaciones nativas deben incluir
`sdk/include/bleskernos_api.h` y llamar simbolos con prefijo `bk_`. Cabeceras como
`pit.h`, `task.h`, `vfs.h`, `mouse.h` y `sound.h` describen la implementacion
del kernel y no son una interfaz estable para aplicaciones.

`api.h` incluye tambien la fachada fuente de `api_compat.h`. Esta publica con
nombres `bk_gui_*`, `bk_device_*`, `bk_runtime_*`, `bk_app_*` y
`bk_screensaver_*` las operaciones que antiguamente obligaban a incluir GUI o
drivers. Las aplicaciones nunca deben incluir `api_compat.h` directamente.

## Recursos gráficos compartidos (API 22 y 23)

Las aplicaciones pueden cargar iconos de `/SYSTEM/GRAPHICS.PAK` sin analizar
el formato del paquete ni copiar PNG privados. `bk_graphics_icon_load()` crea
un `bk_gui_image_t`, que debe liberarse con `bk_gui_image_free()`.
`bk_graphics_icon_count()` y `bk_graphics_icon_name()` permiten descubrir los
975 nombres instalados. Consulte `docs/GRAPHICS_PAK.md` para el formato,
ejemplos y atribución.

Desde la API 23, `bk_gui_widget_set_icon(widget, "Save")` asigna directamente
un recurso a un botón estándar. El widget toma posesión de los píxeles,
reemplaza y libera el icono anterior, y también lo libera al destruirse. Pasar
`NULL` o una cadena vacía quita el icono. Los toolbars dibujados por una
aplicación pueden seguir usando `bk_graphics_icon_load()` y
`bk_gui_surface_draw_image()`.

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
- `bk_gui_alert`, `bk_gui_error` y `bk_gui_network_error`: avisos modales
  uniformes del sistema para informacion, advertencias y fallos.
- `bk_gfx_*` y `bk_input_*`: pantalla, primitivas graficas, teclado y mouse.
- `bk_sound_*` y `bk_time_*`: PCM, tonos y fecha/hora.
- `bk_proc_*`: snapshots publicos de procesos, cierre, argumentos, memoria,
  asociacion de ventanas e hilos del proceso actual.
- `bk_app_launch`: ejecucion de otra aplicacion ELF o PE por ruta.
- `bk_net_*`: estado publico del adaptador, DHCP, resolucion, ping y sockets.
- `bk_clipboard_*`: portapapeles global de texto compartido entre procesos.
- `bk_device_*`: enumeracion controlada de bloques, PCI y drivers, lectura de
  bloques y refresco de medios para herramientas de administracion.
- `bk_image_*`, `bk_desktop_*` y `bk_screensaver_*`: recursos graficos y
  preferencias del escritorio sin depender de sus modulos internos.

Los tipos `bk_proc_info_t` y similares son copias publicas. No conservan
punteros a estructuras mutables del kernel. Desde la ABI 14, `memory_bytes`
combina stacks, asignaciones reales del heap atribuidas al proceso y la pista
de memoria declarada por la aplicacion.

## Avisos y errores globales (API 30)

Las aplicaciones no necesitan construir una ventana propia para comunicar un
fallo. El escritorio copia el titulo y el mensaje, bloquea la interaccion con
las ventanas inferiores y permite cerrar el aviso con `OK`, la cruz, Enter o
Escape:

```c
bk_gui_error("No se pudo guardar", "El disco no tiene espacio disponible.");
bk_gui_network_error("conectar con el servidor", -1002);
bk_gui_alert(BK_ALERT_WARNING, "Archivo modificado",
             "Los cambios todavia no fueron guardados.", 0);
```

Los errores del cargador de programas, las excepciones de una aplicacion y los
fallos de las operaciones principales de red usan el mismo sistema
automaticamente. Para fallos propios de una aplicacion se recomienda un titulo
breve, una accion que el usuario pueda intentar y un codigo distinto de cero
solamente cuando ayude al diagnostico.

## Presentacion durante operaciones bloqueantes

La ABI 14 permite actualizar una ventana desde el mismo callback que esta
realizando una operacion sincrona:

```c
bk_gui_surface_t *surface = NULL;
if (bk_gui_window_begin_immediate_paint(window, &surface)) {
    paint_content(window, surface, context);
    bk_gui_window_end_immediate_paint(window, surface);
}
```

Debe usarse con moderacion para progreso de red, copias o tareas similares. La
funcion final captura el cliente y presenta el cache sin ejecutar un callback
Ring 3 anidado.

## Decodificacion de imagenes

La ABI 13 expone decodificadores en memoria para BMP, GIF, PNG y JPEG baseline:

```c
bool bk_gui_image_decode_bmp(bk_gui_image_t *image, const void *data, uint32_t length);
bool bk_gui_image_decode_gif(bk_gui_image_t *image, const void *data, uint32_t length);
bool bk_gui_image_decode_png(bk_gui_image_t *image, const void *data, uint32_t length);
bool bk_gui_image_decode_jpeg(bk_gui_image_t *image, const void *data, uint32_t length);
void bk_gui_image_free(bk_gui_image_t *image);
```

Las funciones asignan el buffer de pixeles de `image`; la aplicacion debe liberarlo
con `bk_gui_image_free()`. JPEG admite por ahora imagenes baseline en escala de
gris o YCbCr, no JPEG progresivo ni CMYK.

## Ejemplo minimo

```c
#include "kernel/include/api.h"

void bleskernos_program_main(gui_desktop_t *desktop) {
    gui_window_t *window;

    if (bk_sys_api_version() < 18 ||
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
