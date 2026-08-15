# Intel GMA 9xx

`INTELGMA.DVR` cubre los chipsets Intel 915, 945, 946, G33/G35, Q33/Q35/Q965
y GM/GME965 (GMA 900, 950, 3000, 3100, X3000, X3100 y X3500).

## Estrategia segura

El BIOS configura reloj, pipe, panel LVDS y el modo VESA de arranque. El
controlador adopta ese framebuffer lineal de 32 bpp en BAR2 y utiliza BAR0
para manejar el render ring heredado. No reprograma clocks ni el panel.

La aceleración se habilita únicamente si:

1. el framebuffer VESA pertenece al aperture Intel;
2. hay páginas GGTT distintas fuera del área visible para el ring y la prueba;
3. el ring de 64 KiB inicia correctamente;
4. un `XY_COLOR_BLT` modifica la página de prueba esperada.

Con la prueba aprobada, `fill_rect` usa `XY_COLOR_BLT` y las copias sin solape
inverso usan `XY_SRC_COPY_BLT`. Todos los waits tienen límite. Ante cualquier
timeout se restauran los registros previos y el sistema continúa por CPU.

## Límites actuales

- conserva sólo la resolución elegida por el BIOS;
- las copias con solape hacia abajo/derecha usan el fallback del core;
- no hay cursor, overlay ni modesetting nativo;
- no anuncia 3D: Gen3 y Gen4 requieren dos pipelines de comandos, estados y
  shaders sustancialmente distintos, que deben desarrollarse y probarse sobre
  hardware real antes de conectarlos a TinyGL.

## Compilación y diagnóstico

```sh
make build/system/drivers/INTELGMA.DVR
```

Un arranque acelerado informa `ring 64KiB + XY blitter OK`. Si informa
`aceleracion=CPU segura`, el modo de video sigue funcionando sin tocar el ring.

