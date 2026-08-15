# VMware SVGA-II en BlesKernOS 0.8

Este driver agrega un backend de video nativo para el dispositivo PCI VMware
SVGA-II (`15AD:0405`), usado por VMware y por QEMU con `-vga vmware`.

## Arquitectura

- `kernel/drivers/graphics/vmware_svga.c` detecta el dispositivo PCI, habilita I/O,
  memoria y bus mastering, negocia SVGA ID 2/1/0 e inicializa el FIFO.
- El modo activo es framebuffer lineal de 32 bpp. El driver conserva VESA y
  VGA como fallbacks cuando SVGA-II no existe o falla la inicialización.
- La GUI sigue renderizando en su surface de RAM. Al presentar, copia sólo los
  rectángulos modificados a VRAM, emite `SVGA_CMD_UPDATE` y hace un único
  `SYNC` por lote.
- `gfx_fill_rect_rgb()` usa `SVGA_CMD_RECT_FILL` cuando la capacidad está
  anunciada. `gfx_copy_rect()` usa `SVGA_CMD_RECT_COPY`. Ambos tienen fallback
  por software.
- Las escrituras por píxel acumulan un rectángulo sucio y se muestran con
  `gfx_flush()`. Para programas externos se exportan `bk_gfx_present_rect()`,
  `bk_gfx_flush()` y `bk_gfx_copy_rect()`.

## Compilar y probar

```sh
make build/bleskernos-ata-user.img
make run-svga
```

Comando equivalente:

```sh
qemu-system-i386 -cpu qemu32,+rdrand \
  -drive file=build/bleskernos-ata-user.img,format=raw,if=ide \
  -vga vmware -boot c -m 128M -serial stdio -no-reboot -no-shutdown
```

En la shell del sistema:

```text
video
gfx rect 40 40 220 120 4
gfx copy 40 40 300 200 220 120
```

`video` debe informar `vmware-svga2`. El segundo comando prueba relleno y el
tercero prueba copia rectangular acelerada.

## Límites actuales

- Sólo 32 bpp y una pantalla.
- Sin cursor de hardware, IRQ, fences ni aceleración 3D.
- La sincronización del FIFO usa polling con límite para evitar un bloqueo
  infinito si el dispositivo deja de responder.
