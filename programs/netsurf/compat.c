#include <stddef.h>
#include <stdint.h>
#include <bleskernos_api.h>
#include <stdlib.h>
#include <string.h>

void *memchr(const void *memory, int value, size_t count) {
    const uint8_t *bytes = (const uint8_t *)memory;
    uint8_t wanted = (uint8_t)value;
    size_t i;
    for (i = 0; i < count; i++) {
        if (bytes[i] == wanted) return (void *)(bytes + i);
    }
    return NULL;
}

int tolower(int character) {
    if (character >= 'A' && character <= 'Z') return character + ('a' - 'A');
    return character;
}

int toupper(int character) {
    if (character >= 'a' && character <= 'z') return character - ('a' - 'A');
    return character;
}

int isdigit(int character) {
    return character >= '0' && character <= '9';
}

int islower(int character) {
    return character >= 'a' && character <= 'z';
}

int isupper(int character) {
    return character >= 'A' && character <= 'Z';
}

int isalpha(int character) {
    return islower(character) || isupper(character);
}

int isalnum(int character) {
    return isalpha(character) || isdigit(character);
}

int isspace(int character) {
    return character == ' ' || character == '\t' || character == '\n' ||
           character == '\r' || character == '\f' || character == '\v';
}

int isprint(int character) {
    return character >= 0x20 && character <= 0x7e;
}

void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *)) {
    size_t low = 0;
    size_t high = count;
    const uint8_t *bytes = (const uint8_t *)base;
    if (!key || !base || !compare || size == 0U) return NULL;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        const void *entry = bytes + middle * size;
        int relation = compare(key, entry);
        if (relation < 0) high = middle;
        else if (relation > 0) low = middle + 1U;
        else return (void *)entry;
    }
    return NULL;
}

char *strdup(const char *text) {
    size_t length;
    char *copy;
    if (!text) return NULL;
    length = strlen(text) + 1U;
    copy = (char *)malloc(length);
    if (!copy) return NULL;
    memcpy(copy, text, length);
    return copy;
}

#ifndef NDEBUG
void nsbk_assert_fail(const char *expression, const char *file,
                      unsigned int line) {
    (void)file;
    (void)line;
    bk_sys_log(expression ? expression : "@H232DB7E5");
}
#endif

int abs(int value) { return value < 0 ? -value : value; }

void abort(void) {
    bk_proc_exit();
    for (;;) { }
}
