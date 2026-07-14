# SDK nativo de BlesKernOS

Este SDK genera aplicaciones ELF32 relocatables que se comunican con el
kernel exclusivamente mediante `int 0x80`. Una aplicación sólo incluye
`sdk/include/bleskernos.h`; no debe incluir archivos de `kernel/`, `gui/` ni
drivers.

El punto de entrada actual es:

```c
void bleskernos_program_main(void *unused);
```

Compilación manual:

```sh
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostdinc -fno-pic \
  -fno-pie -fno-stack-protector -Isdk/include -c app.c -o app.raw.o
ld -m elf_i386 -r app.raw.o build/sdk/libblesk.a -o app.o
```

`app.o` puede copiarse a `/SYSTEM/PROGRAMS`. La ABI actual ofrece consola,
archivos, directorios, memoria, tiempo y administración de procesos.

## API de aplicaciones nativas

Los programas y comandos ET_REL que necesitan servicios de escritorio o de
administración incluyen `sdk/include/bleskernos_api.h`. Esta API v8 también se
ejecuta en Ring 3 y el cargador transforma sus símbolos en transiciones
controladas al kernel.

La API entrega estructuras públicas por copia (`bk_block_info_t`,
`bk_partition_info_t`, `bk_volume_info_t`, `bk_volume_check_report_t`,
`bk_pci_info_t` y `bk_process_info_t`). Una aplicación no
debe incluir `kernel/include/api.h` ni recibir `block_device_t`, `fat_fs_t`,
`pci_device_t`, estructuras de drivers o punteros internos equivalentes.

`bk_device_check_volume()` ejecuta un análisis FAT de sólo lectura. El informe
público permite consultar copias FAT divergentes, clusters perdidos, cruzados,
malos o reservados, cadenas circulares o inválidas y tamaños incoherentes sin
exponer al programa las estructuras internas del controlador. El mismo informe
incluye archivos fragmentados, cantidad de fragmentos y la mayor extensión
libre. `bk_device_partition_count()` y `bk_device_partition_info()` permiten
consultar las cuatro entradas primarias de cada MBR sin dar acceso crudo al
dispositivo.

`bk_device_repair_volume()` aplica la reparación conservadora usada por
`checkdisk /fix` y ScanDisk. Antes de escribir exige que el árbol completo de
directorios sea legible; sincroniza las FAT, repara la copia de arranque FAT32,
corta cadenas dañadas y libera clusters demostrablemente huérfanos. Devuelve un
informe separado con cada cambio y vuelve a analizar el volumen al terminar.

La misma cabecera declara una GUI Ring 3 de objetos opacos. Las aplicaciones
pueden crear ventanas y dibujar superficies mediante `bk_gui_*` sin incluir
`gui/gui.h` ni conocer la representación interna de ventanas y escritorios.

Ejemplo de enumeración de discos:

```c
#include <bleskernos_api.h>

void bleskernos_program_main(void *unused) {
    bk_block_info_t disk;
    (void)unused;
    for (uint32_t i = 0; i < bk_device_block_count(); i++) {
        if (bk_device_block_info(i, &disk))
            bk_console_printf("%s: %u sectores\n", disk.name,
                              disk.sector_count);
    }
    bk_proc_exit();
}
```
