# TinyGL + GFX3D en BlesKernOS

Este port mantiene TinyGL como frontend OpenGL de funciones fijas: matrices,
iluminacion, clipping, listas, arrays y ensamblado de primitivas siguen en la
biblioteca. El rasterizado puede ejecutarse por software o enviarse al backend
GFX3D cuando un driver compatible expone `GFX3D_CAP_TINYGL`.

## Seleccion del renderer

El SDK 2 ofrece tres modos:

```c
bk_tinygl_context_t *gl = bk_tinygl_create_ex(
    640, 480, BK_TINYGL_RENDERER_AUTO);
```

- `BK_TINYGL_RENDERER_AUTO`: usa GFX3D si esta disponible; de lo contrario CPU.
- `BK_TINYGL_RENDERER_SOFTWARE`: fuerza el rasterizador de TinyGL.
- `BK_TINYGL_RENDERER_GPU`: solicita GFX3D, pero conserva fallback a CPU si el
  driver no existe o no anuncia la capacidad necesaria.

El renderer efectivo se consulta con:

```c
bk_tinygl_renderer_t active = bk_tinygl_active_renderer(gl);
const char *name = bk_tinygl_renderer_name(gl);
```

Tambien se puede cambiar durante la ejecucion:

```c
bk_tinygl_set_renderer(gl, BK_TINYGL_RENDERER_SOFTWARE);
```

El cambio resuelve primero cualquier frame pendiente para no perder pixeles.

## Ruta acelerada

La ruta GFX3D acelera:

- triangulos con sombreado suave o plano;
- triangulos texturados;
- lineas, incluyendo `GL_LINE` en `glPolygonMode`;
- puntos y tamanos configurados con `glPointSize`;
- prueba y escritura de profundidad;
- blending alfa estandar `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`;
- filtros `GL_NEAREST` y `GL_LINEAR`;
- direccionamiento `GL_REPEAT` y `GL_CLAMP`;
- cache LRU de 16 texturas para evitar recrearlas al cambiar de material.

Las combinaciones que el backend no representa se resuelven al framebuffer y
continuan por software. La interfaz publica no depende de VMware: actualmente
`VMWARESVGA3D.DVR` es el driver que implementa la capacidad, pero otros drivers
pueden implementar el mismo contrato GFX3D.

## Estadisticas

```c
bk_tinygl_gpu_stats_t stats;
bk_tinygl_gpu_get_stats(&stats);
```

Los contadores incluyen frames iniciados/resueltos, triangulos, lineas, puntos,
draw calls, cargas de texturas y fallbacks. Se reinician con
`bk_tinygl_gpu_reset_stats()`.

## Presentacion y limite actual

`bk_tinygl_present()` sigue entregando un buffer lineal de CPU para que las
aplicaciones existentes puedan blitearlo con la API grafica normal y, por eso,
descarga el frame acelerado. Para el camino interactivo, el compositor puede
presentar directamente una superficie GFX3D mediante
`bk_tinygl_present_gpu()` y `bk_gui_window_set_gpu_viewport()`. El readback
GPU -> RAM queda reservado para capturas, exportaciones y operaciones CPU.

## Prueba rapida

```sh
make -j"$(nproc)" build/system/libs/tinygl/tinygl.a \
    build/sdk/libblesk_tinygl.a \
    build/system/drivers/VMWARESVGA3D.DVR \
    build/kernel.bin

make -C "programas extras/3D-Plus-Standalone" clean all \
    BLESKERNOS_ROOT=../..
```

En QEMU/VMware debe cargarse primero el driver grafico 2D VMware SVGA y despues
`VMWARESVGA3D.DVR`. Sin el driver 3D, el mismo binario funciona por CPU.
