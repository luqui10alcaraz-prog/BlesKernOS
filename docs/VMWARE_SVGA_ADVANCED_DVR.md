# VMware SVGA-II avanzado como `VMWARESVGA.DVR`

Este parche es **incremental**: se aplica sobre BlesKernOS 0.8 después de
`BlesKernOS-0.8-VMware-SVGA-II-2D.patch`.

## Aplicación

```bash
git apply --check BlesKernOS-0.8-SVGA-DVR-Advanced-2D-COMPAT.patch
git apply BlesKernOS-0.8-SVGA-DVR-Advanced-2D-COMPAT.patch
python3 tools/merge_svga_compositor.py
make -j2 build/bleskernos-ata-user.img
```

Prueba en QEMU:

```bash
make run-svga
```

El archivo instalado debe aparecer como:

```text
/SYSTEM/DRIVERS/VMWARESVGA.DVR
```

## Arquitectura

El código específico de VMware deja de enlazarse dentro de `kernel.bin`.
La implementación avanzada se guarda en `kernel/drivers/graphics/vmware_svga_dvr.c`;
el archivo `vmware_svga.c` del parche inicial queda intacto para no pisar
cambios locales, pero ya no forma parte de `KERNEL_SOURCES`.
Durante el arranque:

1. El kernel inicia con VGA/VESA para conservar el splash temprano.
2. El cargador recorre `/SYSTEM/DRIVERS`.
3. `VMWARESVGA.DVR` detecta PCI `15AD:0405` y registra un backend mediante
   `gfx_register_driver()`.
4. La segunda llamada a `gfx_init()` activa el backend de mayor prioridad.
5. Si el módulo falta o falla, BlesKernOS conserva VESA/VGA.

El kernel contiene únicamente la API genérica (`gfx_driver_ops_t`) y no conoce
registros ni comandos VMware.

## Funciones agregadas

### Cursor por hardware

- Define el cursor clásico SVGA mediante `SVGA_CMD_DEFINE_CURSOR`.
- Convierte el cursor ARGB de la GUI a máscara AND de 1 bit y máscara BGRX de
  32 bits.
- Usa cursor bypass del FIFO cuando existe; si no, usa los registros clásicos.
- El compositor deja de dibujar el puntero dentro del back buffer cuando los
  rastros están desactivados.
- Al cambiar de resolución se incrementa una generación del driver y el cursor
  se vuelve a definir automáticamente.

### Dirty rectangles y back buffer

- La GUI sigue dibujando un cuadro completo y coherente en RAM.
- El compositor entrega al driver sólo las regiones invalidadas.
- El driver recorta, agrupa rectángulos próximos y limita la cola a 32 regiones.
- En el camino compatible copia sólo esas regiones a VRAM y emite `UPDATE`.
- En el camino GMR copia sólo esas regiones al buffer de memoria invitada y
  emite `BLIT_GMRFB_TO_SCREEN`.

### Present y fences

- `gfx_present_buffer()` presenta una lista de regiones en un lote.
- `gfx_flush()` cierra el lote.
- `gfx_last_fence()` y `gfx_wait_fence()` permiten esperar trabajo pendiente.
- Si el FIFO anuncia fences, usa `SVGA_CMD_FENCE`.
- Si no los anuncia, usa el mecanismo síncrono `SYNC/BUSY` como fallback.

### Superficies off-screen

- Hasta 32 superficies con handles generacionales.
- Reserva first-fit sobre la VRAM libre después del framebuffer visible.
- Pitch alineado a 64 bytes.
- Creación, destrucción, upload parcial y blit a pantalla.
- Si existen screen objects, el host hace el blit desde la superficie VRAM.
- Sin screen objects, el driver conserva un fallback por CPU más `UPDATE`.

La API permite que programas, caches de iconos y futuras rutas del compositor
mantengan imágenes o ventanas en VRAM. El parche **no migra automáticamente
cada ventana actual a una superficie independiente**, porque el compositor
existente mantiene un único back buffer de RAM para resolver correctamente
clipping, solapamiento y orden Z.

