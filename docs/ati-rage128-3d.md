# ATI Rage 128: backend 3D experimental

## Alcance

`ATIR1283D.DVR` es una extensión externa del backend gráfico de BlesKernOS
para la ATI Rage Mobility M3/LF (`1002:4C46`). No reemplaza a
`ATIR128.DVR`: el driver 2D conserva el control del modo de video, scanout,
cursor y BitBlt; el módulo 3D sólo registra la ABI `GFX3D` cuando la ATI ya
está activa en un framebuffer lineal de 32 bpp.

La primera etapa implementa una ruta real de hardware mediante el Concurrent
Command Engine (CCE/PM4) en modo `192PIO`. Al usar PIO no necesita AGP, PCI
GART, DMA ring, IRQ ni memoria físicamente contigua del sistema.

## Funciones implementadas

- Detección exacta de la Mobility M3/LF PCI `1002:4C46`.
- Carga del microcódigo CCE de ATI usado por el controlador histórico r128.
- Envío de paquetes PM4 tipo 0 y tipo 3 por el FIFO PIO.
- Render targets XRGB/ARGB8888 alojados fuera del framebuffer visible, en VRAM.
- Listas de triángulos transformados `x, y, z, rhw`.
- Color difuso ARGB interpolado por Gouraud.
- Alpha blending `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`.
- Presentación y lectura síncronas para integrarse con la ABI GFX3D y TinyGL.
- Autoprueba que dibuja un triángulo en VRAM y verifica que cambien píxeles.
- Esperas acotadas; ante timeout se detiene el CCE, se reinicia el motor y el
  backend se desactiva para que TinyGL continúe por CPU.

## Z buffer

El código contiene una ruta Z16 experimental, pero viene desactivada mediante:

```c
#define R1283D_EXPERIMENTAL_Z16 0U
```

La razón es deliberada: la ruta de color y el transporte CCE deben validarse
primero en la Mobility M3 física. Mientras el interruptor sea `0`, el driver no
anuncia `GFX3D_CAP_DEPTH_BUFFER`; escenas que necesiten Z o texturas vuelven al
rasterizador de CPU sin desactivar el resto del backend.

Después de que `gfx3d test` y varios triángulos sin profundidad funcionen de
forma estable, se puede cambiar a `1U`, recompilar y probar Z16 por separado.
Esa segunda etapa sigue siendo experimental.

## Funciones todavía no implementadas

- Texturas y multitextura.
- Mipmapping y filtros de textura.
- Stencil, fog y antialiasing.
- AGP/PCI-GART, DMA ring e interrupciones.
- Page flipping y presentación acelerada.
- Validación de comandos para procesos Ring 3.
- Un controlador OpenGL completo o shaders programables.

TinyGL conserva matrices, iluminación, clipping y armado de primitivas. El
backend sólo recibe triángulos ya transformados. Cuando una operación no está
soportada, la imagen se resuelve desde VRAM y TinyGL sigue por CPU.

## Requisitos

1. `ATIR128.DVR` debe estar activo.
2. El modo de video debe ser 32 bpp.
3. Debe quedar VRAM libre después del framebuffer visible. En una tarjeta de
   8 MiB, 800x600x32 deja espacio suficiente para varios render targets.
4. El archivo `/SYSTEM/DRIVERS/ATIR1283D.DVR` debe estar presente.

En 8 bpp el módulo se registra, pero `probe()` devuelve no disponible y no toca
el motor 3D.

## Prueba

En la terminal:

```text
gfx3d info
gfx3d test
savecom1 /usb0/ATIR3DLOG.TXT
```

Salida esperada:

```text
[ATIR1283D.DVR] extension registrada; espera ATIR128 32bpp
[ATIR1283D:TRACE] PCI 1002:4C46 ...
[ATIR1283D:TRACE] selftest OK sample=0x...
[ATIR1283D.DVR] CCE 192PIO activo ...
GFX3D activo: driver=ati_rage128_3d transporte=CCE PM4 192PIO
Prueba GFX3D completada correctamente
```

Si aparece `TIMEOUT`, el módulo se desactiva y no debe seguir probándose hasta
revisar el log. El driver 2D y el fallback de TinyGL permanecen disponibles.

## Fuentes técnicas

- ATI, *RAGE 128 Software Development Manual*, SDK-G04000 Rev. 0.01.
- ATI, *RAGE 128 Register Reference Guide*.
- Linux DRM histórico: `r128_cce.c`, `r128_state.c`, `r128_drv.h`.
- X.Org/X11Libre: `r128_reg.h` y `r128_exa_render.c`.
- Mesa DRI histórico: frontend r128 usado como referencia de estados y
  compatibilidad OpenGL fija.

El código de integración, el asignador de VRAM y la adaptación a GFX3D son
implementaciones propias de BlesKernOS. El microcódigo y las definiciones
reutilizadas conservan su aviso MIT/X11 en el árbol.
