# Bloqueos SMP por subsistema (v13)

## Objetivo

La ruta normal de BlesKernOS ya no entra en un Big Kernel Lock único. Los
syscalls, las APIs públicas y el temporizador Local APIC usan bloqueos del dato
que realmente modifican. El bloqueo global anterior permanece únicamente como
cuarentena de IRQ PIC, excepciones y rutas de compatibilidad todavía no
clasificadas.

Esta etapa busca paralelismo seguro, no eliminar toda sincronización. Dos CPU
pueden ejecutar simultáneamente, por ejemplo, un render de usuario y una lectura
de red, o VFS y el compositor, siempre que no compartan el mismo dominio.

## Primitivas

- `kspinlock_t`: spinlock IRQ-safe y recursivo por CPU. Sólo para regiones
  cortas que no duermen. Se usa en heap y tabla del scheduler.
- `kmutex_t`: mutex recursivo propiedad de la tarea. Un contendiente cede CPU;
  se usa en operaciones largas de proceso, GUI, GFX3D y controladores de bloque.
- `krwlock_t`: bloqueo lector/escritor con preferencia de escritores. El
  registro de dispositivos de bloque permite lectores simultáneos y reserva la
  exclusión para altas/cambios.
- Datos por CPU: estado del scheduler, métricas de locks, uso de CPU y snapshots
  de Process Manager no comparten una línea global modificada en cada tick.

## Dominios

El orden global es obligatorio para evitar interbloqueos:

1. TASK
2. VFS
3. GUI
4. GFX
5. NET
6. AUDIO
7. DRIVER
8. WINE
9. LEGACY

Una operación con varios dominios siempre los adquiere en ese orden y los
libera en orden inverso. `task_yield()` suelta temporalmente los mutex de dominio
de la tarea y los recupera al reanudarse; nunca duerme sosteniendo el spinlock
del scheduler.

## Separación aplicada

- Scheduler: spinlock propio, sin BKL en el timer LAPIC.
- Heap: spinlock propio IRQ-safe.
- Syscalls: selección de dominio por número de syscall.
- API Ring 3: selección de dominio por nombre exportado (`bk_file_*`,
  `bk_gui_*`, `bk_net_*`, etc.).
- VFS, GUI, GFX, red, audio, drivers y Wine dejan de bloquearse mutuamente.
- GFX3D, pintura del escritorio y transacciones de bloque usan mutexes locales.
- Registro de dispositivos: `krwlock_t` para consultas concurrentes.
- Recolección de procesos: diferida al contexto de la GUI; liberar pilas, Wine
  y ventanas ya no ocurre dentro del timer ni del spinlock del scheduler.

## Lock-free conservado

La solicitud de repaint de la GUI continúa usando su contador generacional
atómico. No se reemplazó por una cola con lock porque sólo necesita publicar
"hay trabajo" y no transportar objetos con ciclo de vida complejo.

## Cuarentena heredada

`smp_kernel_enter()` sigue existiendo para:

- IRQ externas del PIC todavía concentradas en CPU0;
- excepciones del kernel;
- APIs externas no clasificadas;
- rutas Win16/legacy que todavía no son reentrantes.

Esto evita convertir código desconocido en concurrente por accidente. El log
normal debe indicar `kernel por dominios`; el lock heredado ya no rodea todos
los syscalls ni los ticks de los AP.

## Reglas para código nuevo

1. No llamar `task_yield()`, VFS, GUI ni `kmalloc()` manteniendo un spinlock.
2. Usar mutex para operaciones que puedan esperar hardware o recorrer datos
   grandes.
3. Proteger con el mismo lock todos los accesos al mismo objeto compartido.
4. No devolver punteros a datos mutables sin snapshot o contrato de vida.
5. Añadir la API nueva a `kernel_domain_mask_for_api()`; si no se clasifica,
   quedará deliberadamente en LEGACY.
6. Probar con `-smp 1`, `2`, `4` y `16`, además de KVM y WHPX.

## Alcance pendiente

- IOAPIC y afinidad real de IRQ.
- Locks internos más finos dentro de FAT y de cada driver de red.
- Colas runqueue por CPU y balanceo por IPI.
- Separación del dominio GUI por ventana/superficie.
- Lockdep/debug de orden de locks.

El cambio actual elimina el cuello global de la ruta común, pero una operación
individual seguirá limitada por el lock de su propio subsistema. Para que un
solo render use muchos CPU, 3D Plus además debe crear workers o tiles en Ring 3.
