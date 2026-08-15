# Test fixtures

`disk-images/` contiene imágenes de disco usadas manualmente para validar
compatibilidad, instalación y aplicaciones Win32.

`programas-extra.img` se genera con `make` (o `make extra-programs-image`).
Es una imagen FAT16 que contiene los programas independientes de
`programas extras/` en `/SYSTEM/PROGRAMS/`:

- `CODEX.O`
- `HYPERZIP.O`
- `VIEWER.O`
