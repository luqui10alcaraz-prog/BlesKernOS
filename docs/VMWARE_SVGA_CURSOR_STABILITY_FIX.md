# Estabilidad del cursor SVGA-II

Este parche incremental se aplica después de
`BlesKernOS-0.8-SVGA-CURSOR-FIX.patch`.

## Causa del salto

BlesKernOS usa actualmente el mouse PS/2 relativo. El dispositivo VMware VGA
legado de QEMU sólo anuncia Cursor Bypass 2. En el frontend GTK, cada escritura
de `SVGA_REG_CURSOR_ON` hace que QEMU mueva físicamente el cursor del sistema
host a las coordenadas indicadas por el guest.

El compositor estaba llamando `cursor_show(true)` en cada repintado. Esto hacía
que QEMU reposicionara continuamente el cursor de Windows, creando un bucle
entre el movimiento relativo del guest y el puntero del host. El resultado era
que ambos cursores intentaban volver a una posición, saltaban o desaparecían.

## Solución

- El driver sólo anuncia cursor hardware cuando existe Cursor Bypass 3 por FIFO.
- El camino legado Bypass 2 usa automáticamente el cursor software del
  compositor.
- Se cachean posición y visibilidad para no enviar operaciones duplicadas.
- `SHOW` se difiere hasta que exista una primera posición válida.
- Las coordenadas se recortan a los límites de la pantalla.
- Los cambios de modo invalidan correctamente la definición anterior.

En QEMU se espera este mensaje:

```text
[SVGA.DVR] cursor: software fallback; bypass 2 relativo es inseguro
```

En VMware con Bypass 3:

```text
[SVGA.DVR] cursor: hardware (FIFO bypass 3)
```

El resto de la aceleración 2D, dirty rectangles, back buffer, BitBlt, surfaces,
fences y GMR no cambia.
