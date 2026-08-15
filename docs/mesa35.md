# Mesa 3.5 con aceleración GFX3D en BlesKernOS

## Arquitectura

Este port compila el Mesa 3.5 de `libs/Mesa-3.5` como una biblioteca
freestanding para aplicaciones ELF32. El backend
`src/BlesKernOS/bkmesa_gpu.c` se integra al pipeline TNL/OSMesa real de Mesa;
no reemplaza la API por un wrapper reducido.

```text
Aplicación OpenGL 1.2
        |
Mesa 3.5: estado, matrices, luces, clipping y ensamblado (CPU)
        |
        +-- viewport soportado --> GFX3D --> VMware/VirGL/Rage 128
        |
        `-- estado no soportado --> SWRAST (AUTO/GPU solamente)
```

La transformación e iluminación fija permanecen en CPU. GFX3D recibe vértices
y se ocupa de la parte costosa por píxel: rasterización, Z, texturas y mezcla.

## Salidas del build

```text
build/system/libs/mesa35/mesa35.a
build/sdk/libblesk_mesa.a
```

La imagen ATA/USB instala la biblioteca base en
`/SYSTEM/LIBS/MESA35/MESA35.A`. Mesa y TinyGL pueden estar instaladas a la vez,
pero una aplicación debe enlazar sólo una porque ambas exportan símbolos `gl*`.

## Subconjunto GPU para el viewport de 3D Plus

La primera versión completa del viewport acelera:

- Triángulos, quads, strips y fans terminados como triángulos.
- Puntos y líneas convertidos a geometría de pantalla.
- `GL_FILL`, `GL_LINE` y `GL_POINT` mediante `glPolygonMode`.
- Flat y Gouraud; hasta ocho luces del pipeline fijo calculadas por Mesa/CPU.
- Depth test y depth write con `NEVER`, `LESS`, `EQUAL`, `LEQUAL`, `GREATER`,
  `NOTEQUAL`, `GEQUAL` y `ALWAYS`.
- Superficie Z16 sincronizable entre GFX3D y el depth buffer de Mesa.
- Una textura 2D por pasada, `MODULATE` o `REPLACE`.
- `NEAREST`, `LINEAR`, `NEAREST_MIPMAP_NEAREST` y
  `LINEAR_MIPMAP_NEAREST`. Mesa estima el LOD por triángulo y el backend usa
  una superficie GFX3D por nivel. No hay filtrado trilineal.
- `REPEAT`, `CLAMP` y `CLAMP_TO_EDGE`.
- Blending alfa normal `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`.
- Blending aditivo `SRC_ALPHA / ONE`.
- Scissor durante dibujo, implementado recortando puntos, líneas y polígonos
  antes de enviarlos al driver.
- Fog lineal. Mesa calcula el factor por vértice y la GPU interpola el color.
- Polygon offset para overlays y wireframe.
- Batching de hasta 3072 vértices.
- Caché LRU de hasta 64 imágenes/niveles de textura.
- `glTexSubImage2D` con actualización regional cuando el driver anuncia
  `GFX3D_CAP_TEXTURE_REGION_UPLOAD`.
- Fences, subida/descarga de color y subida/descarga de Z16.

## Depth híbrido correcto

La ABI GFX3D v3 agrega:

```c
gfx3d_depth_upload();
gfx3d_depth_download();
gfx3d_surface_upload_region();
```

Los backends VMware SVGA3D, VirtIO/VirGL y ATI Rage 128 implementan una copia
Z16 asociada al render target. Al pasar de GPU a SWRAST, Mesa resuelve color y
profundidad; al regresar a GPU, vuelve a subir ambos. Esto elimina la antigua
restricción que bloqueaba el resto del frame en software después del primer
fallback.

La sincronización híbrida cuantiza a Z16. Para resultados exactos y menor
consumo conviene crear los contextos híbridos con `depth_bits=16`.

## Modo GPU estricto

```c
bk_mesa_context_t *ctx = bk_mesa_create_ex_renderer(
    width, height, 16, 0, 0, BK_MESA_RENDERER_GPU_STRICT);
