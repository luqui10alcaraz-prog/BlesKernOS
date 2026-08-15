# Controladores cargables `.DVR`

BlesKernOS carga controladores opcionales desde `/SYSTEM/DRIVERS` después de
montar el volumen FAT de arranque. Los archivos son objetos ELF32 `ET_REL`
residentes: se relocalizan una vez, ejecutan su inicialización y permanecen en
memoria mientras el sistema está encendido.

## Qué permanece en el kernel

La ruta mínima necesaria para arrancar y poder leer los propios `.DVR` sigue
integrada en `kernel.bin`: memoria, GDT/IDT/PIC/PIT, tareas, PCI, bloque, ATA,
USB UHCI y almacenamiento USB, FAT/VFS, video, teclado y mouse. Esto permite
arrancar tanto desde el disco ATA como desde el pendrive de la Dell Latitude
C600 y mantiene disponible la consola de recuperación.

## Módulos actuales

- `AC97.DVR`: audio PCM Intel ICH AC'97 mediante bus mastering PCI.
- `MAESTRO3.DVR`: ESS Allegro, Canyon3D-2 y Maestro3/3i con firmware ASSP.
- `SB16.DVR`: Sound Blaster 16 y respaldo mediante PC speaker.
- `CMOSRTC.DVR`: reloj CMOS/RTC.
- `ISO9660.DVR`: montaje y lectura de CD-ROM ISO9660.
- `PS2MOUSE.DVR`: mouse PS/2 y protocolo auxiliar del controlador 8042.
- `USBCLASS.DVR`: HID Boot/Report, hubs USB 1.1 y Printer Class sobre la ABI
  `usb_core` del host UHCI integrado. Publica impresoras como USBPRN1..USBPRN8.
- `VESA.DVR`: framebuffer lineal VESA y cambio de modo Bochs VBE.
- `3C90X.DVR`: Ethernet 3Com EtherLink XL 3c900/3c905 y variantes portátiles
  3c555/3c556, mediante listas DMA y sondeo desde una tarea.
- `RTL8139.DVR`: Realtek RTL8129/RTL8139 por BAR de E/S, anillo RX y cuatro
  buffers TX; validado con el dispositivo RTL8139 emulado por QEMU.
- `NETSTACK.DVR`: Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP y HTTP. Se mantiene
  separado del controlador de hardware mediante la ABI de `network.h`.
- `TLS.DVR`: TLS 1.2 BearSSL, SNI y validación X.509 con CA de Mozilla. Requiere
  reloj correcto y RDRAND; no incorpora la criptografía al kernel base.

`AC97.DVR` reconoce los bus-master Intel ICH/ICH0 a ICH7 conocidos por sus
IDs PCI, convierte la API PCM mono U8 a S16LE estéreo a 48 kHz y alimenta una
BDL circular mediante una tarea. No toma dispositivos VIA/SiS/ALi sólo porque
anuncien clase de audio: esos controladores usan interfaces incompatibles. Si
no hay un controlador AC'97 válido, `SB16.DVR` permanece como respaldo.

`MAESTRO3.DVR` reconoce los dispositivos ESS `125D:1988`, `1989`, `1990`,
`1992`, `1998`, `1999`, `199A` y `199B`. El módulo contiene las imágenes
ASSP kernel/minisrc, configura el enlace AC'97, mezcla hasta ocho voces de la
API en un anillo DMA U8 mono de 48 kHz y respeta el límite DMA de 28 bits del
chip. También aplica los quirks de IrDA de Dell Inspiron 4000/8000/8100 y el
codec de dock de Latitude C810 mediante los IDs de subsistema PCI.

En hardware real, un arranque correcto imprime dos líneas `[M3]` con el ID
PCI, `SUBSYS`, puerto de E/S, codec, tamaños de firmware y dirección DMA. Si
aparece `codec AC97 no respondio`, `BAR0 de E/S invalida`, `buffer DMA fuera`
o `DMA sin progreso`, esas líneas deben conservarse completas para diagnosticar
la variante exacta del portátil.

El kernel conserva un proxy muy pequeño para cada subsistema. Los programas
siguen llamando las APIs históricas (`sound_*`, `rtc_*`, `iso9660_*`) y el proxy
las deriva al módulo cargado. Si falta un `.DVR`, esas APIs fallan de manera
segura en lugar de saltar a una dirección inválida.

## ABI

Todo módulo debe exportar:

```c
const bk_driver_module_t *bleskernos_driver_query(void);
```

El descriptor usa `BK_DRIVER_ABI_VERSION` (versión 2), el tamaño exacto del descriptor, un
nombre, una descripción y las funciones `init`/`shutdown`. El cargador rechaza
archivos con ABI incorrecta, descriptor incompatible, entrypoint ausente,
símbolos sin resolver, nombres duplicados o inicialización fallida, e informa
el motivo con el prefijo `[DVR]`.

En la shell, `drivers` muestra la lista de módulos residentes y su ruta.
`drivers load /SYSTEM/DRIVERS/ARCHIVO.DVR` permite probar manualmente un módulo;
una segunda carga del mismo archivo o nombre se rechaza de forma segura.

## Frontera de arranque de la versión 0.6

La Latitude C600 arranca desde almacenamiento USB, por lo que en esta versión
`usb_uhci.c` y `usb_storage.c` son controladores de arranque y permanecen
integrados. ATA también queda integrado para poder montar el disco interno.
FAT, VFS y la capa de bloques no son módulos de dispositivo: son la
infraestructura que permite encontrar y cargar `/SYSTEM/DRIVERS`.

El video comienza mediante VGA/texto integrado. Después de cargar `VESA.DVR`,
`gfx_init()` vuelve a consultar la información que dejó el bootloader y adjunta
el framebuffer antes de iniciar la GUI. El teclado permanece integrado para la
consola de recuperación; el mouse se inicializa después del montaje mediante
`PS2MOUSE.DVR`.

El controlador de disquete no se restaura. El proyecto dejó de crear la imagen
de disquete y la detección fantasma de `fd0` podía bloquear la interfaz al
intentar abrirla.

## Estado de `usb_core` y siguiente etapa

Este parche introduce `usb_core` ABI 1 para UHCI: dispositivos, interfaces,
endpoints, control/bulk/interrupt transfers, hot-plug, hubs y registro de
controladores de clase externos. `USBCLASS.DVR` es su primer consumidor.

EHCI (`usb_storage.c`) todavía conserva su pila Mass Storage independiente. La
siguiente refactorización debe extraer una ABI HCD común, mover BOT/SCSI a una
sola clase y definir companion routing UHCI/EHCI. Hasta entonces, UHCI y Mass
Storage de arranque permanecen integrados y `USBCLASS.DVR` sólo se adjunta a
dispositivos enumerados por UHCI. Consulte `docs/usbclass.md`.
