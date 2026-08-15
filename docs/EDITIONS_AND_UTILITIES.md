# BlesKernOS 0.8: ediciones y utilidades del sistema

## Ediciones

La compilacion acepta `EDITION=user` o `EDITION=developer`.

```bash
make user
make developer
make editions
```

`make user` crea una imagen para uso normal. Incluye las aplicaciones de
escritorio, la capa Win32 y las utilidades administrativas, pero omite pruebas,
demos y archivos del SDK.

`make developer` agrega:

- pruebas nativas en `/SYSTEM/TESTS/NATIVE`;
- pruebas Win32 en `/SYSTEM/TESTS/WIN32`;
- headers y `libblesk.a` en `/SYSTEM/SDK`;
- la demo TinyGL Gears.

Las salidas principales son:

```text
build/bleskernos-ata-user.img
build/bleskernos-usb-user.img
build/bleskernos-ata-developer.img
build/bleskernos-usb-developer.img
```

## Nuevas utilidades

- **Help Center**: ayuda integrada y deteccion de la edicion instalada.
- **Find Files**: busqueda recursiva por nombre desde una ruta elegida.
- **Archive Manager**: inspeccion, compresion y extraccion del formato BKZ1.
- **Disk Tools**: inventario de unidades, FAT check/repair, montaje y formateo
  con doble confirmacion.
- **Network Status**: enlace, IPv4, DNS, contadores, DHCP y ping.
- **Clipboard Viewer**: inspeccion y edicion del portapapeles global de texto.

El Editor de texto comparte el portapapeles global mediante copiar, cortar y
pegar. La capacidad publica actual es de 4096 bytes de texto.
