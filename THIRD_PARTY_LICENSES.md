# Componentes de terceros

Este archivo registra todo código de terceros incorporado al árbol. Las
licencias de cada archivo tienen prioridad sobre la licencia MIT general del
proyecto.

## React95 icons — aviso de derechos de terceros

- Proyecto: React95, paquete `@react95/icons`.
- Origen: `https://github.com/React95/React95`, `packages/icons`.
- Versión empaquetada: 2.5.3.
- Archivos incorporados: 975 PNG canónicos dentro de
  `assets/graphics/GRAPHICS.PAK`; catálogo en
  `assets/graphics/GRAPHICS.CSV`.
- Transformación: selección determinista de una variante por nombre y
  empaquetado sin alterar el PNG.
- Aviso: React95 distribuye su software bajo MIT, pero su licencia dice
  expresamente que Windows y todas las imágenes asociadas pertenecen a
  Microsoft Corp. y no están cubiertas por la licencia de React95.

En consecuencia, los iconos de `GRAPHICS.PAK` quedan excluidos de la licencia
MIT general de BlesKernOS y conservan los derechos de sus respectivos
titulares. El aviso original completo está en
`third_party/react95/LICENSE`.

## Wine — LGPL-2.1-or-later

- Proyecto: Wine
- Origen: `https://gitlab.winehq.org/wine/wine`
- Revisión consultada: `84fe968b936c1b0b656157d7487844eba65c09a7`
- Archivo de origen: `dlls/shcore/main.c`, función `CommandLineToArgvW`.
- Archivo adaptado: `kernel/win32/shell32.c`.
- Cambio: se sustituyeron los tipos y el asignador de Wine por los de
  BlesKernOS; no se incorporó WineServer, código Unix ni binarios de Wine.
- Licencia: GNU Lesser General Public License, versión 2.1 o posterior.
  El texto completo distribuido por Wine se conserva sin cambios en
  `third_party/wine/COPYING.LIB`.

La porción derivada de Wine en `kernel/win32/` se distribuye bajo
LGPL-2.1-or-later. El resto del proyecto conserva su licencia propia salvo
que otro aviso específico indique lo contrario.

### Adaptación Win16/NE

- Fuentes consultadas en el árbol oficial de Wine:
  - `dlls/krnl386.exe16/relay.c`;
  - `dlls/krnl386.exe16/kernel.c`;
  - `dlls/krnl386.exe16/krnl386.exe16.spec`;
  - `dlls/user.exe16/user.exe16.spec`;
  - `dlls/gdi.exe16/gdi.exe16.spec`.
- Fecha de consulta: 2026-07-28.
- Metadatos derivados: `third_party/wine/win16_exports.inc`.
- Adaptación para BlesKernOS: `kernel/ne_loader.c` y las modificaciones de
  soporte Win16 indicadas en `docs/WIN16_PORT.md`.
- Cambios: los relays, las firmas Pascal y el modelo de despacho Win16 se
  adaptaron al GDT, scheduler, syscalls y GUI propios de BlesKernOS. No se
  incorporaron WineServer ni el entorno Unix de Wine.
- Licencia de estas partes derivadas y de los metadatos: LGPL-2.1-or-later.


## VMware SVGA / SVGA3D — MIT

- Proyecto/origen: interfaces públicas VMware SVGA mantenidas en el driver
  `vmwgfx` del kernel Linux.
- Archivos consultados: `svga_reg.h`, `svga3d_cmd.h` y `svga3d_types.h`.
- Uso en BlesKernOS: constantes, identificadores de comandos y estructuras
  binarias mínimas en `kernel/include/svga3d_protocol.h`.
- Código del driver y de las capas de transporte: implementación original para
  BlesKernOS; no se incorporó el driver Linux `vmwgfx` ni Mesa.
- Licencia de las definiciones públicas: `GPL-2.0 OR MIT`; BlesKernOS utiliza
  expresamente la concesión MIT incluida en los encabezados oficiales.

Copyright 1998-2021 VMware, Inc. Permission is hereby granted, free of charge,
to any person obtaining a copy of this software and associated documentation
files, to deal in the Software without restriction, including without
limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, subject to preservation of the
copyright and permission notice. The Software is provided without warranty.

## ATI Rage 128 CCE/PM4 — MIT/X11

- Fuentes consultadas: manuales públicos de programación y registros de ATI;
  controlador DRM r128 histórico del kernel Linux; `xf86-video-r128` de
  X.Org/X11Libre; controlador DRI r128 histórico de Mesa.
- Archivos incorporados/adaptados:
  - `kernel/drivers/ati_rage128_cce_ucode.h`: microcódigo CCE de ATI publicado
    con el controlador DRM r128 y su aviso MIT/X11 completo.
  - `kernel/drivers/ati_rage128_3d_dvr.c`: implementación original para
    BlesKernOS basada en la disposición pública de registros y paquetes PM4.
- Uso: CCE 192PIO, triángulos transformados, color Gouraud y estados fijos 3D.
- No se incorporó DRM, DRI, AGP/GART ni el controlador Mesa completo.

Los avisos de copyright y permiso aplicables están preservados en el encabezado
que contiene el microcódigo. El resto del módulo es código nuevo para
BlesKernOS y conserva la licencia general del proyecto.
