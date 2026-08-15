# Corrección de cursor SVGA-II para QEMU GTK/Windows

Este parche es incremental y se aplica después de
`BlesKernOS-0.8-SVGA-DVR-Advanced-2D-COMPAT.patch` y de ejecutar
`tools/merge_svga_compositor.py`.

## Problema observado

El driver detectaba correctamente el dispositivo y anunciaba cursor por
hardware, por lo que el compositor dejaba de dibujar el cursor por software.
Sin embargo, en QEMU con el frontend GTK de Windows el cursor clásico de 32 bpp
podía quedar completamente transparente.

El registro `fifo=0` del diagnóstico no indica que el FIFO esté desactivado:
el modelo legado de QEMU no anuncia capacidades de FIFO extendido. Sus
capacidades `0xE3` sí incluyen cursor clásico y cursor bypass por registros.

## Corrección

- Para el camino legado sin alpha cursor ni FIFO extendido se define el cursor
  portable AND/XOR de 1 bpp.
- Se conserva el cursor clásico de 32 bpp para dispositivos con una interfaz
  más moderna.
- Se espera a que `DEFINE_CURSOR` termine antes de seleccionar el cursor.
- Se escribe `SVGA_REG_CURSOR_ID` antes de moverlo o mostrarlo.
- `cursor_defined` sólo se activa después de una definición procesada con éxito.
- El log informa el formato elegido, por ejemplo:

```text
[SVGA.DVR] cursor hardware definido: 32x32, 1 bpp, id=1
```

También se elimina el prefijo duplicado `0x0x` del diagnóstico del driver.

## Aplicación

```bash
git apply --check BlesKernOS-0.8-SVGA-CURSOR-FIX.patch
git apply BlesKernOS-0.8-SVGA-CURSOR-FIX.patch
make -j2 build/bleskernos-ata-user.img
```

No es necesario volver a ejecutar el mezclador del compositor.

## Prueba

```powershell
qemu-system-i386 -accel whpx,kernel-irqchip=off -cpu Westmere -snapshot -drive file=build/bleskernos-ata-user.img,format=raw,if=ide,index=0 -drive file=tests/fixtures/disk-images/programas-extra.img,format=raw,if=ide,index=1 -vga vmware -boot c -m 128M -serial stdio -no-reboot -no-shutdown
```

Al entrar al escritorio debe aparecer el nuevo mensaje de cursor y el puntero
debe moverse sin invalidar su rectángulo en el back buffer.
