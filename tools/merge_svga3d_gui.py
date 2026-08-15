#!/usr/bin/env python3
"""Merge the optional SVGA3D compositor hooks without replacing local GUI work."""
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]


def backup(path: Path) -> None:
    target = path.with_name(path.name + ".pre-svga3d")
    if not target.exists():
        shutil.copy2(path, target)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: se esperaba una ancla, se encontraron {count}")
    return text.replace(old, new, 1)


def merge_deskmanager() -> None:
    path = ROOT / "system/desktop/deskmanager.c"
    text = path.read_text()
    original = text
    text = replace_once(
        text,
        "    if (!desktop || !surface) return;\n",
        "    if (!desktop || !surface) return;\n"
        "    /* BLESKERNOS_SVGA3D_FRAME_BEGIN */\n"
        "    gui_gpu_compositor_begin_frame(desktop, surface);\n"
        "    /* BLESKERNOS_SVGA3D_FRAME_END */\n",
        "deskmanager inicio de frame",
    )
    text = replace_once(
        text,
        "\n    deskmanager_paint_windows(desktop, surface, screen);\n",
        "\n    /* BLESKERNOS_SVGA3D_BACKGROUND_BEGIN */\n"
        "    gui_gpu_compositor_capture_background(surface);\n"
        "    /* BLESKERNOS_SVGA3D_BACKGROUND_END */\n"
        "    deskmanager_paint_windows(desktop, surface, screen);\n",
        "deskmanager fondo",
    )
    text = replace_once(
        text,
        "        if (intersects)\n            gui_window_paint_menus(surface, window);\n",
        "        /* BLESKERNOS_SVGA3D_WINDOW_SURFACE_BEGIN */\n"
        "        if (intersects)\n"
        "            gui_gpu_compositor_capture_window(window, surface);\n"
        "        /* BLESKERNOS_SVGA3D_WINDOW_SURFACE_END */\n"
        "        if (intersects)\n"
        "            gui_window_paint_menus(surface, window);\n",
        "deskmanager superficie de ventana",
    )
    if text != original:
        backup(path)
        path.write_text(text)


def merge_compositor() -> None:
    path = ROOT / "gui/compositor.c"
    text = path.read_text()
    original = text
    function_start = text.find("void gui_compositor_paint(gui_desktop_t *desktop) {")
    if function_start < 0:
        raise RuntimeError("compositor: no se encontró gui_compositor_paint")
    prefix = text[:function_start]
    body = text[function_start:]
    body = replace_once(
        body,
        "    bool old_cursor_valid;\n    bool hardware_cursor;\n",
        "    bool old_cursor_valid;\n    bool hardware_cursor;\n"
        "    bool gpu_presented = false;\n",
        "compositor variable",
    )
    body = replace_once(
        body,
        "    screen = (gui_rect_t){0, 0, desktop->surface.width,\n"
        "                          desktop->surface.height};\n",
        "    screen = (gui_rect_t){0, 0, desktop->surface.width,\n"
        "                          desktop->surface.height};\n"
        "    /* BLESKERNOS_SVGA3D_FULL_FRAME_BEGIN\n"
        "     * La primera ruta compuesta por GPU reconstruye sus capas completas\n"
        "     * cuando cambia contenido. Los cuadros que sólo mueven el cursor\n"
        "     * conservan el camino de rectángulos sucios existente. */\n"
        "    if (content_dirty && gui_gpu_compositor_enabled()) {\n"
        "        content_rect = screen;\n"
        "        content_valid = true;\n"
        "    }\n"
        "    /* BLESKERNOS_SVGA3D_FULL_FRAME_END */\n",
        "compositor frame completo",
    )
    body = replace_once(
        body,
        "    gui_gfx_reset_clip(&desktop->surface);\n\n"
        "    /* BLESKERNOS_SVGA_HW_CURSOR_BRANCH_BEGIN */\n",
        "    gui_gfx_reset_clip(&desktop->surface);\n"
        "    /* BLESKERNOS_SVGA3D_PRESENT_BEGIN */\n"
        "    if (content_valid)\n"
        "        gpu_presented = gui_gpu_compositor_present(\n"
        "            desktop, &desktop->surface);\n"
        "    /* BLESKERNOS_SVGA3D_PRESENT_END */\n\n"
        "    /* BLESKERNOS_SVGA_HW_CURSOR_BRANCH_BEGIN */\n",
        "compositor present",
    )
    old = "        if (content_valid)\n            present_rects[present_count++] = content_rect;\n"
    new = "        if (content_valid && !gpu_presented)\n            present_rects[present_count++] = content_rect;\n"
    if new not in body:
        if body.count(old) < 1:
            raise RuntimeError("compositor present hardware: ancla ausente")
        body = body.replace(old, new, 1)
    old2 = "    if (content_valid)\n        present_rects[present_count++] = content_rect;\n"
    new2 = "    if (content_valid && !gpu_presented)\n        present_rects[present_count++] = content_rect;\n"
    if new2 not in body:
        if body.count(old2) < 1:
            raise RuntimeError("compositor present software: ancla ausente")
        body = body.replace(old2, new2, 1)
    text = prefix + body
    if text != original:
        backup(path)
        path.write_text(text)


def main() -> int:
    try:
        merge_deskmanager()
        merge_compositor()
    except (OSError, RuntimeError) as exc:
        print(f"[SVGA3D GUI] ERROR: {exc}", file=sys.stderr)
        return 1
    print("[SVGA3D GUI] hooks integrados; cambios locales conservados")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
