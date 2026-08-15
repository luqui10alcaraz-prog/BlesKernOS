# GRAPHICS.PAK

`/SYSTEM/GRAPHICS.PAK` es el catálogo gráfico compartido de BlesKernOS. Evita
que cada aplicación incluya copias privadas de los mismos iconos y ofrece
nombres estables mediante las API públicas 22 y 23.

La imagen oficial contiene 975 recursos canónicos derivados del paquete npm
`@react95/icons` 2.5.3. El empaquetador elige una variante por nombre:
prioriza 32×32 y, si no existe, usa la dimensión más cercana. Los PNG se
conservan comprimidos dentro de `GRAPHICS.PAK` y el sistema sólo decodifica el
recurso solicitado. El catálogo exacto está en
`assets/graphics/GRAPHICS.CSV`.

## API

```c
bk_gui_image_t icon;

if (bk_graphics_icon_load("Folder", &icon)) {
    bk_gui_surface_draw_image(surface,
        (bk_gui_rect_t){x, y, 32, 32}, clip, &icon);
    bk_gui_image_free(&icon);
}
```

- `bk_graphics_icon_load()` busca sin distinguir mayúsculas/minúsculas.
- `bk_graphics_icon_count()` devuelve la cantidad de entradas.
- `bk_graphics_icon_name()` permite enumerar el catálogo.
- La imagen cargada pertenece a la aplicación y siempre debe liberarse con
  `bk_gui_image_free()`.

Para botones estándar, la API 23 evita administrar la imagen manualmente:

```c
bk_gui_widget_t *save = bk_gui_create_button(
    desktop, window, bounds, "Guardar", on_save);
bk_gui_widget_set_icon(save, "Save");
```

El botón conserva y libera el recurso. Volver a llamar la función reemplaza el
icono; `bk_gui_widget_set_icon(save, NULL)` lo elimina.

Para regenerar el paquete fijado actualmente:

```sh
make graphics-pak
```

También se puede construir sin red desde un tarball o directorio PNG:

```sh
python3 tools/build_graphics_pak.py --tarball icons-2.5.3.tgz
python3 tools/build_graphics_pak.py --png-dir ruta/al/png
```

## Licencia y procedencia

El código de React95 está publicado bajo MIT, pero el propio aviso del
proyecto declara que Windows y sus imágenes asociadas pertenecen a Microsoft
y no están cubiertas por esa licencia. Por eso `GRAPHICS.PAK` no se
relicencia bajo la licencia MIT general de BlesKernOS. Se distribuye como
recurso visual de compatibilidad, sujeto a los derechos de sus titulares.
El aviso original completo se conserva en `third_party/react95/LICENSE` y el
registro general en `THIRD_PARTY_LICENSES.md`.
