#!/usr/bin/env python3
"""
build_bootsplash.py v5

Genera kernel/bootsplash.c hardcodeando assets/boot/splashlogo.bmp.

"""

from __future__ import annotations

import struct
import sys
from pathlib import Path


def die(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    raise SystemExit(1)


def read_bmp_argb(path: Path) -> tuple[int, int, list[int]]:
    data = path.read_bytes()

    if len(data) < 54:
        die(f"{path} no parece ser un BMP válido")

    if data[:2] != b"BM":
        die(f"{path} no es un BMP clásico BM")

    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]

    if dib_size < 40:
        die("solo soporto BMP con BITMAPINFOHEADER o superior")

    width = struct.unpack_from("<i", data, 18)[0]
    raw_height = struct.unpack_from("<i", data, 22)[0]
    planes = struct.unpack_from("<H", data, 26)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]

    if planes != 1:
        die("BMP inválido: planes != 1")
    if width <= 0 or raw_height == 0:
        die("BMP inválido: dimensiones incorrectas")
    if bpp not in (24, 32):
        die(f"solo soporto BMP 24-bit o 32-bit sin compresión, este es {bpp}-bit")
    if compression != 0:
        die("solo soporto BMP sin compresión BI_RGB")

    height = abs(raw_height)
    top_down = raw_height < 0
    bpp_bytes = bpp // 8
    stride = ((width * bpp_bytes + 3) // 4) * 4

    if pixel_offset + stride * height > len(data):
        die("BMP inválido: datos de píxeles fuera del archivo")

    pixels: list[int] = []

    for y in range(height):
        src_y = y if top_down else height - 1 - y
        row = pixel_offset + src_y * stride

        for x in range(width):
            p = row + x * bpp_bytes
            b = data[p + 0]
            g = data[p + 1]
            r = data[p + 2]
            a = data[p + 3] if bpp == 32 else 255
            pixels.append((a << 24) | (r << 16) | (g << 8) | b)

    return width, height, pixels


def make_c_array(name: str, values: list[int]) -> str:
    out = [f"static const uint32_t {name}[] = {{"]
    for i in range(0, len(values), 8):
        chunk = values[i:i + 8]
        out.append("    " + ", ".join(f"0x{v:08X}U" for v in chunk) + ",")
    out.append("};")
    return "\n".join(out)


HEADER_TEXT = r"""#ifndef BOOTSPLASH_H
#define BOOTSPLASH_H

#include "types.h"

void bootsplash_show(const char *status, uint8_t progress);
void bootsplash_pulse(void);
void bootsplash_disable(void);

#endif
"""


C_TEMPLATE = r"""/*
 * bootsplash.c
 *
 * Archivo generado automáticamente por tools/build_bootsplash.py.
 * No edites este archivo a mano si vas a regenerar el splash.
 */

#include "include/bootsplash.h"
#include "include/gfx.h"
#include "include/types.h"

#define SPLASH_LOGO_SRC_W __WIDTH__U
#define SPLASH_LOGO_SRC_H __HEIGHT__U

#define SPLASH_BAR_W      190
#define SPLASH_BAR_H       18
#define SPLASH_BAR_BLOCK   18
#define SPLASH_BAR_GAP      6
#define SPLASH_BAR_BLOCKS   3

__ARRAY__

static bool g_enabled = false;
static bool g_drawn_once = false;
static uint32_t g_pulse_frame = 0;
static int g_logo_x = 0;
static int g_logo_y = 0;
static int g_logo_w = 0;
static int g_logo_h = 0;
static int g_bar_x = 0;
static int g_bar_y = 0;
static int g_bar_w = SPLASH_BAR_W;
static int g_bar_h = SPLASH_BAR_H;
static int g_text_y = 0;

static uint32_t blend_rgb(uint32_t base, uint32_t top, uint8_t alpha) {
    uint32_t br = (base >> 16) & 0xFF;
    uint32_t bg = (base >> 8) & 0xFF;
    uint32_t bb = base & 0xFF;

    uint32_t tr = (top >> 16) & 0xFF;
    uint32_t tg = (top >> 8) & 0xFF;
    uint32_t tb = top & 0xFF;

    uint32_t r = (br * (255U - alpha) + tr * alpha) / 255U;
    uint32_t g = (bg * (255U - alpha) + tg * alpha) / 255U;
    uint32_t b = (bb * (255U - alpha) + tb * alpha) / 255U;

    return (r << 16) | (g << 8) | b;
}

static void put_argb_scaled(int x, int y, uint32_t argb, uint8_t fade) {
    uint8_t alpha = (uint8_t)(argb >> 24);
    uint32_t rgb = argb & 0x00FFFFFFU;

    if (alpha == 0) return;

    alpha = (uint8_t)(((uint32_t)alpha * fade) / 255U);
    gfx_putpixel_rgb(x, y, blend_rgb(0x00000000, rgb, alpha));
}

static int strlen8(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static void draw_text_centered(int y, const char *text) {
    const gfx_info_t *info = gfx_get_info();
    int x;

    if (!info || !text) return;

    x = ((int)info->width - strlen8(text) * 8) / 2;
    if (x < 0) x = 0;

    gfx_draw_string(x, y, text, 7, 0, false);
}

static void draw_logo_scaled(uint8_t fade) {
    if (g_logo_w <= 0 || g_logo_h <= 0) return;

    for (int y = 0; y < g_logo_h; y++) {
        uint32_t sy = ((uint32_t)y * SPLASH_LOGO_SRC_H) / (uint32_t)g_logo_h;
        for (int x = 0; x < g_logo_w; x++) {
            uint32_t sx = ((uint32_t)x * SPLASH_LOGO_SRC_W) / (uint32_t)g_logo_w;
            uint32_t argb = g_splash_logo[sy * SPLASH_LOGO_SRC_W + sx];
            put_argb_scaled(g_logo_x + x, g_logo_y + y, argb, fade);
        }
    }
}

static void draw_bar_frame(void) {
    gfx_fill_rect_rgb(g_bar_x, g_bar_y, g_bar_w, g_bar_h, 0x00B8B8B8);
    gfx_fill_rect_rgb(g_bar_x + 1, g_bar_y + 1, g_bar_w - 2, g_bar_h - 2, 0x00404040);
    gfx_fill_rect_rgb(g_bar_x + 2, g_bar_y + 2, g_bar_w - 4, g_bar_h - 4, 0x00000000);
}

static void draw_bar_blocks(void) {
    int inner_x = g_bar_x + 3;
    int inner_y = g_bar_y + 3;
    int inner_w = g_bar_w - 6;
    int inner_h = g_bar_h - 6;
    int period = inner_w + (SPLASH_BAR_BLOCKS * (SPLASH_BAR_BLOCK + SPLASH_BAR_GAP));
    int start = (int)((g_pulse_frame * 7U) % (uint32_t)period);
    int x0 = inner_x + start - (SPLASH_BAR_BLOCKS * (SPLASH_BAR_BLOCK + SPLASH_BAR_GAP));

    draw_bar_frame();

    for (int i = 0; i < SPLASH_BAR_BLOCKS; i++) {
        int x = x0 + i * (SPLASH_BAR_BLOCK + SPLASH_BAR_GAP);
        int w = SPLASH_BAR_BLOCK;

        if (x >= inner_x + inner_w) continue;
        if (x + w <= inner_x) continue;

        if (x < inner_x) {
            w -= inner_x - x;
            x = inner_x;
        }
        if (x + w > inner_x + inner_w) {
            w = inner_x + inner_w - x;
        }

        if (w <= 0) continue;

        gfx_fill_rect_rgb(x, inner_y, w, inner_h, 0x0028B040);
        gfx_fill_rect_rgb(x, inner_y, w, inner_h / 3, 0x0070F080);
    }
}

static void compute_layout(void) {
    const gfx_info_t *info = gfx_get_info();
    int max_w;
    int max_h;
    int scale_x;
    int scale_y;
    int scale;

    if (!info) return;

    max_w = (int)info->width - 80;
    max_h = ((int)info->height / 2) - 40;
    if (max_w < 32) max_w = (int)info->width;
    if (max_h < 32) max_h = (int)info->height / 3;

    scale_x = max_w / (int)SPLASH_LOGO_SRC_W;
    scale_y = max_h / (int)SPLASH_LOGO_SRC_H;
    scale = scale_x < scale_y ? scale_x : scale_y;

    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;

    g_logo_w = (int)SPLASH_LOGO_SRC_W * scale;
    g_logo_h = (int)SPLASH_LOGO_SRC_H * scale;

    g_logo_x = ((int)info->width - g_logo_w) / 2;
    g_logo_y = ((int)info->height - g_logo_h) / 2 - 42;
    if (g_logo_y < 20) g_logo_y = 20;

    g_bar_w = SPLASH_BAR_W;
    if (g_bar_w > (int)info->width - 40)
        g_bar_w = (int)info->width - 40;
    if (g_bar_w < 100)
        g_bar_w = 100;

    g_bar_h = SPLASH_BAR_H;
    g_bar_x = ((int)info->width - g_bar_w) / 2;
    g_bar_y = g_logo_y + g_logo_h + 34;
    if (g_bar_y + g_bar_h + 24 > (int)info->height)
        g_bar_y = (int)info->height - g_bar_h - 32;

    g_text_y = g_bar_y + g_bar_h + 10;
}

static void render_full(const char *status, uint8_t fade) {
    const gfx_info_t *info = gfx_get_info();

    if (!info || info->mode == GFX_MODE_TEXT) return;

    compute_layout();

    gfx_clear_rgb(0x00000000);
    draw_logo_scaled(fade);
    draw_bar_blocks();

    if (status)
        draw_text_centered(g_text_y, status);
}

static void fade_up(const char *status) {
    for (uint16_t f = 0; f <= 255; f += 17) {
        render_full(status, (uint8_t)f);

        for (volatile uint32_t wait = 0; wait < 260000U; wait++) {
            __asm__ volatile ("pause");
        }
    }
}

void bootsplash_show(const char *status, uint8_t progress) {
    (void)progress;

    if (!gfx_get_info() || gfx_get_info()->mode == GFX_MODE_TEXT)
        return;

    g_enabled = true;

    if (!g_drawn_once) {
        g_drawn_once = true;
        fade_up(status ? status : "LOADING");
        return;
    }

    render_full(status ? status : "LOADING", 255);
}

static bool pulse_time_ready(void) {
    uint32_t now = pit_get_ticks();

    if (now != 0) {
        if (g_last_pulse_tick != 0 &&
            now - g_last_pulse_tick < SPLASH_PULSE_TICKS) {
            return false;
        }
        g_last_pulse_tick = now;
        return true;
    }

    /*
     * Fallback por si alguien llama pulse antes del PIT.
     * Muy lento a propósito para no quemar CPU ni arruinar timings.
     */
    g_fallback_pulse_counter++;
    return (g_fallback_pulse_counter & 0x3FFFU) == 0;
}

void bootsplash_pulse(void) {
    if (!g_enabled || !g_drawn_once)
        return;

    if (!pulse_time_ready())
        return;

    g_pulse_frame++;
    draw_bar_blocks();
}

void bootsplash_disable(void) {
    g_enabled = false;
}
"""


def generate_c(width: int, height: int, pixels: list[int]) -> str:
    array = make_c_array("g_splash_logo", pixels)
    text = C_TEMPLATE
    text = text.replace("__WIDTH__", str(width))
    text = text.replace("__HEIGHT__", str(height))
    text = text.replace("__ARRAY__", array)
    return text


def main() -> None:
    script_dir = Path(__file__).resolve().parent

    if len(sys.argv) >= 2:
        bmp_path = Path(sys.argv[1])
    else:
        bmp_path = script_dir / "../assets/boot/splashlogo.bmp"

    if len(sys.argv) >= 3:
        c_path = Path(sys.argv[2])
    else:
        c_path = script_dir / "../kernel/bootsplash.c"

    if len(sys.argv) >= 4:
        h_path = Path(sys.argv[3])
    else:
        h_path = script_dir / "../kernel/include/bootsplash.h"

    bmp_path = bmp_path.resolve()
    c_path = c_path.resolve()
    h_path = h_path.resolve()

    if not bmp_path.exists():
        die(f"no existe {bmp_path}")

    width, height, pixels = read_bmp_argb(bmp_path)

    if width * height > 180000:
        die(
            f"el BMP mide {width}x{height}. Es demasiado grande para hardcodear. "
            "Usá un logo más chico, por ejemplo 200x80 o 240x100."
        )

    c_path.parent.mkdir(parents=True, exist_ok=True)
    h_path.parent.mkdir(parents=True, exist_ok=True)

    c_path.write_text(generate_c(width, height, pixels), encoding="utf-8")
    h_path.write_text(HEADER_TEXT, encoding="utf-8")

    print(f"[ok] generado {c_path}")
    print(f"[ok] generado {h_path}")
    print(f"[ok] logo {width}x{height}")
    print("Ahora ejecutá: make clean && make run")


if __name__ == "__main__":
    main()
