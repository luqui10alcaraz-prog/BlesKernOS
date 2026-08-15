#ifndef GUI_IMAGE_H
#define GUI_IMAGE_H

#include "../kernel/include/types.h"

typedef struct {
    uint32_t *pixels;
    uint16_t width;
    uint16_t height;
} gui_image_t;

typedef struct {
    gui_image_t *frames;
    uint16_t *delays_cs;
    uint16_t frame_count;
    uint16_t width;
    uint16_t height;
} gui_gif_animation_t;

/* Decodes uncompressed 24/32-bit BMP and the first GIF frame to ARGB. */
bool gui_bmp_decode(gui_image_t *image, const uint8_t *data, uint32_t length);
bool gui_png_decode(gui_image_t *image, const uint8_t *data, uint32_t length);
bool gui_gif_decode(gui_image_t *image, const uint8_t *data, uint32_t length);
bool gui_jpeg_decode(gui_image_t *image, const uint8_t *data, uint32_t length);
bool gui_svg_decode(gui_image_t *image, const uint8_t *data, uint32_t length);

/* Reads and decodes a GIF from the virtual filesystem. */
bool gui_gif_load(gui_image_t *image, const char *path);
bool gui_gif_load_animation(gui_gif_animation_t *animation, const char *path);
bool gui_gif_load_animation_limited(gui_gif_animation_t *animation,
                                    const char *path, uint16_t max_frames);

void gui_image_free(gui_image_t *image);
void gui_gif_animation_free(gui_gif_animation_t *animation);

#endif
