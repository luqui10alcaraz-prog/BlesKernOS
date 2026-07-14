#include "../stdlib.h"
#include "../string.h"
#include "../errno.h"
#include "../limits.h"
#include "../include/memory.h"
#include "../include/task.h"
#include "../include/vfs.h"
#include "../stdio.h"

int errno = 0;

static void (*g_exit_handler)(int status) = NULL;

void libc_set_exit_handler(void (*handler)(int status)) {
    g_exit_handler = handler;
}

void *malloc(size_t size) {
    return kmalloc(size);
}

void *calloc(size_t count, size_t size) {
    if (count == 0 || size == 0) return kmalloc(0);
    if (count > UINT_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }
    return kzalloc(count * size);
}

void *realloc(void *ptr, size_t size) {
    return krealloc(ptr, size);
}

void free(void *ptr) {
    kfree(ptr);
}

void exit(int status) {
    kprintf("[libc] exit(%d)\n", status);
    if (g_exit_handler) g_exit_handler(status);
    task_exit();
}

int abs(int value) {
    return value < 0 ? -value : value;
}

int atoi(const char *s) {
    int sign = 1;
    int value = 0;

    if (!s) return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        s++;
    }
    return value * sign;
}

double atof(const char *s) {
    double sign = 1.0;
    double value = 0.0;
    double scale = 1.0;

    if (!s) return 0.0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '-') {
        sign = -1.0;
        s++;
    } else if (*s == '+') {
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        value = value * 10.0 + (double)(*s - '0');
        s++;
    }

    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            scale *= 10.0;
            value += (double)(*s - '0') / scale;
            s++;
        }
    }

    return value * sign;
}

char *getenv(const char *name) {
    (void)name;
    return NULL;
}

int system(const char *command) {
    (void)command;
    errno = EINVAL;
    return -1;
}

int remove(const char *path) {
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    if (!vfs_remove(path)) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int rename(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) {
        errno = EINVAL;
        return -1;
    }

    if (!vfs_rename(old_path, new_path)) {
        errno = EIO;
        return -1;
    }
    return 0;
}
