# Textbox público de BlesKernOS (API 10)

La API 10 añade un control de edición de una línea dentro del sistema global de
widgets. El control mantiene su propio texto, caret y desplazamiento horizontal.

## Comportamiento

- click para posicionar el caret entre caracteres;
- flechas izquierda/derecha;
- Home y End;
- Backspace y Delete;
- inserción en cualquier posición, no solamente al final;
- Tab o Escape quitan el foco;
- Enter ejecuta el callback del widget;
- desplazamiento horizontal automático para mantener visible el caret.

## API Ring 3

```c
bk_gui_widget_t *field = bk_gui_create_textbox(
    desktop, window, (bk_gui_rect_t){8, 8, 240, 24},
    "texto inicial", 255, on_submit);

bk_gui_widget_set_focus(window, field, true);
bk_gui_widget_get_text(field, buffer, sizeof(buffer));
bk_gui_widget_set_text(field, "nuevo texto");
```

Los botones también quedan disponibles mediante `bk_gui_create_button`. Los
widgets pertenecen a la ventana y se destruyen automáticamente junto con ella.
