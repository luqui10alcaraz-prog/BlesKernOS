# SMP / soporte multiprocesador de BlesKernOS 0.8

## Alcance

Esta implementación añade SMP real para x86 de 32 bits, con un máximo actual
de 16 procesadores lógicos. Conserva un arranque monoprocesador automático si
el firmware no publica una topología válida o si un procesador secundario no
responde.

El objetivo de esta primera etapa es obtener paralelismo sin volver inestables
los subsistemas heredados. Las aplicaciones nativas Ring 3 pueden ejecutarse en CPU diferentes al mismo
tiempo. Desde v13, la ruta común del kernel se divide en dominios TASK, VFS,
GUI, GFX, NET, AUDIO, DRIVER y WINE. El lock global permanece sólo como
cuarentena heredada. Las tareas de kernel y Win16 quedan fijadas al CPU0.

## Arquitectura

- Detección primaria mediante ACPI RSDP -> RSDT -> MADT.
- Fallback a Intel MultiProcessor Specification (`_MP_` / `PCMP`).
- xAPIC MMIO y ventana APIC marcada no-cacheable en paging.
- Trampoline de arranque copiado a `0x6000`.
- Secuencia INIT, deassert INIT y dos Startup IPI (SIPI).
- Paging, CR0.WP, CR4.PSE, GDT y TSS configurados en cada AP.
- GDT, TSS, pila de kernel, estado x87 y scheduler por CPU.
- Timer periódico Local APIC en cada AP, calibrado contra el PIT del BSP.
- Estado `running_cpu` para impedir que una tarea se ejecute simultáneamente en
  dos procesadores.
- Afinidad inicial:
  - aplicaciones nativas Ring 3: cualquier CPU;
  - kernel, GUI, drivers y Win16: CPU0;
  - una tarea idle privada por CPU.
- Uso de CPU agregado entre todos los procesadores online.

La implementación sigue la arquitectura descrita por Intel para Local APIC e
INIT/SIPI, la estructura MADT de ACPI y el patrón de arranque usado por kernels
x86 maduros. No copia código de Linux: se usa como referencia de secuencia y
validación de diseño.

## Perfiles de memoria

`TASK_MAX` es 32 y SMP admite hasta 16 CPU lógicos. Cada AP reserva por ahora
un slot idle, dejando el resto de la tabla para procesos y servicios. SMP se
desactiva deliberadamente en los perfiles de 4 a 15 MiB. Cada AP necesita
una pila inicial y estado adicional del scheduler; activar varios núcleos en una
máquina con tan poca RAM empeoraría el objetivo del modo de compatibilidad.

## Logs esperados

Con cuatro CPU virtuales deberían aparecer mensajes similares a estos en COM1:

```text
[SMP] topologia detectada: 4 CPU(s), BSP APIC=0, LAPIC=fee00000
[SMP] iniciando CPU1 APIC=1...
[SMP] CPU1 online
[SMP] iniciando CPU2 APIC=2...
[SMP] CPU2 online
[SMP] iniciando CPU3 APIC=3...
[SMP] CPU3 online
[SMP] 4/4 CPU(s) online; timer LAPIC=... cuentas/tick
[SMP] scheduler distribuido activo en 4 CPU(s)
```

Si ACPI/MPS no informa otros procesadores, el sistema continúa normalmente y
muestra el fallback monoprocesador.

## Prueba en QEMU/KVM

```sh
qemu-system-i386 -accel kvm -cpu qemu32 -smp 4,sockets=1,cores=4,threads=1 -snapshot -drive file=build/bleskernos-ata-user.img,format=raw,if=ide,index=0 -drive file=tests/fixtures/disk-images/programas-extra.img,format=raw,if=ide,index=1 -vga vmware -boot c -m 256M -serial stdio -no-reboot -no-shutdown
```

Para WHPX se puede conservar la configuración usada por el proyecto y añadir
`-smp 4,sockets=1,cores=4,threads=1`.

Prueba recomendada:

1. Confirmar por COM1 que todos los AP aparecen `online`.
2. Abrir varias aplicaciones nativas intensivas en CPU.
3. Verificar que GUI, almacenamiento, red, sonido y cierre de procesos siguen
   estables.
4. Repetir con `-smp 1`, `-smp 2` y `-smp 4`.
5. Repetir sin KVM/WHPX para separar errores SMP de errores del acelerador.

## Limitaciones deliberadas de esta etapa

- El PIC/PIT legado sigue entregando IRQ externas al BSP; los AP usan Local APIC
  solamente para su tick de scheduler.
- No hay IOAPIC ni afinidad de IRQ todavía.
- No hay TLB shootdown porque BlesKernOS mantiene por ahora un directorio de
  páginas identidad compartido que no cambia durante la ejecución normal.
- El lock global queda limitado a IRQ PIC, excepciones y compatibilidad no
  clasificada. Los subsistemas convertidos usan locks propios; consulte
  `docs/SMP_LOCKING.md`.
- x2APIC y topologías con APIC ID mayor de 255 no forman parte de esta primera
  versión.

## Siguiente etapa aconsejada

1. IOAPIC y routing de IRQ por MADT.
2. Spinlocks finos para heap, VFS/FAT, colas de red y compositor.
3. Colas de ejecución por CPU y balanceo menos global.
4. IPI de reschedule y TLB shootdown.
5. Contadores por CPU visibles en el monitor de rendimiento.
6. Pruebas de estrés con creación/cierre de procesos, TinyGL, disco, USB y red
   simultáneos.

