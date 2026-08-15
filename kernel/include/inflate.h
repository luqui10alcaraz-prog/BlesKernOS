#ifndef BK_INFLATE_H
#define BK_INFLATE_H

#include "types.h"

int32_t inflate_raw(const void *input, uint32_t input_length,
                    void *output, uint32_t output_capacity);
int32_t inflate_zlib(const void *input, uint32_t input_length,
                     void *output, uint32_t output_capacity);
int32_t inflate_gzip(const void *input, uint32_t input_length,
                     void *output, uint32_t output_capacity);

#endif
