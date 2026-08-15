# USBCLASS.DVR para BlesKernOS 0.8

`USBCLASS.DVR` es el controlador externo de clases USB para el host UHCI
integrado. El controlador UHCI continúa dentro de `kernel.bin` porque
BlesKernOS puede necesitarlo para leer el disco USB de arranque; el módulo se
carga después desde `/SYSTEM/DRIVERS` y registra las clases opcionales.

## Arquitectura

```text
UHCI integrado
  -> usb_core ABI 1 (dispositivo/interfaz/endpoint/transferencias)
  -> USBCLASS.DVR
       -> HID Boot + HID Report -> teclado/mouse del sistema
       -> Hub Class             -> enumeración de hijos y hot-plug
       -> Printer Class         -> puertos virtuales USBPRN1..USBPRN8
                                  -> PRINTSPL.BEX y perfiles BPD
```

El núcleo exporta una ABI pequeña en `kernel/include/usb_core.h`: registro de
controladores de clase, control transfers, bulk/interrupt transfers, selección
de alternate setting, limpieza de endpoint halt y enumeración de puertos de
hub. Los objetos de dispositivo permanecen propiedad del núcleo; el `.DVR` no
debe liberarlos ni conservarlos después de `disconnect`.

## HID Boot

Para teclados y mouse con subclass Boot, el módulo intenta primero leer y
analizar el Report Descriptor y seleccionar Report Protocol. Así conserva
rueda, botones adicionales, NKRO razonable y distribuciones que no caben en el
reporte boot. Si el descriptor falta, está mal formado o supera los límites del
parser, cambia a Boot Protocol:

- teclado: reporte de 8 bytes, modificadores y seis teclas;
- mouse: botones, X/Y relativos y rueda cuando aparece como cuarto byte.

El camino boot no depende del parser genérico y sirve como recuperación para
hardware sencillo o firmware defectuoso.

## HID Report genérico

El parser implementa items cortos/largos, estados Global/Local, Push/Pop,
Collections, Report ID, campos Variable/Array, tamaños no alineados a byte,
rangos de Usage y valores relativos/absolutos. Actualmente conecta al ABI de
entrada de BlesKernOS:

- Usage Page Keyboard/Keypad: letras, números, símbolos, modificadores,
  navegación y F1..F12;
- Generic Desktop Mouse/Pointer: X, Y y Wheel;
- Button Page: hasta cinco botones expuestos por el mouse del sistema.

Los ejes de joystick/gamepad no se convierten en movimiento del puntero: el
parser exige que el campo pertenezca a una Application Collection Mouse o
Pointer. Consumer Page y campos vendor-specific se reconocen y se omiten de
forma segura porque la ABI de teclado 0.8 todavía no tiene eventos multimedia
ni una API HID cruda. Tampoco se envían Output/Feature Reports, por lo que LED
de Caps Lock, force feedback y configuración específica del fabricante quedan
fuera de esta versión.

Límites defensivos: 512 bytes de Report Descriptor, 64 bytes por input report,
192 campos por interfaz y 16 interfaces HID activas.

## Hubs USB 1.1

La clase Hub lee el descriptor 0x29, enciende los puertos, respeta
`bPwrOn2PwrGood`, consulta el endpoint Interrupt IN y mantiene un sondeo total
de respaldo. Ante cambios de conexión:

1. aplica debounce;
2. resetea el puerto;
3. espera `PORT_ENABLE` y el fin de `PORT_RESET`;
4. limpia los bits `C_PORT_*`;
5. enumera el hijo como low-speed o full-speed.

Se admiten hubs encadenados hasta profundidad 5, 31 puertos por hub y ocho hubs
activos. UHCI puede alcanzar hijos low/full-speed de un hub USB 1.1 sin split
transactions. No se implementan dispositivos high-speed: para eso BlesKernOS
necesita una ABI HCD común para EHCI y companion controllers.

## Impresoras USB

Se implementa USB Printer Class subclass 1:

- protocolo 1: interfaz unidireccional, Bulk OUT;
- protocolo 2: interfaz bidireccional, Bulk OUT y Bulk IN opcional;
- `GET_DEVICE_ID` para diagnóstico IEEE 1284;
- `GET_PORT_STATUS` para papel, selección y error;
- `SOFT_RESET`, limpieza de halt y reinicio de data toggle al recuperar fallos.

El protocolo 3 (IEEE 1284.4) se detecta pero no se reclama porque requiere su
propio multiplexor de canales. Tampoco se implementan protocolos propietarios
como host-based/GDI. La clase USB sólo transporta bytes: la impresora debe
entender el lenguaje elegido en su perfil BPD.

Puertos publicados:

- `USBPRN1` a `USBPRN8`, estables según el slot del dispositivo;
- `USBTEXT.BPD` para impresoras de texto;
- `USBPCL5.BPD` para PCL 5;
- `USBPS.BPD` para PostScript.

No seleccione PCL o PostScript por el solo hecho de que una impresora sea USB.
Revise su documentación o el campo `CMD:` de su IEEE 1284 Device ID.

## Compilación e instalación

```sh
make build/system/drivers/USBCLASS.DVR
make build/kernel.elf
make build/bleskernos-ata-user.img
```

El Makefile incluye el módulo en `DRIVER_OBJS`, por lo que la imagen FAT32 lo
copia automáticamente a `/SYSTEM/DRIVERS/USBCLASS.DVR`.

Para instalar sólo el archivo en una imagen ya montada, copie el `.DVR` al
directorio de drivers y reinicie. También puede probarlo desde la shell:

```text
drivers load /SYSTEM/DRIVERS/USBCLASS.DVR
drivers
```

## Diagnóstico esperado

```text
[DVR] usbclass: cargado (/SYSTEM/DRIVERS/USBCLASS.DVR)
[USBCLASS] HID dev=... mode=...
[USBCLASS] hub dev=... ports=...
[USBCLASS] printer dev=... protocol=... id=...
```

Para un fallo real conserve desde la primera línea `[UHCI]`/`[USB]` hasta la
última `[USBCLASS]`. VID, PID, clase/subclase/protocolo, endpoint y Device ID
son necesarios para distinguir un error del host, de la clase o del lenguaje
de impresión.

## Alcance verificado

El módulo y todos los archivos del kernel modificados compilan como i386
freestanding con `-Wall -Werror`; `kernel.elf` enlaza correctamente y el `.DVR`
usa únicamente relocaciones `R_386_32` y `R_386_PC32`, admitidas por el loader.
La prueba en este entorno es de compilación e integración. Mouse, teclado, hub
e impresora físicos deben validarse en QEMU y en hardware real antes de llamar
a esta implementación estable.
