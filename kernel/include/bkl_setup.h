#ifndef BKL_SETUP_H
#define BKL_SETUP_H

#include "types.h"
#include "bkl_progress.h"

bool bkl_setup_pending(void);
bool bkl_setup_extract_package(const char *path);
bool bkl_setup_get_progress(bkl_setup_progress_t *progress);
bool bkl_setup_finalize(void);

#endif
