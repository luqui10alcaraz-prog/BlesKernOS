# SMP Phase 1 — aislamiento y diagnóstico

Este cambio endurece el SMP existente antes de añadir hilos de usuario o un
scheduler con runqueues por CPU. Su objetivo es que un fallo de una aplicación
sea detectable y contenido, y que las carreras de locks dejen información útil.

## Memoria

* La página cero queda no presente.
* Los primeros 4 MiB (kernel, GDT, IDT, trampoline y datos estáticos) son
  supervisor-only.
* La vista PE fija y el heap compartido conservan PAGE_USER por compatibilidad
  con el ABI actual.
* Las pilas Ring 3 y las pilas kernel se alinean a 4 KiB y tienen una página no
  presente en cada extremo.
* Los cambios de permisos publican una generación TLB. Cada CPU recarga CR3 en
  su siguiente entrada al scheduler; un #PF por traducción obsoleta se reintenta
  una vez.
* Los syscalls validan PAGE_PRESENT/PAGE_USER/PAGE_WRITABLE y usan
  copy_from_user/copy_to_user para argumentos pequeños y cadenas.

### Límite todavía vigente

BlesKernOS 0.8 usa un heap plano compartido por compatibilidad con aplicaciones
ELF/PE y superficies GUI que reciben punteros directos. Por ello, el parche
protege el núcleo estático y las pilas, pero todavía no crea un directorio de
páginas independiente por proceso. La separación completa requiere cambiar el
ABI de buffers compartidos o mapear explícitamente cada objeto exportado.

## Locks

Los dominios conservan el orden fijo:

    TASK -> VFS -> GUI -> GFX -> NET -> AUDIO -> DRIVER -> WINE -> LEGACY

Cada CPU registra la profundidad de dominios. Intentar adquirir un dominio de
menor rango mientras se mantiene uno de mayor rango produce:

    [LOCK] inversion CPU... pid...: ... despues de ...

El aviso no fuerza un panic: permite descubrir y corregir la ruta sin ocultar
el problema original.

## Watchdog

Cada timer del scheduler actualiza un heartbeat por CPU. CPU0 revisa una vez por
segundo y, si un procesador no progresa durante cinco segundos, imprime:

    [WATCHDOG] CPU... sin progreso durante 5 s heartbeat=...

El panic muestra CPU, PID y máscara de dominios; COM1 agrega también la máscara
de CPUs estancadas.

## Pruebas mínimas

Ejecutar cada prueba durante al menos diez minutos:

1. `-smp 1`: abrir/cerrar aplicaciones, copiar archivos y usar Win16/Win32.
2. `-smp 2`: render de 3D Plus mientras Process Manager y File Browser trabajan.
3. `-smp 4`: render, red/TLS, audio y lectura FAT simultáneos.
4. `-smp 16`: repetir la carga y comprobar que no aparecen #GP/#PF de kernel,
   inversiones de locks ni CPUs estancadas.
5. Provocar un puntero Ring 3 inválido: debe morir sólo el proceso y el #PF debe
   mostrar la dirección de CR2.
6. Crear y destruir aplicaciones repetidamente para verificar que las guard
   pages se restauran antes de devolver cada bloque al heap.

La fase siguiente debe introducir espacios de direcciones por proceso,
runqueues por CPU e IPI de replanificación. Esas piezas no se deben mezclar con
este parche de estabilización hasta superar las pruebas anteriores.
