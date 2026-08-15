#!/usr/bin/env python3
"""Merge the SVGA hardware-cursor path into a locally modified compositor.

The transformation is deliberately anchor-based instead of line-number based.
It makes a backup and refuses to write if the expected compositor architecture
cannot be recognized.
"""
from __future__ import annotations

import argparse
import re
import shutil
from pathlib import Path

HELPER_BEGIN = "/* BLESKERNOS_SVGA_HW_CURSOR_HELPER_BEGIN */"
HELPER_END = "/* BLESKERNOS_SVGA_HW_CURSOR_HELPER_END */"
INIT_BEGIN = "/* BLESKERNOS_SVGA_HW_CURSOR_INIT_BEGIN */"
INIT_END = "/* BLESKERNOS_SVGA_HW_CURSOR_INIT_END */"
BRANCH_BEGIN = "/* BLESKERNOS_SVGA_HW_CURSOR_BRANCH_BEGIN */"
BRANCH_END = "/* BLESKERNOS_SVGA_HW_CURSOR_BRANCH_END */"

HELPER = r'''
/* BLESKERNOS_SVGA_HW_CURSOR_HELPER_BEGIN */
static bool g_hardware_cursor_ready;
static uint32_t g_hardware_cursor_generation;

static bool compositor_prepare_hardware_cursor(void) {
    static uint32_t pixels[GUI_CURSOR_WIDTH * GUI_CURSOR_HEIGHT];
    uint32_t generation = gfx_driver_generation();

    if (!gfx_cursor_supported()) {
        g_hardware_cursor_ready = false;
        g_hardware_cursor_generation = 0U;
        return false;
    }
    if (g_hardware_cursor_ready &&
        g_hardware_cursor_generation == generation) return true;

    g_hardware_cursor_ready = false;
    for (int row = 0; row < GUI_CURSOR_HEIGHT; row++) {
        for (int col = 0; col < GUI_CURSOR_WIDTH; col++) {
            char px = g_arrow_cursor[row][col];
            uint32_t color = 0U;
            if (px == 'X') color = 0xFF000000U;
            else if (px == 'o') color = 0xFF808080U;
            else if (px == '.') color = 0xFFFFFFFFU;
            pixels[row * GUI_CURSOR_WIDTH + col] = color;
        }
    }
    g_hardware_cursor_ready = gfx_cursor_define(
        pixels, GUI_CURSOR_WIDTH, GUI_CURSOR_HEIGHT, 0U, 0U);
    if (g_hardware_cursor_ready) {
        g_hardware_cursor_generation = generation;
        (void)gfx_cursor_show(true);
    } else {
        g_hardware_cursor_generation = 0U;
    }
    return g_hardware_cursor_ready;
}
/* BLESKERNOS_SVGA_HW_CURSOR_HELPER_END */

'''

INIT = r'''    /* BLESKERNOS_SVGA_HW_CURSOR_INIT_BEGIN */
    hardware_cursor = !desktop->cursor_trail_enabled &&
                      compositor_prepare_hardware_cursor();
    if (!hardware_cursor && g_hardware_cursor_ready)
        (void)gfx_cursor_show(false);
    /* BLESKERNOS_SVGA_HW_CURSOR_INIT_END */
'''

BRANCH = r'''
    /* BLESKERNOS_SVGA_HW_CURSOR_BRANCH_BEGIN */
    if (hardware_cursor) {
        if (content_valid)
            present_rects[present_count++] = content_rect;
        /* Presentar una vez el rectángulo restaurado del cursor software
           anterior; luego el puntero vive como overlay del dispositivo. */
        if (old_cursor_valid &&
            (!content_valid || desktop->cursor_rect.x < content_rect.x ||
             desktop->cursor_rect.y < content_rect.y ||
             desktop->cursor_rect.x + desktop->cursor_rect.w >
                 content_rect.x + content_rect.w ||
             desktop->cursor_rect.y + desktop->cursor_rect.h >
                 content_rect.y + content_rect.h))
            present_rects[present_count++] = desktop->cursor_rect;
        if (present_count)
            gui_gfx_present_rects(&desktop->surface, present_rects,
                                  present_count);
        (void)gfx_cursor_move(desktop->mouse_x, desktop->mouse_y);
        (void)gfx_cursor_show(true);
        if (content_valid) compositor_finish_windows(desktop);
        if (content_valid && desktop->dirty_generation == dirty_generation)
            desktop->dirty_valid = false;
        desktop->cursor_valid = false;
        desktop->cursor_paint_count = 0U;
        desktop->paint_valid = true;
        return;
    }
    /* BLESKERNOS_SVGA_HW_CURSOR_BRANCH_END */
'''


