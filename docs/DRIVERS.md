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
- `VESA.DVR`: framebuffer lineal VESA y cambio de modo Bochs VBE.

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

## Siguiente etapa: `usb_core`

Actualmente hay dos pilas parcialmente independientes:

- `usb_storage.c` implementa EHCI y una implementación Mass Storage/BOT.
- `usb_uhci.c` implementa UHCI y otra implementación Mass Storage/BOT.

Antes de producir `UHCI.DVR` o `USBMSC.DVR` hay que extraer, en este orden:

1. Tipos comunes de dispositivo, configuración, interfaz y endpoint USB.
2. Una interfaz HCD para `control`, `bulk`, reset de puerto y hotplug.
3. Enumeración común: descriptor, dirección y configuración.
4. Una sola implementación de clase Mass Storage/BOT/SCSI.
5. Registro del disco resultante en la capa `block`.
6. Una política de bootstrap: USB integrado, initramfs o módulos precargados
   por el bootloader.

Hasta completar esos puntos, UHCI se conserva exactamente en la ruta de
arranque que ya fue validada en el hardware real.
