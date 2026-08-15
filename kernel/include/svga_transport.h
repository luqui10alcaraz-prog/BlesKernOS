#ifndef BK_SVGA_TRANSPORT_H
#define BK_SVGA_TRANSPORT_H

#include "types.h"

/* Interfaz privada entre VMWARESVGA.DVR y extensiones como VMWARESVGA3D.DVR. */
#define BK_SVGA_TRANSPORT_ABI_VERSION 1U
#define BK_SVGA_TRANSPORT_INVALID_ALLOCATION 0U

typedef struct bk_svga_transport_ops {
    uint32_t abi_version;
    uint32_t descriptor_size;
    const char *name;
    void *context;

    bool (*is_active)(void *context);
    uint32_t (*generation)(void *context);
    uint32_t (*device_capabilities)(void *context);
    uint32_t (*fifo_capabilities)(void *context);
    uint32_t (*fifo_register_count)(void *context);
    uint32_t (*fifo_register_read)(void *context, uint32_t index);
    bool (*fifo_register_write)(void *context, uint32_t index,
                                uint32_t value);

    bool (*emit)(void *context, const void *bytes, uint32_t byte_count);
    bool (*submit)(void *context, bool wait, uint32_t *fence_out);
    bool (*wait_fence)(void *context, uint32_t fence);

    bool (*vram_allocate)(void *context, uint32_t size, uint32_t alignment,
                          uint32_t *handle_out, uint32_t *offset_out);
    bool (*vram_free)(void *context, uint32_t handle);
    void *(*vram_pointer)(void *context, uint32_t offset, uint32_t size);

    bool (*display_info)(void *context, uint32_t *width, uint32_t *height,
                         uint32_t *pitch, uint32_t *framebuffer_offset);
} bk_svga_transport_ops_t;

bool svga_transport_register(const bk_svga_transport_ops_t *ops);
bool svga_transport_unregister(const bk_svga_transport_ops_t *ops);
const bk_svga_transport_ops_t *svga_transport_get(void);

#endif
