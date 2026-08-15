# Organización de controladores

Los controladores se agrupan por el subsistema que implementan:

- `core/`: ABI y buses internos (`gfx`, `gfx3d`, PCI, block, VFS y PIT).
- `graphics/`: VGA/VESA, VMware SVGA, VirtIO GPU, ATI Rage 128 e Intel GMA.
- `audio/`: núcleo de sonido y controladores SB16, AC97 y Maestro3.
- `network/`: NIC, pila IPv4 y proveedor TLS.
- `storage/`: ATA, floppy, sistemas de archivos y almacenamiento USB.
- `input/`: teclado y mouse.
- `usb/`: host UHCI y clases USB.
- `platform/`: RTC y puerto paralelo.
- `archive/`: copias históricas que no participan del build.

Los módulos externos se compilan como `build/system/drivers/*.DVR`. Los
componentes residentes se enlazan dentro del kernel. Al mover o agregar un
archivo hay que actualizar `KERNEL_SOURCES` o el target `.DVR` correspondiente
en el `Makefile`.

