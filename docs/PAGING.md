# Paging y memoria de procesos

## Estado actual

BlesKernOS activa paging IA-32 durante el arranque, antes de inicializar el
heap. Usa un directorio alineado a 4 KiB y 1024 PDE con PSE, por lo que cada
entrada mapea una página de 4 MiB. El mapa identidad cubre los 4 GiB para no
perder framebuffer, ROM y MMIO de PCI. También se activa `CR0.WP`.

El mapa conserva temporalmente `PAGE_USER` en todas sus entradas. La ABI Ring 3
actual pasa punteros de ventanas, superficies y thunks ubicados en el espacio
del kernel; retirar ese permiso antes de convertir esas APIs a handles y copias
controladas rompería las aplicaciones existentes. Paging está activo, pero el
aislamiento mediante un CR3 diferente por proceso queda como la etapa siguiente.

## Heap dinámico

El heap comienza en `0x00200000` y termina al final de la región E820 utilizable
que lo contiene, alineada a 4 KiB y limitada a 1 GiB. Ya no queda fijado en
64 MiB. Por ejemplo:

- QEMU `-m 64M`: heap de aproximadamente 62 MiB;
- QEMU `-m 128M`: heap de aproximadamente 126 MiB;
- QEMU `-m 256M`: heap de aproximadamente 254 MiB.

Una llamada individual a `SYS_ALLOC` puede solicitar hasta 64 MiB y cada
proceso dispone de 128 registros de asignación. Las APIs nativas que llaman a
`bk_sys_alloc` utilizan el mismo heap sin una cuota artificial por aplicación.

## Liberación al terminar

Cada bloque conserva el `process_id` propietario. Cuando un proceso no tiene
más hilos vivos, el scheduler libera sus stacks, descriptores, sockets, ventanas,
programas GUI, imagen ELF y cualquier bloque que la aplicación no liberó. El
estado zombie y el código de salida se mantienen para que `waitpid` continúe
funcionando, pero ya no retienen la RAM.

La autoprueba `RING3PROXY.O` abandona deliberadamente 96 KiB. El arranque debe
mostrar una línea similar a:

```
[MM] proceso 4: 96 KiB recuperados
```

## Próxima etapa

Para aislamiento real hay que reemplazar los punteros GUI compartidos por
handles, usar tablas de 4 KiB, separar páginas supervisor/usuario, asignar un
directorio por proceso y cambiar CR3 en el scheduler. Después podrá agregarse
crecimiento por page fault y, eventualmente, copy-on-write o swap.
