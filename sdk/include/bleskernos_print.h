#ifndef BLESKERNOS_PRINT_H
#define BLESKERNOS_PRINT_H

#include <bleskernos_api.h>

#define BK_PRINT_API_VERSION 1U
#define BK_PRINT_DEFAULT_PRINTER "PSFILE"
#define BK_PRINT_A4_WIDTH_POINTS 595U
#define BK_PRINT_A4_HEIGHT_POINTS 842U
#define BK_PRINT_LETTER_WIDTH_POINTS 612U
#define BK_PRINT_LETTER_HEIGHT_POINTS 792U

#define BK_PRINT_FONT_BOLD      0x00000001U
#define BK_PRINT_FONT_ITALIC    0x00000002U
#define BK_PRINT_FONT_UNDERLINE 0x00000004U

/* Formato intermedio BPJ1. Los enteros se serializan little-endian. */
#define BK_PRINT_JOB_MAGIC_0 'B'
#define BK_PRINT_JOB_MAGIC_1 'P'
#define BK_PRINT_JOB_MAGIC_2 'J'
#define BK_PRINT_JOB_MAGIC_3 '1'
#define BK_PRINT_JOB_VERSION 1U
#define BK_PRINT_JOB_HEADER_SIZE 128U
#define BK_PRINT_JOB_MAX_SIZE (8U * 1024U * 1024U)

typedef enum {
    BK_PRINT_FONT_SERIF = 0,
    BK_PRINT_FONT_SANS = 1,
    BK_PRINT_FONT_MONO = 2
} bk_print_font_family_t;

typedef enum {
    BK_PRINT_CMD_BEGIN_PAGE = 1,
    BK_PRINT_CMD_SET_FONT = 2,
    BK_PRINT_CMD_TEXT = 3,
    BK_PRINT_CMD_LINE = 4,
    BK_PRINT_CMD_BITMAP_MONO = 5,
    BK_PRINT_CMD_END_PAGE = 6
} bk_print_command_type_t;

typedef struct bk_print_job bk_print_job_t;

/* print_begin crea y abre una pagina A4 de 300 dpi logicos. */
bk_print_job_t *print_begin(const char *title, const char *printer_id);
bool print_begin_page(bk_print_job_t *job, uint32_t width_points,
                      uint32_t height_points, uint32_t raster_dpi);
bool print_set_font(bk_print_job_t *job, bk_print_font_family_t family,
                    uint16_t points, uint32_t flags);
bool print_text(bk_print_job_t *job, int32_t x_points, int32_t y_points,
                const char *text);
bool print_line(bk_print_job_t *job, int32_t x1_points, int32_t y1_points,
                int32_t x2_points, int32_t y2_points,
                uint16_t width_points);
/* Bitmap 1 bpp, MSB primero, filas de stride_bytes. x/y estan en puntos. */
bool print_bitmap_mono(bk_print_job_t *job, int32_t x_points,
                       int32_t y_points, uint32_t width_pixels,
                       uint32_t height_pixels, uint32_t stride_bytes,
                       uint16_t source_dpi, const void *pixels);
bool print_end_page(bk_print_job_t *job);
/* Escribe primero .TMP y publica mediante rename atomico a .BPJ.
   Siempre consume job, tanto si devuelve true como false. */
bool print_submit(bk_print_job_t *job);
/* Use print_cancel solamente para trabajos que aun no fueron enviados. */
void print_cancel(bk_print_job_t *job);

#endif