## Estabilización de cargas pesadas

El Big Kernel Lock serializa el código del núcleo entre procesadores. La v9.2
mantiene la conmutación histórica de tareas desde CPL0 porque el GUI principal
también es una tarea de kernel y depende del temporizador para dormir, despertar
y entregar CPU a los servicios Ring 3. Las regiones realmente críticas siguen
usando `task_preempt_disable()` y el BKL.

Los programas pesados (`3D`, Doom, NetSurf y Wine) reciben 128 KiB tanto para
la pila Ring 3 como para la pila de kernel usada durante `int 0x80`. Antes sólo
la pila de usuario era grande y las cadenas de render/API seguían limitadas a
48 KiB. Cada proxy API reserva además 16 KiB de margen mínimo, normaliza
DS/ES/FS/GS y DF después de llamar a la implementación, y verifica un canario
del frame.

Un #GP o #PF producido dentro de un proxy API de una aplicación nativa se
registra en COM1 y termina solamente ese proceso. Los fallos del kernel fuera
de esa región continúan mostrando el panic normal; no se ocultan errores de
drivers o del núcleo sin relación con una aplicación.

## Corrección de progreso del escritorio (v9.2)

La política de v9 que bloqueaba toda conmutación desde CPL0 resultó demasiado
estricta para la arquitectura actual: el escritorio principal es una tarea de
kernel y usa el temporizador para dormir, despertar y entregar CPU a procesos
Ring 3. En SMP podía quedar sin un punto de conmutación válido justo después de
mostrar el escritorio.

La v9.2 restaura la conmutación de frames de kernel protegida por el Big Kernel
Lock y conserva las demás defensas de carga pesada: pilas grandes para programas
3D, canario del proxy API, normalización de segmentos y contención de #GP/#PF.

## Estabilidad bajo render y carga sostenida (v10)

La política correcta no es «preemptar todo CPL0» ni «bloquear todo CPL0». El
escritorio y los servicios de kernel necesitan conmutación desde sus frames de
núcleo, pero una aplicación Ring 3 no puede ser apartada arbitrariamente a
mitad de `int 0x80`: su syscall/API conserva estado en su pila kernel privada y
el Big Kernel Lock anterior pertenecía al CPU, no a esa pila suspendida.

Desde v10, una tarea de usuario interrumpida dentro del kernel permanece en el
mismo CPU hasta regresar a Ring 3. Sólo `task_yield()` y `task_sleep()` abren un
punto seguro de una sola vez, después de soltar el BKL. Las tareas de kernel
conservan la preempción necesaria para que GUI, Deskbar y servicios progresen.

El scheduler también vigila un margen de emergencia de 8 KiB y los bordes de
las pilas de usuario. Si una carga pesada consume ese margen, se termina el
proceso antes de que alcance metadatos del heap. Los contadores gráficos siguen
existiendo internamente, pero ya no imprimen reportes `[GFX:PERF]` periódicos en
COM1.

## Escalado a 16 CPU y arranque sin inanición (v11)

El límite inicial de cuatro procesadores no provenía de xAPIC ni de QEMU. Era
un límite explícito (`SMP_MAX_CPUS=4`) porque cada AP reservaba una tarea idle
dentro de la tabla global de 16 tareas. La v11 amplía la tabla a 32 entradas y
el límite SMP a 16 CPU lógicos. Así, 16 entradas cubren kernel/idle por CPU y
quedan otras 16 para procesos y servicios. El nuevo `.bss` permanece debajo de
`0x00340000`, por lo que conserva el perfil de 4 MiB.

La protección de carga pesada de v10 ahora sólo bloquea la preempción de un
syscall a mitad de camino después de que SMP fue activado. Antes de ese punto
el arranque usa la política monoprocesador histórica; de lo contrario un syscall
largo de PrintSpool/VFS podía mantener CPU0 ocupado, imprimir pulsos `.` del
splash y dejar sin tiempo a la tarea que debía entrar a `gui_init()`.

El Process Manager acepta hasta 16 CPU y cambia a una cuadrícula compacta de
cuatro columnas cuando hay más de cuatro. La capa Win32 también informa el
número real mediante `GetSystemInfo`.

## Escalado con muchos CPU y temporizador AP

Los timers Local APIC de los procesadores secundarios usan una entrada
**no bloqueante** al Big Kernel Lock. Si otro CPU ya ejecuta kernel/driver, el
tick se reconoce y se descarta; el AP continúa con el contexto interrumpido.
Esto es correcto porque un tick de planificación es una oportunidad, no una
operación que deba completarse obligatoriamente.

La versión bloqueante provocaba un *thundering herd*: con 16 CPU, quince AP
idle giraban sobre el mismo lock en el instante de activar SMP y podían impedir
que CPU0 terminara `kernel_main` y dibujara el escritorio. CPU0 además recibe
prioridad cuando está esperando el BKL, porque concentra GUI, PIC y drivers
heredados.

## Kernel por dominios (v13)

La ruta común dejó de usar el Big Kernel Lock. Consulte
`docs/SMP_LOCKING.md` para la jerarquía TASK/VFS/GUI/GFX/NET/AUDIO/DRIVER/WINE,
las nuevas primitivas y la cuarentena heredada que permanece para IRQ PIC,
excepciones y compatibilidad no clasificada.
