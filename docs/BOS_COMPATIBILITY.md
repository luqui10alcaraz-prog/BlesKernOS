# Continuidad BOS / BlesKernOS

`.BEX` es la identidad pública de los ejecutables de la familia BOS.
BlesKernOS 0.8 conserva temporalmente `.O` como alias compatible, pero las
aplicaciones nuevas pueden distribuirse con extensión `.BEX`.

## Generaciones reconocidas

| Identidad | Cabecera interna | Ejecución |
| --- | --- | --- |
| BOS 1.x | `BEX1` | reservada para BOS/VM |
| BOS 2.x | `BEX2` | reservada para BOS/VM |
| BOS 3.x | `BEX3` | reservada para BOS/VM |
| BOS 4.x | `BEX4` | reservada para BOS/VM |
| BlesKernOS 32-bit | ELF32 (`7F 45 4C 46`, clase 1) | nativa |

El perfil nativo se llama **BEX32** aunque conserva la cabecera ELF32 en el
archivo. Esto permite renombrar un programa enlazado de `APP.O` a `APP.BEX`
sin modificar el ABI ni el toolchain.

El lanzador decide por contenido, no solamente por extensión. Un `.BEX`
histórico se identifica correctamente y se deriva al punto de integración de
BOS/VM. Mientras el emulador 8086 no esté presente, el sistema rechaza el
archivo con un diagnóstico explícito; nunca intenta interpretarlo como código
32-bit.

## Límite de arquitectura

BOS/VM debe vivir fuera del kernel moderno:

```text
programa BEX1..BEX4
        |
        v
interprete 8086 + API BOS
        |
        v
API publica BlesKernOS
```

Las próximas capas son, en orden: especificar la cabecera completa de cada
generación BOS, implementar la CPU 8086, traducir primero consola/memoria/
archivos, añadir el intérprete `.CMD` y finalmente montar BFS16 en el VFS.
