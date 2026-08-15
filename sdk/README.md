# SDK nativo de BlesKernOS

Este SDK genera aplicaciones ELF32 relocatables que se comunican con el
kernel exclusivamente mediante `int 0x80`. Una aplicación sólo incluye
`sdk/include/bleskernos.h`; no debe incluir archivos de `kernel/`, `gui/` ni
drivers.

El punto de entrada actual es:

```c
void bleskernos_program_main(void *unused);
```

Compilación manual:

```sh
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostdinc -fno-pic \
  -fno-pie -fno-stack-protector -Isdk/include -c app.c -o app.raw.o
ld -m elf_i386 -r app.raw.o build/sdk/libblesk.a -o app.o
```

`app.o` puede copiarse a `/SYSTEM/PROGRAMS`. La syscall ABI 3 ofrece consola,
archivos, directorios, memoria, tiempo, administración de procesos, resolución
DNS y sockets TCP cliente. Las llamadas nuevas son `bk_dns_resolve`,
`bk_socket`, `bk_connect`, `bk_send`, `bk_recv`, `bk_socket_close` y el cliente
de alto nivel `bk_http_get` (acepta `http://` y `https://`); cada proceso
dispone de hasta cuatro sockets.

## API de aplicaciones nativas

Los programas y comandos ET_REL que necesitan servicios de escritorio o de
administración incluyen `sdk/include/bleskernos_api.h`. Esta API v23 también se
ejecuta en Ring 3 y el cargador transforma sus símbolos en transiciones
controladas al kernel.

La API entrega estructuras públicas por copia (`bk_block_info_t`,
`bk_partition_info_t`, `bk_volume_info_t`, `bk_volume_check_report_t`,
`bk_pci_info_t` y `bk_process_info_t`). Una aplicación no
debe incluir `kernel/include/api.h` ni recibir `block_device_t`, `fat_fs_t`,
`pci_device_t`, estructuras de drivers o punteros internos equivalentes.

`bk_device_check_volume()` ejecuta un análisis FAT de sólo lectura. El informe
público permite consultar copias FAT divergentes, clusters perdidos, cruzados,
malos o reservados, cadenas circulares o inválidas y tamaños incoherentes sin
exponer al programa las estructuras internas del controlador. El mismo informe
incluye archivos fragmentados, cantidad de fragmentos y la mayor extensión
libre. `bk_device_partition_count()` y `bk_device_partition_info()` permiten
consultar las cuatro entradas primarias de cada MBR sin dar acceso crudo al
dispositivo.

`bk_device_repair_volume()` aplica la reparación conservadora usada por
`checkdisk /fix` y ScanDisk. Antes de escribir exige que el árbol completo de
directorios sea legible; sincroniza las FAT, repara la copia de arranque FAT32,
corta cadenas dañadas y libera clusters demostrablemente huérfanos. Devuelve un
informe separado con cada cambio y vuelve a analizar el volumen al terminar.

La misma cabecera declara una GUI Ring 3 de objetos opacos. Las aplicaciones
pueden crear ventanas y dibujar superficies mediante `bk_gui_*` sin incluir
`gui/gui.h` ni conocer la representación interna de ventanas y escritorios.

La API v22 agrega el catálogo compartido `/SYSTEM/GRAPHICS.PAK`.
`bk_graphics_icon_load("Folder", &image)` carga un recurso por nombre,
`bk_graphics_icon_count()` y `bk_graphics_icon_name()` permiten enumerarlo, y
`bk_gui_image_free()` libera la imagen decodificada. El archivo
`assets/graphics/GRAPHICS.CSV` contiene el catálogo disponible en la imagen
oficial.

La API v23 agrega `bk_gui_widget_set_icon(button, "FolderOpen")`: el botón
estándar conserva el recurso y lo libera automáticamente al reemplazarlo o
destruirse. Esto sirve para acciones como abrir, copiar, pegar, guardar,
actualizar y eliminar sin duplicar imágenes dentro del programa.

La API v24 agrega cursores gráficos temporales por proceso. Una aplicación puede
usar `bk_gui_cursor_set_resource("Brush", 5, 27)` para activar un cursor de
`GRAPHICS.PAK` y `bk_gui_cursor_reset()` para volver a la flecha. El escritorio
restaura automáticamente el cursor al cerrar el proceso que lo solicitó. Los
recursos admitidos miden hasta 32x32 píxeles y conservan transparencia alfa.

