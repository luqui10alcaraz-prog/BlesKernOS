# Sistema de impresión de BlesKernOS 0.8

## Arquitectura de la primera versión

```text
Aplicación Ring 3
  -> libblesk print API
  -> BPJ1 (/TEMP/SPOOL/*.BPJ)
  -> PRINTSPL.BEX (Ring 3)
  -> perfil /SYSTEM/PRINTERS/*.BPD
  -> motor TEXT | ESC/P | ESC/P2 | PCL5 | POSTSCRIPT
  -> transporte FILE | LPT/USBPRN
```

El kernel no interpreta documentos ni genera lenguajes de impresora. Solamente
expone el transporte paralelo SPP mediante `bk_print_lpt_*` y la ABI extendida
`bk_print_port_info`. `USBCLASS.DVR` agrega puertos virtuales `USBPRN1` a
`USBPRN8`; el renderizado, la cola y los perfiles se ejecutan en Ring 3.

## API de aplicaciones

Incluya `bleskernos_print.h` y enlace `LIBBLESK.A`:

```c
bk_print_job_t *job = print_begin("Documento", "PSFILE");
print_set_font(job, BK_PRINT_FONT_SERIF, 12, 0);
print_text(job, 72, 72, "Hola BlesKernOS");
print_line(job, 72, 90, 523, 90, 1);
print_end_page(job);
print_submit(job);
```

Las coordenadas y tamaños de página se expresan en puntos (1/72 de pulgada).
Los bitmaps de esta versión son monocromos, 1 bpp y MSB primero.

## Formato BPJ1

BPJ1 es el formato intermedio de la cola. Tiene una cabecera de 128 bytes y una
secuencia de comandos con tamaño explícito. El cliente escribe primero `.TMP`
y publica el trabajo mediante un `rename` a `.BPJ`, evitando que el spooler lea
un archivo incompleto.

Estados de la cola:

- `.BPJ`: en espera.
- `.RUN`: tomado por el spooler.
- `.R01` a `.R03`: reintento.
- `.ERR`: error definitivo mientras se archiva.
- `.STS`: estado legible como texto.
- `.HLD`: pausa manual del trabajo.
- `.CAN`: solicitud de cancelación.

Al terminar o cancelar un trabajo, el estado se mueve a
`/TEMP/SPOOL/HISTORY`. Los trabajos con error definitivo y su estado también se
archivan allí. De esta forma los archivos históricos no bloquean el escaneo de
la cola activa.

## Perfiles BPD incluidos

- `PSFILE.BPD`: PostScript a `/TEMP/PRINT/*.PS`.
- `GENTEXT.BPD`: texto a `/TEMP/PRINT/*.TXT`.
- `ESCP.BPD`: ESC/P monocromo por LPT1.
- `ESCP2.BPD`: núcleo compatible ESC/P2 por LPT1.
- `HPLJ4.BPD`: PCL 5 monocromo por LPT1.
- `USBTEXT.BPD`: texto crudo por USBPRN1.
- `USBPCL5.BPD`: PCL 5 monocromo por USBPRN1.
- `USBPS.BPD`: PostScript por USBPRN1.

Ejemplo:

```ini
[Printer]
Id=HPLJ4
Name=HP LaserJet 4 PCL 5 on LPT1
Language=PCL5
Transport=LPT
Port=LPT1
DPI=300
```

Agregar un modelo compatible normalmente requiere otro `.BPD`, no duplicar el
motor. Un perfil debe elegirse solamente cuando la impresora entiende realmente
ese lenguaje.

## Prueba sin impresora

En la edición Developer, ejecute `/SYSTEM/TESTS/NATIVE/PRINT_TEST.BEX`. El perfil predeterminado `PSFILE` crea un archivo en
`/TEMP/PRINT`. Ese camino permite probar la API y el spooler sin hardware.

## Prueba LPT con QEMU

Agregue al comando de QEMU:

```text
-parallel file:build/lpt-output.prn
```

El archivo resultante contiene bytes crudos del lenguaje elegido. Para PCL o
ESC/P seleccione el perfil correspondiente en la aplicación o cambie el ID que
se pasa a `print_begin`.

## Alcance de esta versión

Implementado: cola persistente, reintentos, pausa/cancelación por archivos,
perfiles externos, salida a archivo, LPT SPP por sondeo, texto, PostScript,
PCL 5 monocromo y un núcleo ESC/P/ESC/P2 monocromo.

Pendiente: interfaz gráfica de la cola, selección predeterminada en Configuración,
IEEE 1284.4, LPD/JetDirect, color, lenguajes host-based/GDI y funciones
específicas de modelos. El perfil ESC/P2 comparte por ahora el camino gráfico
`ESC *`; no pretende cubrir todas las extensiones de inyección de tinta.