def find_function_span(text: str) -> tuple[int, int]:
    match = re.search(
        r"\bvoid\s+gui_compositor_paint\s*\(\s*gui_desktop_t\s*\*\s*desktop\s*\)\s*\{",
        text,
    )
    if not match:
        raise RuntimeError("no se encontró gui_compositor_paint(gui_desktop_t *desktop)")
    brace = text.find("{", match.start())
    depth = 0
    for index in range(brace, len(text)):
        ch = text[index]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return match.start(), index + 1
    raise RuntimeError("gui_compositor_paint tiene llaves desbalanceadas")


def replace_function(text: str, start: int, end: int, function: str) -> str:
    return text[:start] + function + text[end:]


def merge(path: Path) -> bool:
    original = path.read_text(encoding="utf-8")
    text = original

    if '#include "../kernel/include/gfx.h"' not in text:
        include = re.search(r'^#include\s+"gui\.h"\s*$', text, re.MULTILINE)
        if not include:
            raise RuntimeError('no se encontró #include "gui.h"')
        text = text[:include.end()] + '\n#include "../kernel/include/gfx.h"' + text[include.end():]

    if HELPER_BEGIN not in text:
        anchor = re.search(r'\nstatic\s+gui_rect_t\s+cursor_rect\s*\(', text)
        if not anchor:
            raise RuntimeError("no se encontró static gui_rect_t cursor_rect(...)")
        text = text[:anchor.start() + 1] + HELPER + text[anchor.start() + 1:]

    start, end = find_function_span(text)
    function = text[start:end]

    required_tokens = [
        "content_rect", "present_rects", "present_count", "content_valid",
        "old_cursor_valid", "dirty_generation", "cursor_save_background",
        "gui_gfx_reset_clip", "compositor_finish_windows",
    ]
    missing = [token for token in required_tokens if token not in function]
    if missing:
        raise RuntimeError(
            "el compositor cambió demasiado; faltan anclas: " + ", ".join(missing)
        )

    if "bool hardware_cursor;" not in function:
        declaration = re.search(r'(^[ \t]*bool\s+old_cursor_valid\s*;[ \t]*$)',
                                function, re.MULTILINE)
        if not declaration:
            raise RuntimeError("no se encontró la declaración old_cursor_valid")
        function = (function[:declaration.end()] +
                    "\n    bool hardware_cursor;" +
                    function[declaration.end():])

    if INIT_BEGIN not in function:
        assignment = re.search(
            r'(^[ \t]*old_cursor_valid\s*=\s*desktop->cursor_valid\s*;[ \t]*$)',
            function,
            re.MULTILINE,
        )
        if not assignment:
            raise RuntimeError("no se encontró old_cursor_valid = desktop->cursor_valid;")
        function = function[:assignment.end()] + "\n" + INIT + function[assignment.end():]

    if BRANCH_BEGIN not in function:
        reset_matches = list(re.finditer(
            r'^[ \t]*gui_gfx_reset_clip\s*\(\s*&desktop->surface\s*\)\s*;[ \t]*$',
            function,
            re.MULTILINE,
        ))
        if not reset_matches:
            raise RuntimeError("no se encontró gui_gfx_reset_clip(&desktop->surface);")
        # Use the last reset before the software-cursor save path.
        cursor_save = function.find("cursor_save_background")
        candidates = [m for m in reset_matches if m.start() < cursor_save]
        reset = candidates[-1] if candidates else reset_matches[-1]
        function = function[:reset.end()] + "\n" + BRANCH + function[reset.end():]

    text = replace_function(text, start, end, function)
    if text == original:
        print(f"[OK] {path}: la integración ya estaba aplicada")
        return False

    backup = path.with_suffix(path.suffix + ".pre-svga-advanced")
    if not backup.exists():
        shutil.copy2(path, backup)
    path.write_text(text, encoding="utf-8")
    print(f"[OK] integración semántica aplicada a {path}")
    print(f"[OK] copia de seguridad: {backup}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".", help="raíz del source de BlesKernOS")
    args = parser.parse_args()
    target = Path(args.root).resolve() / "gui" / "compositor.c"
    if not target.is_file():
        print(f"[ERROR] no existe {target}")
        return 2
    try:
        merge(target)
    except RuntimeError as exc:
        print(f"[ERROR] {exc}")
        print("[INFO] no se modificó compositor.c")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