La API v30 agrega avisos modales compartidos. `bk_gui_error()` muestra un fallo
normal, `bk_gui_network_error()` aplica la presentacion de errores de red y
`bk_gui_alert()` permite elegir `BK_ALERT_INFO`, `BK_ALERT_WARNING`,
`BK_ALERT_ERROR` o `BK_ALERT_NETWORK`, junto con un codigo de diagnostico.

Ejemplo de enumeración de discos:

```c
#include <bleskernos_api.h>

void bleskernos_program_main(void *unused) {
    bk_block_info_t disk;
    (void)unused;
    for (uint32_t i = 0; i < bk_device_block_count(); i++) {
        if (bk_device_block_info(i, &disk))
            bk_console_printf("%s: %u sectores\n", disk.name,
                              disk.sector_count);
    }
    bk_proc_exit();
}
```

## TinyGL

El SDK publica TinyGL sin depender de headers privados del kernel:

- `include/TGL/gl.h`: API OpenGL-compatible de TinyGL.
- `include/tinygl/zbuffer.h`: API de framebuffer de bajo nivel.
- `include/bleskernos_tinygl.h`: contexto y presentación adaptados a BlesKernOS.
- `libblesk_tinygl.a`: TinyGL más la capa de integración del SDK.
- `examples/tinygl_triangle.c`: ejemplo mínimo.

Compilación típica:

```sh
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostdinc -Os \
  -fno-pic -fno-pie -fno-stack-protector -Isdk/include \
  -c app.c -o app.raw.o
ld -m elf_i386 -r app.raw.o build/sdk/libblesk_tinygl.a \
  build/sdk/libblesk.a -o app.o
```

`bk_tinygl_create()` crea el ZBuffer y el buffer lineal; después de dibujar,
`bk_tinygl_present()` actualiza los píxeles que la aplicación puede copiar a
su superficie GUI.


## Mesa 3.5 / GFX3D

Mesa 3.5 se compila como una segunda implementación OpenGL, independiente de
TinyGL. El backend de BlesKernOS está insertado en OSMesa/TNL: Mesa conserva
estado, matrices, iluminación y clipping, mientras GFX3D rasteriza el
subconjunto del viewport de 3D Plus.

Archivos publicados por el SDK:

- `include/GL/gl.h`, `include/GL/glext.h` y `include/GL/osmesa.h`.
- `include/bleskernos_mesa.h`: contextos, renderer estricto, presentación directa y estadísticas.
- `libblesk_mesa.a`: Mesa 3.5, OSMesa y el backend GFX3D.
- `examples/mesa_triangle.c`: prueba mínima AUTO.
- `examples/mesa_gpu_strict_test.c`: stress GPU-only de 10.000 frames.

Compilación típica:

```sh
gcc -m32 -mfpmath=387 -mno-sse -mno-sse2 -ffreestanding -fno-builtin \
  -nostdlib -nostdinc -Os -fno-pic -fno-pie -fno-stack-protector \
  -Isdk/include -c app.c -o app.raw.o
ld -m elf_i386 -r app.raw.o build/sdk/libblesk_mesa.a \
  build/sdk/libblesk.a -o app.o
```

`BK_MESA_RENDERER_GPU_STRICT` convierte cualquier fallback inesperado en un
error consultable con `bk_mesa_strict_failed()` y
`bk_mesa_last_fallback_string()`. `bk_mesa_present_gpu()` conserva el resultado
en VRAM y `bk_mesa_attach_to_window()` lo conecta al compositor sin readback.
La ABI GFX3D v3 sincroniza color y Z16 cuando los modos AUTO/GPU alternan con
SWRAST.

Para probar aceleración real hay que comprobar que `draw_calls` y `triangles`
aumenten y que `fallbacks`, `strict_failures` y `downloads` permanezcan en cero.
La tabla completa de estados acelerados y limitaciones está en
`docs/mesa35.md`.

Mesa y TinyGL exportan los mismos símbolos OpenGL (`glBegin`, `glClear`, etc.).
Por eso una aplicación debe enlazar **una sola** de las dos bibliotecas. El
sistema puede distribuir ambas sin conflicto porque cada programa ET_REL lleva
la implementación elegida dentro de su propio objeto enlazado.
