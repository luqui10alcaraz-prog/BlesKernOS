#ifndef BKL_PROGRESS_H
#define BKL_PROGRESS_H

#include "types.h"

#define BKL_SETUP_PROGRESS_PATH_MAX 260U

typedef enum {
    BKL_SETUP_PHASE_IDLE = 0,
    BKL_SETUP_PHASE_OPENING,
    BKL_SETUP_PHASE_READING_HEADER,
    BKL_SETUP_PHASE_DECODING,
    BKL_SETUP_PHASE_VERIFYING,
    BKL_SETUP_PHASE_COMMITTING,
    BKL_SETUP_PHASE_FILE_DONE,
    BKL_SETUP_PHASE_PACKAGE_DONE,
    BKL_SETUP_PHASE_ERROR
} bkl_setup_phase_t;

typedef struct {
    uint32_t generation;
    uint32_t active;
    uint32_t phase;
    uint32_t file_index;
    uint32_t file_total;
    uint32_t file_bytes_done;
    uint32_t file_bytes_total;
    uint32_t stream_bytes_read;
    uint32_t part_index;
    uint32_t part_total;
    char path[BKL_SETUP_PROGRESS_PATH_MAX];
} bkl_setup_progress_t;

#endif
