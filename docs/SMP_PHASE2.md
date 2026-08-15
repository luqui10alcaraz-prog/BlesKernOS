# SMP Phase 2: scheduler escalable

Esta fase reemplaza el escaneo global de `TASK_MAX` en cada tick por una cola
de ejecución independiente por CPU.

## Arquitectura

Cada procesador posee:

- una runqueue protegida por un spinlock corto;
- su tarea actual y quantum en datos por CPU;
- un contexto idle privado que no ocupa un slot de proceso;
- contadores de robos, migraciones e IPI de replanificación.

Las tareas ejecutables se encuentran en exactamente uno de estos estados:

1. ejecutándose en un CPU (`running_cpu >= 0`);
2. en una runqueue (`queued_cpu >= 0`);
3. en transición atómica de cambio de stack (`running_cpu == -2`);
4. dormida, zombie o aún no publicada.

## Handoff de contexto

No se publica inmediatamente la tarea que acaba de ser desalojada. Su frame
de retorno todavía vive en la pila kernel que usa el CPU actual. Publicarla
antes del `IRET` permitiría que otro CPU robara y restaurara esa misma pila.

El slot queda como `deferred_ready_slot` del CPU y se inserta en la runqueue al
siguiente ingreso al scheduler, cuando el procesador ya está ejecutando sobre
la pila de la nueva tarea.

## Balanceo

Una tarea nueva se coloca en el CPU permitido con menor profundidad de cola.
Las aplicaciones Ring 3 sin afinidad tienen una pequeña preferencia por los AP,
porque CPU0 concentra GUI, PIC y drivers heredados.

Si una runqueue queda vacía, el CPU intenta robar una tarea migrable desde la
cola más cargada. Las tareas fijadas, Win16, GUI y servicios de arranque no se
roban.

## IPI de replanificación

El vector Local APIC `0xF1` despierta un CPU remoto cuando se agrega trabajo a
una cola vacía. El handler reconoce el APIC y entra al scheduler sin atravesar
el Big Kernel Lock.

## Afinidad pública (API 28)

```c
uint32_t bk_proc_current_cpu(void);
int32_t bk_proc_set_affinity(uint32_t pid, uint32_t cpu_mask);
uint32_t bk_proc_get_affinity(uint32_t pid);
```

`pid == 0` significa la tarea actual. Una máscara cero es inválida.

Diagnóstico del scheduler:

```c
uint32_t bk_proc_runqueue_depth(uint32_t cpu);
uint32_t bk_proc_scheduler_steals(uint32_t cpu);
uint32_t bk_proc_scheduler_migrations(uint32_t cpu);
uint32_t bk_proc_scheduler_ipis(uint32_t cpu);
```

## Qué no incluye todavía

Esta fase balancea tareas e hilos existentes. No crea por sí sola múltiples
hilos dentro de una aplicación. La Fase 3 deberá separar proceso e hilo y
agregar `bk_thread_create/join`, sincronización de usuario y un pool de jobs.