```

En este modo Mesa no oculta un fallback con SWRAST. El backend:

- marca el contexto como fallido;
- incrementa `strict_failures`;
- registra la causa por el log del sistema;
- hace que `bk_mesa_present()` y `bk_mesa_present_gpu()` devuelvan `false`;
- impide que `bk_mesa_gpu_surface()` entregue una superficie como si el frame
  fuese válido.

OpenGL 1.2 no permite que `glBegin`, `glDrawElements`, etc. devuelvan un error
del driver. La aplicación debe consultar:

```c
if (bk_mesa_strict_failed(ctx)) {
    bk_mesa_fallback_t reason = bk_mesa_last_fallback(ctx);
    const char *text = bk_mesa_last_fallback_string(ctx);
}
```

`bk_mesa_clear_error()` limpia el diagnóstico para iniciar otra prueba.

## Presentación directa al compositor

El source ya tenía el camino de composición GFX3D para ventanas. El SDK ahora
lo expone sin obligar a la aplicación a manipular handles:

```c
if (bk_mesa_present_gpu(ctx)) {
    bk_gui_rect_t rect = {x, y, width, height};
    bk_mesa_attach_to_window(ctx, window, rect);
}
```

`bk_mesa_attach_to_window()` conecta la surface al viewport GPU de la ventana.
El compositor espera el trabajo del backend, recorta y compone la superficie
sin una copia GPU->RAM. Para retirar el contenido:

```c
bk_mesa_detach_from_window(window);
```

Durante interacción normal, una aplicación que usa `present_gpu` y el viewport
directo debe conservar `stats.downloads == 0`.

## Estadísticas y diagnóstico

```c
bk_mesa_gpu_stats_t s;
bk_mesa_get_gpu_stats(ctx, &s);
```

Campos disponibles:

```text
frames_started / frames_finished
draw_calls / triangles / lines / points
texture_uploads / texture_region_uploads
uploads / downloads
depth_uploads / depth_downloads
fallbacks / strict_failures
```

`bk_mesa_active_renderer()` sólo indica que existe una ruta GFX3D activa. Para
probar que un viewport fue realmente GPU hay que verificar `draw_calls`,
`triangles`, `fallbacks`, `strict_failures`, `uploads` y `downloads`.

El ejemplo `sdk/examples/mesa_gpu_strict_test.c` incluye una prueba de 10.000
frames, resize, ocho luces, textura mipmapped, fog, scissor, wireframe,
transparencia normal/aditiva y creación/destrucción repetida de contextos.

## Fallbacks que continúan existiendo

Los modos `AUTO` y `GPU` todavía usan SWRAST cuando aparece una operación fuera
del subconjunto. `GPU_STRICT` la denuncia como error. Aún no se aceleran:

- Stencil y accumulation buffer.
- Alpha test.
- Logic operations y máscaras parciales de color.
- Scissored/partial clear; el scissor acelerado es para primitivas.
- Polygon stipple y polygon smoothing.
- Líneas punteadas/suavizadas y puntos suavizados/atenuados.
- Two-sided lighting y separate specular color.
- Multitextura; sólo unidad 0.
- Texturas 1D, 3D y cube maps.
- Coordenadas proyectivas de textura con `q != 1`.
- `NEAREST_MIPMAP_LINEAR` y `LINEAR_MIPMAP_LINEAR` (trilineal).
- Ecuaciones/factores de blending distintos de los dos pares documentados.
- Fog `EXP` y `EXP2`.
- Selection/feedback, `glDrawPixels`, `glReadPixels`, bitmaps y el resto de las
  operaciones clásicas de píxeles.

Estas funciones siguen disponibles en Mesa 3.5 y pueden ejecutarse por CPU en
`AUTO`/`GPU`; no forman parte del viewport GPU estricto.

## Actualizaciones parciales de texturas

`glTexSubImage2D` intenta actualizar sólo el rectángulo cambiado. VMware y Rage
128 copian la región; VirtIO/VirGL conserva la API regional pero, por las
limitaciones actuales del comando de transferencia implementado, puede terminar
transfiriendo el backing completo al host. El resultado es correcto, aunque no
es todavía la ruta óptima en VirtIO.

La caché continúa limitada a 64 imágenes/niveles, no por presupuesto de VRAM.
Una escena que exceda ese número usa reemplazo LRU y puede volver a subir
texturas.

## ABI y compatibilidad de drivers

`BK_GFX3D_DRIVER_ABI_VERSION` pasa de 2 a 3. Los tres drivers 3D incluidos se
recompilan con la nueva tabla de callbacks. Un `.DVR` 3D externo construido con
la ABI 2 debe recompilarse; el coordinador lo rechazará para evitar interpretar
una estructura más corta como ABI 3.

## Uso recomendado para 3D Plus

Para el viewport interactivo:

1. usar `BK_MESA_RENDERER_GPU_STRICT` durante desarrollo;
2. crear depth de 16 bits y stencil/accum en cero;
3. comenzar cada frame con clear completo de color y depth;
4. presentar con `bk_mesa_present_gpu()`;
5. enlazar la surface a la ventana con `bk_mesa_attach_to_window()`;
6. exigir `fallbacks == 0`, `strict_failures == 0` y `downloads == 0`;
7. cambiar a `AUTO` sólo para herramientas u operaciones que deban conservar
   compatibilidad SWRAST.

## Compilación

```sh
make -j"$(nproc)" sdk \
  build/kernel.elf \
  build/system/drivers/VMWARESVGA3D.DVR \
  build/system/drivers/VIRTIOGPU.DVR \
  build/system/drivers/ATIR1283D.DVR
```

Enlazado de una aplicación:

```sh
gcc -m32 -mfpmath=387 -mno-sse -mno-sse2 -ffreestanding -fno-builtin \
  -nostdlib -nostdinc -Os -fno-pic -fno-pie -fno-stack-protector \
  -Isdk/include -c app.c -o app.raw.o
ld -m elf_i386 -r app.raw.o build/sdk/libblesk_mesa.a \
  build/sdk/libblesk.a -o app.o
```
