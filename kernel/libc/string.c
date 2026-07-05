#include "../string.h"
#include "../strings.h"
#include "../ctype.h"
#include "../include/memory.h"

void *memcpy(void *dst, const void *src, size_t n) {
    return kmemcpy(dst, src, n);
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s || n == 0) return dst;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    return kmemset(dst, c, n);
}

int memcmp(const void *a, const void *b, size_t n) {
    return kmemcmp(a, b, n);
}

size_t strlen(const char *s) {
    return kstrlen(s);
}

int strcmp(const char *a, const char *b) {
    return kstrcmp(a, b);
}

int strncmp(const char *a, const char *b, size_t n) {
    return kstrncmp(a, b, n);
}

char *strcpy(char *dst, const char *src) {
    return kstrcpy(dst, src);
}

char *strncpy(char *dst, const char *src, size_t n) {
    return kstrncpy(dst, src, n);
}

char *strcat(char *dst, const char *src) {
    return kstrcat(dst, src);
}

char *strchr(const char *s, int c) {
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return (char *)s;
        s++;
    }
    return ch == '\0' ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    char ch = (char)c;
    while (*s) {
        if (*s == ch) last = s;
        s++;
    }
    if (ch == '\0') return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    size_t needle_len;

    if (!*needle) return (char *)haystack;
    needle_len = strlen(needle);
    while (*haystack) {
        if (strncmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
        haystack++;
    }
    return NULL;
}

char *strdup(const char *s) {
    size_t len;
    char *copy;

    if (!s) return NULL;
    len = strlen(s);
    copy = (char *)kmalloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

int strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    while (n && *a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
        n--;
    }
    if (n == 0) return 0;
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}
