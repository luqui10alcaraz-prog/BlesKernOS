#ifndef BK_CLIPBOARD_H
#define BK_CLIPBOARD_H

#include "types.h"

#define BK_CLIPBOARD_TEXT_CAPACITY 4096U

bool bk_clipboard_set_text(const char *text);
uint32_t bk_clipboard_get_text(char *buffer, uint32_t capacity);
void bk_clipboard_clear(void);
uint32_t bk_clipboard_generation(void);

#endif
