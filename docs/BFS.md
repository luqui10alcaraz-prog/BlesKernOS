# Bles File System

## BFS16

BFS16 es el formato histórico de BOS. BlesKernOS reconoce la firma `BFS1` y
lo monta exclusivamente en modo lectura: el sistema moderno nunca modifica
un disco legado. La traducción de sus registros de 16 bits se mantiene
separada del código BFS32.

## BFS32 (1994)

BFS32 usa bloques de 512 bytes, bitmap de espacio libre e inodos de 256 bytes.
El nombre no limita los tamaños: cada inodo guarda un tamaño de archivo de
64 bits, timestamps de creación/modificación/acceso y un ID estable.

Cada inodo contiene ocho extents directos:

```c
struct bfs32_extent {
    uint32_t start_block;
    uint32_t block_count;
};
```

Los nombres UTF-8 de hasta 127 bytes son nativos. El superbloque y cada inodo
tienen CRC32. `recovery_state` se marca `DIRT` durante una modificación y
`CLEN` después de confirmar bitmap, inodos y superbloque; el contador
`sequence` permite detectar una actualización interrumpida.

Crear una imagen de prueba:

```sh
python3 tools/bfs32.py mkfs build/bfs32.img --size-mib 16 --label BLES
python3 tools/bfs32.py info build/bfs32.img
```
