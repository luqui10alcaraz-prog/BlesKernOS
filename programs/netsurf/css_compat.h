#ifndef BLESKERNOS_NETSURF_CSS_COMPAT_H
#define BLESKERNOS_NETSURF_CSS_COMPAT_H

#include <stdbool.h>
#include <stdint.h>

#define NSBK_CSS_VARIABLE_MAX 128U
#define NSBK_CSS_VARIABLE_NAME_MAX 64U
#define NSBK_CSS_VARIABLE_VALUE_MAX 320U

typedef struct {
    char name[NSBK_CSS_VARIABLE_NAME_MAX];
    char value[NSBK_CSS_VARIABLE_VALUE_MAX];
} nsbk_css_variable_t;

typedef struct {
    nsbk_css_variable_t variables[NSBK_CSS_VARIABLE_MAX];
    uint32_t variable_count;
    int32_t viewport_width;
    int32_t viewport_height;
} nsbk_css_compat_t;

void nsbk_css_compat_init(nsbk_css_compat_t *compat, int32_t viewport_width,
                          int32_t viewport_height);
bool nsbk_css_compat_process(nsbk_css_compat_t *compat, const uint8_t *input,
                             uint32_t input_length, uint8_t **output,
                             uint32_t *output_length);

#endif
