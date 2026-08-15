#include "include/usercopy.h"
#include "include/paging.h"
#include "include/memory.h"

bool user_access_ok(const void *pointer, uint32_t length, bool write) {
    return paging_user_range_ok(pointer, length, write);
}

bool copy_from_user(void *destination, const void *source, uint32_t length) {
    if (!destination || (!source && length)) return false;
    if (!length) return true;
    if (!user_access_ok(source, length, false)) return false;
    kmemcpy(destination, source, length);
    return true;
}

bool copy_to_user(void *destination, const void *source, uint32_t length) {
    if ((!destination && length) || !source) return false;
    if (!length) return true;
    if (!user_access_ok(destination, length, true)) return false;
    kmemcpy(destination, source, length);
    return true;
}

bool copy_string_from_user(char *destination, uint32_t capacity,
                           const char *source) {
    if (!destination || !source || capacity < 2U) return false;
    for (uint32_t i = 0U; i < capacity; i++) {
        char value;
        if (!copy_from_user(&value, source + i, 1U)) return false;
        destination[i] = value;
        if (!value) return true;
    }
    destination[capacity - 1U] = '\0';
    return false;
}
