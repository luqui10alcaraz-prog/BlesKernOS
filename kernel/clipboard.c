#include "include/clipboard.h"
#include "include/task.h"
#include "string.h"

static char g_clipboard_text[BK_CLIPBOARD_TEXT_CAPACITY];
static uint32_t g_clipboard_generation = 1U;

bool bk_clipboard_set_text(const char *text) {
    uint32_t length = 0;
    if (!text) return false;
    while (text[length] && length + 1U < BK_CLIPBOARD_TEXT_CAPACITY) length++;
    task_preempt_disable();
    if (length) memcpy(g_clipboard_text, text, length);
    g_clipboard_text[length] = '\0';
    g_clipboard_generation++;
    if (!g_clipboard_generation) g_clipboard_generation = 1U;
    task_preempt_enable();
    return true;
}

uint32_t bk_clipboard_get_text(char *buffer, uint32_t capacity) {
    uint32_t length = 0;
    uint32_t copied;
    if (!buffer || !capacity) return 0;
    task_preempt_disable();
    while (g_clipboard_text[length]) length++;
    copied = length < capacity - 1U ? length : capacity - 1U;
    if (copied) memcpy(buffer, g_clipboard_text, copied);
    buffer[copied] = '\0';
    task_preempt_enable();
    return copied;
}

void bk_clipboard_clear(void) {
    task_preempt_disable();
    g_clipboard_text[0] = '\0';
    g_clipboard_generation++;
    if (!g_clipboard_generation) g_clipboard_generation = 1U;
    task_preempt_enable();
}

uint32_t bk_clipboard_generation(void) {
    uint32_t generation;
    task_preempt_disable();
    generation = g_clipboard_generation;
    task_preempt_enable();
    return generation;
}
