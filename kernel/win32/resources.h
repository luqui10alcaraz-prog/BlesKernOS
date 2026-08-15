#ifndef BLESKERNOS_WIN32_RESOURCES_H
#define BLESKERNOS_WIN32_RESOURCES_H

#include "../include/types.h"

#define WIN32_RT_CURSOR       1U
#define WIN32_RT_BITMAP       2U
#define WIN32_RT_ICON         3U
#define WIN32_RT_MENU         4U
#define WIN32_RT_DIALOG       5U
#define WIN32_RT_STRING       6U
#define WIN32_RT_ACCELERATOR  9U
#define WIN32_RT_GROUP_CURSOR 12U
#define WIN32_RT_GROUP_ICON   14U

void *win32_resource_find(void *module, const void *type, const void *name,
                          uint16_t language, bool exact_language);
void *win32_resource_find_w(void *module, const void *type, const void *name,
                            uint16_t language, bool exact_language);
void *win32_resource_load(void *module, void *resource);
const void *win32_resource_lock(void *loaded_resource);
uint32_t win32_resource_size(void *module, void *resource);
bool win32_resource_free(void *loaded_resource);
void *win32_icon_load(void *module, const void *name, bool wide_name,
                      int requested_width, int requested_height);
void *win32_icon_create_from_resource(const void *data, uint32_t size,
                                      int requested_width,
                                      int requested_height);
bool win32_icon_get(void *icon, const uint32_t **pixels, int *width,
                    int *height);
void *win32_icon_copy(void *icon);
bool win32_icon_destroy(void *icon);
void win32_resource_cleanup_process(uint32_t pid);

#endif