### GMR2

La ruta GMR se activa únicamente cuando el dispositivo anuncia simultáneamente:

- capacidad de dispositivo GMR2;
- capacidad FIFO GMR2;
- screen objects.

El driver:

- reserva memoria alineada a páginas;
- define y remapea el GMR2 con PPN de 32 bits;
- define el screen object primario;
- define el GMR framebuffer;
- presenta regiones con `BLIT_GMRFB_TO_SCREEN`;
- espera el fence anterior antes de reutilizar el buffer;
- destruye screen/GMR y libera memoria de forma ordenada al cambiar de modo.

### BitBlt

API disponible:

```c
gfx_bitblt(src_x, src_y, dst_x, dst_y, w, h, rop, &fence);
```

Operaciones:

- `GFX_ROP_COPY`
- `GFX_ROP_XOR`
- `GFX_ROP_AND`
- `GFX_ROP_OR`
- `GFX_ROP_INVERT`

`COPY` usa `RECT_COPY` cuando el host lo anuncia. Las ROP usan
`RECT_ROP_COPY` cuando existe la capacidad correspondiente. Todas mantienen
fallback software con tratamiento correcto de rectángulos solapados.

## Compatibilidad esperada

| Función | QEMU `-vga vmware` | VMware con capacidades modernas |
|---|---|---|
| Driver `.DVR` | Sí | Sí |
| Back buffer y dirty rectangles | Sí | Sí |
| Cursor hardware clásico | Sí | Si se anuncia cursor |
| `RECT_COPY` | Hardware | Si se anuncia |
| ROP XOR/AND/OR | Fallback software | Hardware si se anuncia raster-op |
| Fences reales | Normalmente no; `SYNC/BUSY` | Si FIFO anuncia fence |
| Superficies en VRAM | Sí | Sí |
| Blit host desde superficie | No en el modelo QEMU legado | Si anuncia screen objects |
| GMR2 | No en el modelo QEMU legado | Si anuncia GMR2 + screen objects |

El módulo consulta capacidades en tiempo de ejecución. No envía comandos GMR,
screen-object, fence o ROP a un host que no los haya anunciado.

## Diagnóstico dentro de BlesKernOS

```text
video
```

Muestra el driver y sólo las capacidades activas. Posibles etiquetas:

```text
backbuffer dirty-rects cursor-hw fences surfaces-vram gmr2
bitblt fill copy-hw rop-hw surface-blit-hw
```

Pruebas:

```text
gfx rect 40 40 220 120 4
gfx copy 40 40 300 200 220 120
gfx rop xor 40 40 300 200 220 120
gfx fence
gfx surface
```

Con los rastros del mouse desactivados, mover el puntero no debería invalidar
ni copiar el contenido de las ventanas.

## Validación realizada

- Compilación de todos los C modificados con el toolchain freestanding del
  proyecto.
- Compilación adicional de `vmware_svga.c` y `compositor.c` con
  `-Wall -Wextra -Werror`.
- `VMWARESVGA.DVR` resultante: ELF i386 relocatable.
- Todos sus símbolos externos están presentes en la tabla de exports del
  cargador ELF.
- `git diff --check` sin errores.
- Empaquetador FAT32 validado con `python3 -m py_compile`.

No se pudo generar ni arrancar la imagen completa en el entorno de creación del
parche porque no están instalados `nasm`, `mkfs.fat` ni QEMU. La prueba real de
arranque debe hacerse en tu entorno habitual.


## Compatibilidad con compositores modificados

Este parche no contiene un diff tradicional de `gui/compositor.c`. Después de
aplicarlo, `tools/merge_svga_compositor.py` localiza las funciones y variables
por nombre, inserta únicamente la ruta de cursor hardware y crea
`gui/compositor.c.pre-svga-advanced` antes de escribir. Si no reconoce la
arquitectura, aborta sin tocar el archivo.
