# Modo de memoria reducida de BlesKernOS 0.8

Este parche agrega perfiles automáticos de memoria. No requiere una opción de
arranque: `compat_mode_init()` usa el límite físico detectado por E820.

## Perfiles

| RAM física | Perfil | Comportamiento principal |
|---|---|---|
| hasta 4 MiB | `4-7MB` mínimo | escritorio solamente, 2 tareas máximas, VGA 320x200x8, sin procesos Ring 3 |
| más de 4 y hasta 5 MiB | `4-7MB` mínimo | 3 tareas máximas; permite una aplicación a la vez |
| más de 5 y hasta 7 MiB | `4-7MB` mínimo | 5 tareas máximas |
| más de 7 y hasta 15 MiB | `8-15MB` reducido | 8 tareas, GUI a 30 FPS, pilas reducidas, sin PE/Win16 ni screensaver |
| más de 15 MiB | `normal` | comportamiento completo, hasta 16 tareas |

En el perfil de 4-7 MiB se desactivan automáticamente:

- autopruebas Ring 3 del arranque;
- carga de drivers externos y autoconfiguración de red;
- tarea de hotplug USB;
- PrintSpool;
- sonido de inicio;
- screensaver, wallpaper e imágenes de iconos;
- Win32 PE y Win16 NE;
- shadow RGBA frontal del compositor.

La GUI usa VGA modo 13h, 320x200, un único backbuffer RGBA de unos 250 KiB,
20 FPS máximos y 25 sondeos por segundo cuando está inactiva.

## Cambios generales de consumo

Estos cambios benefician también al perfil normal:

- pilas por tarea dinámicas en lugar de 128 KiB para todas;
- 48 KiB para tareas kernel normales, 64 KiB para aplicaciones comunes y
  128 KiB sólo para programas pesados reconocidos;
- guards de pila de 1 KiB en lugar de 4 KiB por extremo;
- cola de upcalls reducida de 32 a 16 entradas;
- sin shadow frontal cuando el driver ofrece `GFX_CAP_PRESENT_BUFFER`;
- catálogo de idioma sin copia transitoria adicional;
- límites Win32 internos reducidos a valores todavía amplios: 64 ventanas,
  16 menús, 256 comandos GDI, 64 archivos y 96 valores de registro;
- historial COM1 reducido de 64 a 32 KiB.

## Mapa de memoria

- Con 12 MiB o más, el heap conserva su inicio tradicional en `0x00800000` y
  queda libre la vista PE fija de 4-8 MiB.
- Entre 5 y 11 MiB, el heap comienza en `0x00400000`; por eso PE/Win16 queda
  desactivado.
- Con 4 MiB, el heap comienza después de `__bss_end` y termina antes de la pila
  de Stage 2 en `0x003FF000`.
- El linker falla si el BSS supera `0x00340000`, para impedir que futuros
  cambios rompan silenciosamente el modo de 4 MiB.

## Pruebas sugeridas

Después de reconstruir la imagen, probar por separado:

```sh
qemu-system-i386 -m 4M -drive file=build/bleskernos-ata-user.img,format=raw,if=ide -serial stdio -no-reboot -no-shutdown
qemu-system-i386 -m 5M -drive file=build/bleskernos-ata-user.img,format=raw,if=ide -serial stdio -no-reboot -no-shutdown
qemu-system-i386 -m 7M -drive file=build/bleskernos-ata-user.img,format=raw,if=ide -serial stdio -no-reboot -no-shutdown
qemu-system-i386 -m 12M -drive file=build/bleskernos-ata-user.img,format=raw,if=ide -serial stdio -no-reboot -no-shutdown
```

El log debe mostrar una línea similar a:

```text
[COMPAT] perfil=4-7MB RAM_top=4096 KiB heap=954 KiB tareas=2
```

La cifra exacta de heap puede cambiar si crece o disminuye el BSS.
