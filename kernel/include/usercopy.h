#ifndef USERCOPY_H
#define USERCOPY_H

#include "types.h"

bool user_access_ok(const void *pointer, uint32_t length, bool write);
bool copy_from_user(void *destination, const void *source, uint32_t length);
bool copy_to_user(void *destination, const void *source, uint32_t length);
bool copy_string_from_user(char *destination, uint32_t capacity,
                           const char *source);

#endif
