#ifndef BK_RAGE128_ENGINE_H
#define BK_RAGE128_ENGINE_H

#include "types.h"

#define BK_R128_ENGINE_2D_ABI_VERSION 1U

typedef enum {
    R128_ENGINE_CONTEXT_NONE = 0,
    R128_ENGINE_CONTEXT_2D = 1,
    R128_ENGINE_CONTEXT_3D = 2,
} r128_engine_context_t;

typedef struct {
    uint32_t abi_version;
    uint32_t descriptor_size;
    const char *name;

    /* Called with the shared engine lock held. It must leave PM4 in a state
     * where direct 2D register submission and CPU framebuffer access are safe. */
    bool (*enter_2d)(void);
    bool (*wait_2d_idle)(void);

    /* Source offset is relative to BAR0/VRAM. Destination is the active
     * scanout owned by the 2D driver. The callback only submits the blit; the
     * coordinator waits for completion before releasing the engine. */
    bool (*blit_vram32)(uint32_t source_offset,
                        uint32_t source_pitch_bytes,
                        int src_x, int src_y,
                        int dst_x, int dst_y,
                        int width, int height);

    /* Lowest VRAM byte that another client may allocate. This includes the
     * visible scanout and any permanently reserved compositor backbuffer. */
    uint32_t (*reserved_vram_end)(void);
} r128_engine_2d_ops_t;

bool r128_engine_register_2d(const r128_engine_2d_ops_t *ops);
void r128_engine_unregister_2d(const r128_engine_2d_ops_t *ops);

bool r128_engine_acquire_2d(void);
bool r128_engine_acquire_3d(void);
void r128_engine_release_2d(void);
void r128_engine_release_3d(void);

bool r128_engine_present_vram32(uint32_t source_offset,
                                uint32_t source_pitch_bytes,
                                int src_x, int src_y,
                                int dst_x, int dst_y,
                                int width, int height);
uint32_t r128_engine_reserved_vram_end(void);
r128_engine_context_t r128_engine_context(void);
void r128_engine_report(const char *reason);

#endif
