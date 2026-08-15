#include "../../include/gfx_driver.h"
#include "../../include/gfx3d_driver.h"
#include "../../include/driver.h"
#include "../../include/pci.h"
#include "../../include/memory.h"
#include "../../include/vga.h"
#include "../../include/pic.h"
#include "../../include/pit.h"
#include "../../include/task.h"

const bk_driver_module_t *bleskernos_driver_query(void);

/*
 * BlesKernOS VirtIO GPU (modern PCI transport)
 *
 * Implemented:
 *   - modern VirtIO PCI capability discovery
 *   - split virtqueues in polling mode (controlq + cursorq)
 *   - 2D resources backed by guest RAM
 *   - scanout, dirty-region transfer and resource flush
 *   - hardware cursor resource/update/move
 *   - software Fill/BitBlt/off-screen surfaces followed by host upload
 *   - VirGL feature/capset discovery, capset download and context lifecycle
 *
 * VIRTIO_GPU_F_VIRGL only exposes the 3D transport. Rendering triangles
 * requires a VirGL/Gallium command-stream encoder (shader and pipeline state,
 * vertex elements, resources and contexts). This module deliberately does not
 * register a fake gfx3d backend. TinyGL can still render into the framebuffer;
 * this driver then transfers the completed image to the host scanout.
 */

#define VIRTIO_PCI_VENDOR_ID               0x1AF4U
#define VIRTIO_GPU_DEVICE_ID_MODERN        0x1050U
#define VIRTIO_GPU_DEVICE_ID_TRANSITIONAL  0x1010U

#define PCI_STATUS_CAP_LIST                0x0010U
#define PCI_CAP_ID_VENDOR                  0x09U
#define PCI_CAP_PTR                        0x34U

#define VIRTIO_PCI_CAP_COMMON_CFG          1U
#define VIRTIO_PCI_CAP_NOTIFY_CFG          2U
#define VIRTIO_PCI_CAP_ISR_CFG             3U
#define VIRTIO_PCI_CAP_DEVICE_CFG          4U

#define VIRTIO_STATUS_ACKNOWLEDGE           1U
#define VIRTIO_STATUS_DRIVER                2U
#define VIRTIO_STATUS_DRIVER_OK             4U
#define VIRTIO_STATUS_FEATURES_OK           8U
#define VIRTIO_STATUS_FAILED              128U

#define VIRTIO_GPU_F_VIRGL                  (1U << 0)
#define VIRTIO_GPU_F_EDID                   (1U << 1)
#define VIRTIO_GPU_F_CONTEXT_INIT           (1U << 4)
#define VIRTIO_F_VERSION_1_HIGH             (1U << 0)

#define VIRTIO_GPU_CONTROLQ                 0U
#define VIRTIO_GPU_CURSORQ                  1U
#define VIRTIO_GPU_MAX_QUEUE_SIZE          64U
#define VIRTIO_GPU_QUEUE_SPIN_LIMIT 100000000U
#define VIRTIO_GPU_QUEUE_TIMEOUT_SECONDS    3U
#define VIRTIO_GPU_QUEUE_IOWAIT_INTERVAL 1024U
#define VIRTIO_GPU_MAX_DIRTY_RECTS         32U
#define VIRTIO_GPU_MAX_SURFACES            32U
#define VIRTIO_GPU_CURSOR_SIZE             64U
#define VIRTIO_GPU_PAGE_SIZE             4096U
#define VIRTIO_GPU_MAX_CAPSETS              8U
#define VIRTIO_GPU_MAX_3D_SURFACES          96U

#define VIRTQ_DESC_F_NEXT                   1U
#define VIRTQ_DESC_F_WRITE                  2U
#define VIRTQ_NO_VECTOR                0xFFFFU

#define VIRTQ_ERROR_NONE                    0U
#define VIRTQ_ERROR_NOT_READY               1U
#define VIRTQ_ERROR_BUSY                    2U
#define VIRTQ_ERROR_TIMEOUT                 3U
#define VIRTQ_ERROR_BAD_USED_ID             4U

#define VIRTIO_GPU_FLAG_FENCE               1U

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO       0x0100U
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D     0x0101U
#define VIRTIO_GPU_CMD_RESOURCE_UNREF         0x0102U
#define VIRTIO_GPU_CMD_SET_SCANOUT            0x0103U
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH         0x0104U
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D    0x0105U
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106U
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107U
#define VIRTIO_GPU_CMD_GET_CAPSET_INFO        0x0108U
#define VIRTIO_GPU_CMD_GET_CAPSET             0x0109U
#define VIRTIO_GPU_CMD_CTX_CREATE             0x0200U
#define VIRTIO_GPU_CMD_CTX_DESTROY            0x0201U
#define VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE    0x0202U
#define VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE    0x0203U
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_3D     0x0204U
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D    0x0205U
#define VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D  0x0206U
#define VIRTIO_GPU_CMD_SUBMIT_3D              0x0207U
#define VIRTIO_GPU_CMD_UPDATE_CURSOR          0x0300U
#define VIRTIO_GPU_CMD_MOVE_CURSOR            0x0301U

#define VIRTIO_GPU_RESP_OK_NODATA             0x1100U
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO       0x1101U
#define VIRTIO_GPU_RESP_OK_CAPSET_INFO        0x1102U
#define VIRTIO_GPU_RESP_OK_CAPSET             0x1103U
#define VIRTIO_GPU_RESP_ERR_UNSPEC            0x1200U

#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM      1U
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM      2U

/* Gallium/VirGL wire constants used by the transport self-test. */
#define VIRGL_TARGET_TEXTURE_2D                2U
#define VIRGL_BIND_RENDER_TARGET              (1U << 1)
#define VIRGL_BIND_SAMPLER_VIEW               (1U << 3)
#define VIRGL_BIND_VERTEX_BUFFER              (1U << 4)
#define VIRGL_BIND_DEPTH_STENCIL              (1U << 0)
#define VIRGL_BIND_SCANOUT                    (1U << 18)
#define VIRGL_TARGET_BUFFER                    0U
#define VIRGL_OBJECT_SURFACE                   8U
#define VIRGL_OBJECT_BLEND                     1U
#define VIRGL_OBJECT_RASTERIZER                2U
#define VIRGL_OBJECT_DSA                       3U
#define VIRGL_OBJECT_SHADER                    4U
#define VIRGL_OBJECT_VERTEX_ELEMENTS           5U
#define VIRGL_OBJECT_SAMPLER_VIEW              6U
#define VIRGL_OBJECT_SAMPLER_STATE             7U
#define VIRGL_CCMD_CREATE_OBJECT               1U
#define VIRGL_CCMD_BIND_OBJECT                 2U
#define VIRGL_CCMD_DESTROY_OBJECT              3U
#define VIRGL_CCMD_SET_VIEWPORT_STATE          4U
#define VIRGL_CCMD_SET_FRAMEBUFFER_STATE       5U
#define VIRGL_CCMD_SET_VERTEX_BUFFERS          6U
#define VIRGL_CCMD_CLEAR                       7U
#define VIRGL_CCMD_DRAW_VBO                    8U
#define VIRGL_CCMD_SET_SAMPLER_VIEWS           10U
#define VIRGL_CCMD_BIND_SAMPLER_STATES         18U
#define VIRGL_CCMD_BIND_SHADER                 31U
#define VIRGL_CCMD_LINK_SHADER                 52U
#define VIRGL_CLEAR_COLOR0                     (1U << 2)
#define VIRGL_CLEAR_DEPTH                      (1U << 0)
#define VIRGL_CMD0(cmd,obj,len) ((cmd)|((obj)<<8)|((len)<<16))
#define VIRGL_FORMAT_Z16_UNORM                 16U
#define VIRGL_FORMAT_R32G32B32A32_FLOAT        31U
#define VIRGL_FORMAT_R8_UNORM                  64U

#define ALIGN_UP(v, a) (((v) + ((a) - 1U)) & ~((a) - 1U))

/* Offsets in struct virtio_pci_common_cfg. */
#define COMMON_DFSELECT       0U
#define COMMON_DF             4U
#define COMMON_GFSELECT       8U
#define COMMON_GF            12U
#define COMMON_MSIX          16U
#define COMMON_NUMQ          18U
#define COMMON_STATUS        20U
#define COMMON_QSELECT       22U
#define COMMON_QSIZE         24U
#define COMMON_QMSIX         26U
#define COMMON_QENABLE       28U
#define COMMON_QNOTIFY_OFF   30U
#define COMMON_QDESC         32U
#define COMMON_QDRIVER       40U
#define COMMON_QDEVICE       48U

#define GPU_CONFIG_NUM_SCANOUTS  8U
#define GPU_CONFIG_NUM_CAPSETS  12U

typedef struct PACKED {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct PACKED {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[1];
} virtq_avail_t;

typedef struct PACKED {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

typedef struct PACKED {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[1];
} virtq_used_t;

typedef struct {
    bool ready;
    bool busy;
    uint16_t index;
    uint16_t size;
    uint16_t notify_off;
    uint16_t avail_shadow;
    uint16_t used_shadow;
    void *desc_raw;
    void *avail_raw;
    void *used_raw;
    virtq_desc_t *desc;
    volatile virtq_avail_t *avail;
    volatile virtq_used_t *used;
    volatile uint16_t *notify;
    uint8_t last_error;
    uint32_t last_used_id;
} virtio_queue_t;

typedef struct PACKED {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint8_t ring_idx;
    uint8_t padding[3];
} virtio_gpu_ctrl_hdr_t;

typedef struct PACKED {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} virtio_gpu_rect_t;

typedef struct PACKED {
    virtio_gpu_rect_t r;
    uint32_t enabled;
    uint32_t flags;
} virtio_gpu_display_one_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_display_one_t pmodes[16];
} virtio_gpu_resp_display_info_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} virtio_gpu_resource_create_2d_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t padding;
} virtio_gpu_resource_unref_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t scanout_id;
    uint32_t resource_id;
} virtio_gpu_set_scanout_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t resource_id;
    uint32_t padding;
} virtio_gpu_resource_flush_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} virtio_gpu_transfer_to_host_2d_t;

typedef struct PACKED {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} virtio_gpu_mem_entry_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    virtio_gpu_mem_entry_t entry;
} virtio_gpu_resource_attach_backing_one_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t capset_index;
    uint32_t padding;
} virtio_gpu_get_capset_info_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t capset_id;
    uint32_t capset_max_version;
    uint32_t capset_max_size;
    uint32_t padding;
} virtio_gpu_resp_capset_info_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t capset_id;
    uint32_t capset_version;
} virtio_gpu_get_capset_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t nlen;
    uint32_t context_init;
    char debug_name[64];
} virtio_gpu_ctx_create_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t target;
    uint32_t format;
    uint32_t bind;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t array_size;
    uint32_t last_level;
    uint32_t nr_samples;
    uint32_t flags;
    uint32_t padding;
} virtio_gpu_resource_create_3d_t;

typedef struct PACKED {
    uint32_t x, y, z, w, h, d;
} virtio_gpu_box_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_box_t box;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t level;
    uint32_t stride;
    uint32_t layer_stride;
} virtio_gpu_transfer_host_3d_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t padding;
} virtio_gpu_ctx_resource_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t size;
    uint32_t padding;
} virtio_gpu_cmd_submit_t;

typedef struct PACKED {
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
    uint32_t padding;
} virtio_gpu_cursor_pos_t;

typedef struct PACKED {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_cursor_pos_t pos;
    uint32_t resource_id;
    uint32_t hot_x;
    uint32_t hot_y;
    uint32_t padding;
} virtio_gpu_update_cursor_t;

#define VIRTIO_STATIC_ASSERT(name, expr) \
    typedef char virtio_static_assert_##name[(expr) ? 1 : -1]
VIRTIO_STATIC_ASSERT(desc_size, sizeof(virtq_desc_t) == 16U);
VIRTIO_STATIC_ASSERT(ctrl_hdr_size, sizeof(virtio_gpu_ctrl_hdr_t) == 24U);
VIRTIO_STATIC_ASSERT(rect_size, sizeof(virtio_gpu_rect_t) == 16U);
VIRTIO_STATIC_ASSERT(display_one_size, sizeof(virtio_gpu_display_one_t) == 24U);
VIRTIO_STATIC_ASSERT(display_info_size, sizeof(virtio_gpu_resp_display_info_t) == 408U);
VIRTIO_STATIC_ASSERT(create_2d_size, sizeof(virtio_gpu_resource_create_2d_t) == 40U);
VIRTIO_STATIC_ASSERT(unref_size, sizeof(virtio_gpu_resource_unref_t) == 32U);
VIRTIO_STATIC_ASSERT(set_scanout_size, sizeof(virtio_gpu_set_scanout_t) == 48U);
VIRTIO_STATIC_ASSERT(flush_size, sizeof(virtio_gpu_resource_flush_t) == 48U);
VIRTIO_STATIC_ASSERT(transfer_2d_size, sizeof(virtio_gpu_transfer_to_host_2d_t) == 56U);
VIRTIO_STATIC_ASSERT(attach_one_size, sizeof(virtio_gpu_resource_attach_backing_one_t) == 48U);
VIRTIO_STATIC_ASSERT(cursor_cmd_size, sizeof(virtio_gpu_update_cursor_t) == 56U);
VIRTIO_STATIC_ASSERT(create_3d_size, sizeof(virtio_gpu_resource_create_3d_t) == 72U);
VIRTIO_STATIC_ASSERT(transfer_3d_size, sizeof(virtio_gpu_transfer_host_3d_t) == 72U);
VIRTIO_STATIC_ASSERT(ctx_resource_size, sizeof(virtio_gpu_ctx_resource_t) == 32U);
VIRTIO_STATIC_ASSERT(submit_3d_size, sizeof(virtio_gpu_cmd_submit_t) == 32U);

typedef struct {
    bool used;
    uint8_t generation;
    uint16_t width;
    uint16_t height;
    uint32_t pitch;
    uint32_t size;
    uint32_t *pixels;
} virtio_gpu_surface_t;

typedef struct {
    bool used;
    uint8_t generation;
    uint16_t width,height;
    gfx3d_format_t format;
    uint32_t flags,resource,surface_object,sampler_view_object;
    void *raw;
    uint32_t *pixels;
    uint32_t bytes;
    uint32_t depth_resource,depth_surface_object;
    void *depth_raw,*depth_pixels;
} virtio_gpu_3d_surface_t;

typedef struct {
    const pci_device_t *pci;
    volatile uint8_t *common;
    volatile uint8_t *notify_base;
    volatile uint8_t *device_cfg;
    uint32_t common_length;
    uint32_t notify_length;
    uint32_t device_cfg_length;
    uint32_t notify_multiplier;
    bool transport_mapped;
    bool device_ready;
    bool active;
    bool virgl_available;
    bool context_init_available;
    bool virgl_context_ready;
    bool virgl_commands_ready;
    bool edid_available;
    uint32_t device_features_low;
    uint32_t num_scanouts;
    uint32_t num_capsets;
    uint32_t virgl_capset_id;
    uint32_t virgl_capset_version;
    uint32_t virgl_capset_size;
    uint32_t virgl_capset_checksum;
    uint32_t virgl_context_id;

    virtio_queue_t controlq;
    virtio_queue_t cursorq;

    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    void *framebuffer_raw;
    uint32_t *framebuffer;
    uint32_t framebuffer_size;
    uint32_t display_resource;
    uint32_t scanout_resource;
    uint32_t next_resource;

    gfx_rect_t dirty[VIRTIO_GPU_MAX_DIRTY_RECTS];
    uint32_t dirty_count;
    uint32_t next_fence;
    uint32_t completed_fence;

    void *cursor_raw;
    uint32_t *cursor_pixels;
    uint32_t cursor_resource;
    uint16_t cursor_width;
    uint16_t cursor_height;
    uint16_t cursor_hot_x;
    uint16_t cursor_hot_y;
    int32_t cursor_x;
    int32_t cursor_y;
    bool cursor_defined;
    bool cursor_visible;
    bool cursor_position_valid;
    bool cursor_host_defined;

    virtio_gpu_surface_t surfaces[VIRTIO_GPU_MAX_SURFACES];
    virtio_gpu_3d_surface_t surfaces3d[VIRTIO_GPU_MAX_3D_SURFACES];
    bool virgl_pipeline_ready;
    gfx3d_surface_handle_t virgl_target;
    uint32_t virgl_vbo_resource[3],virgl_vbo_capacity;
    void *virgl_vbo_raw[3],*virgl_vbo[3];
    uint32_t virgl_vbo_index,virgl_vbo_used[3];
    uint32_t virgl_vbo_hash[3],virgl_vbo_uploaded[3];
    uint32_t virgl_frame_commands[16384];
    uint32_t virgl_frame_command_count;
    bool virgl_frame_batching;
} virtio_gpu_state_t;

static virtio_gpu_state_t g_virtio;
static void vg_reset(void);
static bool vg_surface_destroy(gfx3d_surface_handle_t h);

static const gfx_display_mode_t g_virtio_modes[] = {
    {640, 480, 32}, {800, 600, 32}, {1024, 768, 32},
    {1152, 864, 32}, {1280, 720, 32}, {1280, 800, 32},
    {1280, 1024, 32}, {1366, 768, 32}, {1440, 900, 32},
    {1600, 900, 32}, {1680, 1050, 32}, {1920, 1080, 32}
};

static void virtio_barrier(void) {
    __asm__ volatile ("" : : : "memory");
}

static uint8_t mmio_read8(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint8_t *)(base + offset);
}

static uint16_t mmio_read16(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint16_t *)(base + offset);
}

static uint32_t mmio_read32(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint32_t *)(base + offset);
}

static void mmio_write8(volatile uint8_t *base, uint32_t offset,
                        uint8_t value) {
    *(volatile uint8_t *)(base + offset) = value;
}

static void mmio_write16(volatile uint8_t *base, uint32_t offset,
                         uint16_t value) {
    *(volatile uint16_t *)(base + offset) = value;
}

static void mmio_write32(volatile uint8_t *base, uint32_t offset,
                         uint32_t value) {
    *(volatile uint32_t *)(base + offset) = value;
}

static void mmio_write64_32(volatile uint8_t *base, uint32_t offset,
                            uint32_t value) {
    mmio_write32(base, offset, value);
    mmio_write32(base, offset + 4U, 0U);
}

static bool virtio_gpu_reset_status(void) {
    uint32_t spins = 0U;
    if (!g_virtio.common) return false;
    mmio_write8(g_virtio.common, COMMON_STATUS, 0U);
    while (mmio_read8(g_virtio.common, COMMON_STATUS) != 0U) {
        if (++spins >= VIRTIO_GPU_QUEUE_SPIN_LIMIT) return false;
        __asm__ volatile ("pause");
    }
    virtio_barrier();
    return true;
}

static uint8_t pci_read8_local(const pci_device_t *dev, uint8_t offset) {
    uint32_t value;
    if (!dev) return 0xFFU;
    value = pci_config_read32(dev->bus, dev->slot, dev->function,
                              (uint8_t)(offset & 0xFCU));
    return (uint8_t)(value >> ((offset & 3U) * 8U));
}

static bool virtio_gpu_device_matches(const pci_device_t *dev) {
    return dev && dev->vendor_id == VIRTIO_PCI_VENDOR_ID &&
           (dev->device_id == VIRTIO_GPU_DEVICE_ID_MODERN ||
            dev->device_id == VIRTIO_GPU_DEVICE_ID_TRANSITIONAL);
}

static const pci_device_t *virtio_gpu_find_device(void) {
    uint32_t count = pci_device_count();
    for (uint32_t i = 0; i < count; i++) {
        const pci_device_t *dev = pci_device_at(i);
        if (virtio_gpu_device_matches(dev)) return dev;
    }
    return NULL;
}

static bool virtio_gpu_bar_base(const pci_device_t *dev, uint8_t bar,
                                uint32_t *base_out) {
    uint32_t raw, type;
    if (!dev || !base_out || bar >= PCI_BAR_COUNT) return false;
    raw = dev->bars[bar];
    if (!raw || (raw & 1U)) return false;
    type = (raw >> 1) & 3U;
    if (type == 2U) {
        if (bar + 1U >= PCI_BAR_COUNT || dev->bars[bar + 1U] != 0U)
            return false;
    } else if (type == 1U) {
        return false;
    }
    *base_out = raw & ~0xFU;
    return *base_out != 0U;
}

static volatile uint8_t *virtio_gpu_map_capability(const pci_device_t *dev,
                                                    uint8_t bar,
                                                    uint32_t offset,
                                                    uint32_t length) {
    uint32_t base;
    if (!length || !virtio_gpu_bar_base(dev, bar, &base)) return NULL;
    if (offset > 0xFFFFFFFFU - base) return NULL;
    return (volatile uint8_t *)(uintptr_t)(base + offset);
}

static bool virtio_gpu_map_transport(void) {
    const pci_device_t *dev = g_virtio.pci;
    uint16_t status;
    uint8_t cap;
    uint32_t visits = 0;

    if (g_virtio.transport_mapped) return true;
    if (!dev) return false;
    status = pci_config_read16(dev->bus, dev->slot, dev->function, 0x06U);
    if (!(status & PCI_STATUS_CAP_LIST)) return false;
    cap = (uint8_t)(pci_read8_local(dev, PCI_CAP_PTR) & 0xFCU);

    while (cap >= 0x40U && cap <= 0xFCU && visits++ < 48U) {
        uint8_t id = pci_read8_local(dev, cap);
        uint8_t next = (uint8_t)(pci_read8_local(dev, (uint8_t)(cap + 1U)) & 0xFCU);
        uint8_t cap_len = pci_read8_local(dev, (uint8_t)(cap + 2U));
        if (id == PCI_CAP_ID_VENDOR && cap_len >= 16U) {
            uint8_t cfg_type = pci_read8_local(dev, (uint8_t)(cap + 3U));
            uint8_t bar = pci_read8_local(dev, (uint8_t)(cap + 4U));
            uint32_t offset = pci_config_read32(dev->bus, dev->slot,
                                                dev->function,
                                                (uint8_t)(cap + 8U));
            uint32_t length = pci_config_read32(dev->bus, dev->slot,
                                                dev->function,
                                                (uint8_t)(cap + 12U));
            volatile uint8_t *mapped =
                virtio_gpu_map_capability(dev, bar, offset, length);
            if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG && length >= 56U) {
                g_virtio.common = mapped;
                g_virtio.common_length = length;
            } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG &&
                       cap_len >= 20U) {
                g_virtio.notify_base = mapped;
                g_virtio.notify_length = length;
                g_virtio.notify_multiplier = pci_config_read32(
                    dev->bus, dev->slot, dev->function,
                    (uint8_t)(cap + 16U));
            } else if (cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG &&
                       length >= 16U) {
                g_virtio.device_cfg = mapped;
                g_virtio.device_cfg_length = length;
            }
        }
        if (!next || next == cap) break;
        cap = next;
    }

    g_virtio.transport_mapped = g_virtio.common && g_virtio.notify_base &&
                                g_virtio.device_cfg;
    return g_virtio.transport_mapped;
}

static void *virtio_alloc_aligned(uint32_t size, uint32_t alignment,
                                  void **raw_out) {
    uintptr_t aligned;
    void *raw;
    if (raw_out) *raw_out = NULL;
    if (!size || !alignment || (alignment & (alignment - 1U))) return NULL;
    raw = kzalloc(size + alignment - 1U);
    if (!raw) return NULL;
    /* Los recursos VirtIO sobreviven a la aplicacion que dispara un modeset
       o crea la primera superficie 3D. Si conservan el owner del proceso
       llamador, mm_release_process() libera el backing mientras el host y el
       driver todavia lo referencian. */
    if (!mm_set_allocation_owner(raw, 0U)) {
        kfree(raw);
        return NULL;
    }
    aligned = ALIGN_UP((uintptr_t)raw, alignment);
    if (raw_out) *raw_out = raw;
    return (void *)aligned;
}

static uint16_t virtio_queue_choose_size(uint16_t offered) {
    uint16_t limit = offered < VIRTIO_GPU_MAX_QUEUE_SIZE
                   ? offered : VIRTIO_GPU_MAX_QUEUE_SIZE;
    uint16_t size = 1U;
    if (limit < 2U) return 0U;
    while ((uint16_t)(size << 1) <= limit) size = (uint16_t)(size << 1);
    return size;
}

static void virtio_queue_release(virtio_queue_t *queue) {
    if (!queue) return;
    if (queue->desc_raw) kfree(queue->desc_raw);
    if (queue->avail_raw) kfree(queue->avail_raw);
    if (queue->used_raw) kfree(queue->used_raw);
    kmemset(queue, 0, sizeof(*queue));
}

static bool virtio_queue_setup(virtio_queue_t *queue, uint16_t index) {
    uint16_t offered, size, notify_off;
    uint32_t desc_bytes, avail_bytes, used_bytes, notify_byte_offset;

    if (!queue || !g_virtio.common || !g_virtio.notify_base) return false;
    mmio_write16(g_virtio.common, COMMON_QSELECT, index);
    offered = mmio_read16(g_virtio.common, COMMON_QSIZE);
    size = virtio_queue_choose_size(offered);
    if (!size || mmio_read16(g_virtio.common, COMMON_QENABLE)) return false;

    desc_bytes = (uint32_t)size * sizeof(virtq_desc_t);
    avail_bytes = 4U + (uint32_t)size * sizeof(uint16_t) + 2U;
    used_bytes = 4U + (uint32_t)size * sizeof(virtq_used_elem_t) + 2U;

    queue->desc = (virtq_desc_t *)virtio_alloc_aligned(
        desc_bytes, 16U, &queue->desc_raw);
    queue->avail = (volatile virtq_avail_t *)virtio_alloc_aligned(
        avail_bytes, 2U, &queue->avail_raw);
    queue->used = (volatile virtq_used_t *)virtio_alloc_aligned(
        used_bytes, 4U, &queue->used_raw);
    if (!queue->desc || !queue->avail || !queue->used) {
        virtio_queue_release(queue);
        return false;
    }

    queue->index = index;
    queue->size = size;
    queue->avail_shadow = 0U;
    queue->used_shadow = 0U;
    notify_off = mmio_read16(g_virtio.common, COMMON_QNOTIFY_OFF);
    queue->notify_off = notify_off;
    notify_byte_offset = (uint32_t)notify_off * g_virtio.notify_multiplier;
    if (notify_byte_offset + sizeof(uint16_t) > g_virtio.notify_length) {
        virtio_queue_release(queue);
        return false;
    }
    queue->notify = (volatile uint16_t *)(g_virtio.notify_base +
                                           notify_byte_offset);

    mmio_write16(g_virtio.common, COMMON_QSIZE, size);
    mmio_write16(g_virtio.common, COMMON_QMSIX, VIRTQ_NO_VECTOR);
    mmio_write64_32(g_virtio.common, COMMON_QDESC,
                    (uint32_t)(uintptr_t)queue->desc);
    mmio_write64_32(g_virtio.common, COMMON_QDRIVER,
                    (uint32_t)(uintptr_t)queue->avail);
    mmio_write64_32(g_virtio.common, COMMON_QDEVICE,
                    (uint32_t)(uintptr_t)queue->used);
    virtio_barrier();
    mmio_write16(g_virtio.common, COMMON_QENABLE, 1U);
    queue->ready = true;
    return true;
}

static bool virtio_queue_wait_used(virtio_queue_t *queue, uint32_t expected_id) {
    uint32_t spins = 0U;
    uint32_t start_tick;
    uint32_t timeout_ticks;
    virtq_used_elem_t elem;

    if (!queue || !queue->ready) {
        if (queue) queue->last_error = VIRTQ_ERROR_NOT_READY;
        return false;
    }
    start_tick = pit_get_ticks();
    timeout_ticks = pit_get_frequency_hz() * VIRTIO_GPU_QUEUE_TIMEOUT_SECONDS;
    while (queue->used->idx == queue->used_shadow) {
        spins++;
        if ((spins % VIRTIO_GPU_QUEUE_IOWAIT_INTERVAL) == 0U) {
            /*
             * virtio-gpu procesa las notificaciones mediante bottom halves y,
             * con VirGL, los fences se completan desde callbacks/timers del
             * host. El acceso a 0x80 fuerza una salida de TCG/KVM periódica y
             * evita que un guest en polling monopolice el hilo de QEMU.
             */
            io_wait();
            if (timeout_ticks &&
                pit_get_ticks() - start_tick >= timeout_ticks) {
                queue->last_error = VIRTQ_ERROR_TIMEOUT;
                return false;
            }
        }
        if (spins >= VIRTIO_GPU_QUEUE_SPIN_LIMIT) {
            queue->last_error = VIRTQ_ERROR_TIMEOUT;
            return false;
        }
        __asm__ volatile ("pause");
    }
    virtio_barrier();
    elem = queue->used->ring[queue->used_shadow % queue->size];
    queue->used_shadow++;
    queue->last_used_id = elem.id;
    if (elem.id != expected_id) {
        queue->last_error = VIRTQ_ERROR_BAD_USED_ID;
        return false;
    }
    queue->last_error = VIRTQ_ERROR_NONE;
    return true;
}

static bool virtio_queue_submit_control(virtio_queue_t *queue,
                                        const void *request,
                                        uint32_t request_size,
                                        void *response,
                                        uint32_t response_size) {
    uint16_t slot;
    if (!queue || !queue->ready || !request || !request_size ||
        !response || response_size < sizeof(virtio_gpu_ctrl_hdr_t)) {
        if (queue) queue->last_error = VIRTQ_ERROR_NOT_READY;
        return false;
    }
    {
        uint32_t start_tick = pit_get_ticks();
        uint32_t timeout = pit_get_frequency_hz() *
                           VIRTIO_GPU_QUEUE_TIMEOUT_SECONDS;
        for (;;) {
            task_preempt_disable();
            if (!queue->busy) {
                queue->busy = true;
                task_preempt_enable();
                break;
            }
            task_preempt_enable();
            if (timeout && pit_get_ticks() - start_tick >= timeout) {
                queue->last_error = VIRTQ_ERROR_BUSY;
                return false;
            }
            task_yield();
        }
    }
    queue->desc[0].addr = (uint64_t)(uint32_t)(uintptr_t)request;
    queue->desc[0].len = request_size;
    queue->desc[0].flags = VIRTQ_DESC_F_NEXT;
    queue->desc[0].next = 1U;
    queue->desc[1].addr = (uint64_t)(uint32_t)(uintptr_t)response;
    queue->desc[1].len = response_size;
    queue->desc[1].flags = VIRTQ_DESC_F_WRITE;
    queue->desc[1].next = 0U;

    slot = (uint16_t)(queue->avail_shadow % queue->size);
    queue->avail->ring[slot] = 0U;
    virtio_barrier();
    queue->avail_shadow++;
    queue->avail->idx = queue->avail_shadow;
    virtio_barrier();
    *queue->notify = queue->index;
    {
        bool completed = virtio_queue_wait_used(queue, 0U);
        task_preempt_disable();
        queue->busy = false;
        task_preempt_enable();
        if (!completed) queue->ready = false;
        return completed;
    }
}

/* Submit the transfer and flush as two independent chains with one notify.
 * The old path kicked QEMU twice per desktop repaint, which dominates small
 * dirty rectangles and makes VirtIO feel slower than a plain framebuffer. */
static bool virtio_queue_submit_control_pair(
    virtio_queue_t *queue,
    const void *request_a,uint32_t request_a_size,void *response_a,
    uint32_t response_a_size,const void *request_b,uint32_t request_b_size,
    void *response_b,uint32_t response_b_size) {
    uint16_t slot;
    bool first,second;
    if(!queue||!queue->ready||queue->size<4U||
       !request_a||!request_b||!response_a||!response_b)return false;
    {
        uint32_t start_tick=pit_get_ticks();
        uint32_t timeout=pit_get_frequency_hz()*
                         VIRTIO_GPU_QUEUE_TIMEOUT_SECONDS;
        for(;;){
            task_preempt_disable();
            if(!queue->busy){queue->busy=true;task_preempt_enable();break;}
            task_preempt_enable();
            if(timeout&&pit_get_ticks()-start_tick>=timeout){
                queue->last_error=VIRTQ_ERROR_BUSY;
                return false;
            }
            task_yield();
        }
    }
    queue->desc[0]=(virtq_desc_t){
        (uint64_t)(uint32_t)(uintptr_t)request_a,request_a_size,
        VIRTQ_DESC_F_NEXT,1U};
    queue->desc[1]=(virtq_desc_t){
        (uint64_t)(uint32_t)(uintptr_t)response_a,response_a_size,
        VIRTQ_DESC_F_WRITE,0U};
    queue->desc[2]=(virtq_desc_t){
        (uint64_t)(uint32_t)(uintptr_t)request_b,request_b_size,
        VIRTQ_DESC_F_NEXT,3U};
    queue->desc[3]=(virtq_desc_t){
        (uint64_t)(uint32_t)(uintptr_t)response_b,response_b_size,
        VIRTQ_DESC_F_WRITE,0U};
    slot=(uint16_t)(queue->avail_shadow%queue->size);
    queue->avail->ring[slot]=0U;
    queue->avail_shadow++;
    slot=(uint16_t)(queue->avail_shadow%queue->size);
    queue->avail->ring[slot]=2U;
    virtio_barrier();
    queue->avail_shadow++;
    queue->avail->idx=queue->avail_shadow;
    virtio_barrier();
    *queue->notify=queue->index;
    first=virtio_queue_wait_used(queue,0U);
    second=first&&virtio_queue_wait_used(queue,2U);
    task_preempt_disable();
    queue->busy=false;
    task_preempt_enable();
    if(!second)queue->ready=false;
    return second;
}

static bool virtio_queue_submit_cursor(virtio_queue_t *queue,
                                       const void *request,
                                       uint32_t request_size) {
    uint16_t slot;
    if (!queue || !queue->ready || !request || !request_size) {
        if (queue) queue->last_error = VIRTQ_ERROR_NOT_READY;
        return false;
    }
    {
        uint32_t start_tick = pit_get_ticks();
        uint32_t timeout = pit_get_frequency_hz() *
                           VIRTIO_GPU_QUEUE_TIMEOUT_SECONDS;
        for (;;) {
            task_preempt_disable();
            if (!queue->busy) {
                queue->busy = true;
                task_preempt_enable();
                break;
            }
            task_preempt_enable();
            if (timeout && pit_get_ticks() - start_tick >= timeout) {
                queue->last_error = VIRTQ_ERROR_BUSY;
                return false;
            }
            task_yield();
        }
    }
    queue->desc[0].addr = (uint64_t)(uint32_t)(uintptr_t)request;
    queue->desc[0].len = request_size;
    queue->desc[0].flags = 0U;
    queue->desc[0].next = 0U;
    slot = (uint16_t)(queue->avail_shadow % queue->size);
    queue->avail->ring[slot] = 0U;
    virtio_barrier();
    queue->avail_shadow++;
    queue->avail->idx = queue->avail_shadow;
    virtio_barrier();
    *queue->notify = queue->index;
    {
        bool completed = virtio_queue_wait_used(queue, 0U);
        task_preempt_disable();
        queue->busy = false;
        task_preempt_enable();
        if (!completed) queue->ready = false;
        return completed;
    }
}

static uint32_t virtio_gpu_next_resource(void) {
    uint32_t resource;
    task_preempt_disable();
    g_virtio.next_resource++;
    if (!g_virtio.next_resource) g_virtio.next_resource++;
    resource = g_virtio.next_resource;
    task_preempt_enable();
    return resource;
}

static uint32_t virtio_gpu_next_fence(void) {
    uint32_t fence;
    task_preempt_disable();
    g_virtio.next_fence++;
    if (!g_virtio.next_fence) g_virtio.next_fence++;
    fence = g_virtio.next_fence;
    task_preempt_enable();
    return fence;
}

static bool virtio_gpu_control_command(void *request, uint32_t request_size,
                                       void *response, uint32_t response_size,
                                       uint32_t expected_response,
                                       bool fenced, uint32_t *fence_out) {
    virtio_gpu_ctrl_hdr_t *request_hdr = (virtio_gpu_ctrl_hdr_t *)request;
    virtio_gpu_ctrl_hdr_t *response_hdr = (virtio_gpu_ctrl_hdr_t *)response;
    uint32_t fence = 0U;
    if (fence_out) *fence_out = 0U;
    if (!g_virtio.device_ready || !request_hdr || !response_hdr) return false;
    if (fenced) {
        fence = virtio_gpu_next_fence();
        request_hdr->flags |= VIRTIO_GPU_FLAG_FENCE;
        request_hdr->fence_id = (uint64_t)fence;
    }
    kmemset(response, 0, response_size);
    if (!virtio_queue_submit_control(&g_virtio.controlq, request,
                                     request_size, response, response_size)) {
        if (g_virtio.controlq.last_error == VIRTQ_ERROR_TIMEOUT) {
            kprintf("[VIRTIOGPU.DVR] timeout controlq cmd=%x avail=%u used=%u\n",
                    request_hdr->type, g_virtio.controlq.avail_shadow,
                    g_virtio.controlq.used_shadow);
        } else if (g_virtio.controlq.last_error == VIRTQ_ERROR_BAD_USED_ID) {
            kprintf("[VIRTIOGPU.DVR] controlq used-id=%u inesperado, cmd=%x\n",
                    g_virtio.controlq.last_used_id, request_hdr->type);
        } else if (g_virtio.controlq.last_error == VIRTQ_ERROR_BUSY) {
            kprintf("[VIRTIOGPU.DVR] controlq ocupada, cmd=%x descartado\n",
                    request_hdr->type);
        }
        return false;
    }
    if (response_hdr->type != expected_response) {
        kprintf("[VIRTIOGPU.DVR] respuesta 0x%x para cmd 0x%x (esperada 0x%x)\n",
                response_hdr->type, request_hdr->type, expected_response);
        return false;
    }
    if (fenced) {
        g_virtio.completed_fence = fence;
        if (fence_out) *fence_out = fence;
    }
    return true;
}

static bool virtio_gpu_resource_create(uint32_t resource_id, uint32_t format,
                                       uint32_t width, uint32_t height) {
    virtio_gpu_resource_create_2d_t request;
    virtio_gpu_ctrl_hdr_t response;
    kmemset(&request, 0, sizeof(request));
    request.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    request.resource_id = resource_id;
    request.format = format;
    request.width = width;
    request.height = height;
    return virtio_gpu_control_command(&request, sizeof(request), &response,
        sizeof(response), VIRTIO_GPU_RESP_OK_NODATA, false, NULL);
}

static bool virtio_gpu_resource_attach(uint32_t resource_id, void *memory,
                                       uint32_t length) {
    virtio_gpu_resource_attach_backing_one_t request;
    virtio_gpu_ctrl_hdr_t response;
    kmemset(&request, 0, sizeof(request));
    request.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    request.resource_id = resource_id;
    request.nr_entries = 1U;
    request.entry.addr = (uint64_t)(uint32_t)(uintptr_t)memory;
    request.entry.length = length;
    return virtio_gpu_control_command(&request, sizeof(request), &response,
        sizeof(response), VIRTIO_GPU_RESP_OK_NODATA, false, NULL);
}

static bool virtio_gpu_resource_detach(uint32_t resource_id) {
    virtio_gpu_resource_unref_t request;
    virtio_gpu_ctrl_hdr_t response;
    kmemset(&request, 0, sizeof(request));
    request.hdr.type = VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING;
    request.resource_id = resource_id;
    return virtio_gpu_control_command(&request, sizeof(request), &response,
        sizeof(response), VIRTIO_GPU_RESP_OK_NODATA, false, NULL);
}

static bool virtio_gpu_resource_unref(uint32_t resource_id) {
    virtio_gpu_resource_unref_t request;
    virtio_gpu_ctrl_hdr_t response;
    kmemset(&request, 0, sizeof(request));
    request.hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    request.resource_id = resource_id;
    return virtio_gpu_control_command(&request, sizeof(request), &response,
        sizeof(response), VIRTIO_GPU_RESP_OK_NODATA, false, NULL);
}

static void virtio_gpu_resource_destroy(uint32_t resource_id) {
    if (!resource_id || !g_virtio.device_ready) return;
    (void)virtio_gpu_resource_detach(resource_id);
    (void)virtio_gpu_resource_unref(resource_id);
}

static bool virtio_gpu_set_scanout_resource(uint32_t resource_id,
                                            uint32_t width,
                                            uint32_t height) {
    virtio_gpu_set_scanout_t request;
    virtio_gpu_ctrl_hdr_t response;
    kmemset(&request, 0, sizeof(request));
    request.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    request.r.width = width;
    request.r.height = height;
    request.scanout_id = 0U;
    request.resource_id = resource_id;
    return virtio_gpu_control_command(&request, sizeof(request), &response,
        sizeof(response), VIRTIO_GPU_RESP_OK_NODATA, false, NULL);
}

static bool virtio_gpu_transfer_rect(uint32_t resource_id, uint32_t pitch,
                                     gfx_rect_t rect, bool fenced,
                                     uint32_t *fence_out) {
    virtio_gpu_transfer_to_host_2d_t request;
    virtio_gpu_ctrl_hdr_t response;
    kmemset(&request, 0, sizeof(request));
    request.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    request.r.x = (uint32_t)rect.x;
    request.r.y = (uint32_t)rect.y;
    request.r.width = (uint32_t)rect.w;
    request.r.height = (uint32_t)rect.h;
    request.offset = (uint64_t)((uint32_t)rect.y * pitch +
                                (uint32_t)rect.x * 4U);
    request.resource_id = resource_id;
    return virtio_gpu_control_command(&request, sizeof(request), &response,
        sizeof(response), VIRTIO_GPU_RESP_OK_NODATA, fenced, fence_out);
}

static bool virtio_gpu_upload_scanout_rect(uint32_t resource_id,
                                           uint32_t pitch,
                                           gfx_rect_t rect,
                                           uint32_t *fence_out) {
    virtio_gpu_transfer_to_host_2d_t transfer;
    virtio_gpu_resource_flush_t flush;
    virtio_gpu_ctrl_hdr_t transfer_response,flush_response;
    uint32_t fence=0U;
    if(fence_out)*fence_out=0U;
    if(g_virtio.scanout_resource!=resource_id){
        if(!virtio_gpu_set_scanout_resource(resource_id,g_virtio.width,
                                             g_virtio.height))return false;
        g_virtio.scanout_resource=resource_id;
    }
    kmemset(&transfer,0,sizeof(transfer));
    kmemset(&flush,0,sizeof(flush));
    kmemset(&transfer_response,0,sizeof(transfer_response));
    kmemset(&flush_response,0,sizeof(flush_response));
    transfer.hdr.type=VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    transfer.r=(virtio_gpu_rect_t){(uint32_t)rect.x,(uint32_t)rect.y,
        (uint32_t)rect.w,(uint32_t)rect.h};
    transfer.offset=(uint64_t)((uint32_t)rect.y*pitch+
                               (uint32_t)rect.x*4U);
    transfer.resource_id=resource_id;
    flush.hdr.type=VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    flush.r=transfer.r;flush.resource_id=resource_id;
    if(fence_out) {
        fence=virtio_gpu_next_fence();
        flush.hdr.flags=VIRTIO_GPU_FLAG_FENCE;
        flush.hdr.fence_id=fence;
    }
    if(!virtio_queue_submit_control_pair(&g_virtio.controlq,
        &transfer,sizeof(transfer),&transfer_response,sizeof(transfer_response),
        &flush,sizeof(flush),&flush_response,sizeof(flush_response))||
       transfer_response.type!=VIRTIO_GPU_RESP_OK_NODATA||
       flush_response.type!=VIRTIO_GPU_RESP_OK_NODATA)return false;
    if(fence){g_virtio.completed_fence=fence;*fence_out=fence;}
    return true;
}

static bool virtio_gpu_flush_resource(uint32_t resource_id,gfx_rect_t rect,
                                      uint32_t *fence_out){
    virtio_gpu_resource_flush_t flush;
    virtio_gpu_ctrl_hdr_t response;
    uint32_t fence=0U;
    if(fence_out)*fence_out=0U;
    kmemset(&flush,0,sizeof(flush));
    flush.hdr.type=VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    flush.r=(virtio_gpu_rect_t){(uint32_t)rect.x,(uint32_t)rect.y,
        (uint32_t)rect.w,(uint32_t)rect.h};
    flush.resource_id=resource_id;
    if(fence_out){fence=virtio_gpu_next_fence();
        flush.hdr.flags=VIRTIO_GPU_FLAG_FENCE;flush.hdr.fence_id=fence;}
    if(!virtio_gpu_control_command(&flush,sizeof(flush),&response,
        sizeof(response),VIRTIO_GPU_RESP_OK_NODATA,false,NULL))return false;
    if(fence){g_virtio.completed_fence=fence;*fence_out=fence;}
    return true;
}

static bool virtio_gpu_upload_cursor_resource(uint32_t resource_id,
                                              uint32_t pitch,
                                              gfx_rect_t rect) {
    uint32_t fence = 0U;
    /* VirtIO exige completar la transferencia del cursor con fence antes de
       mandar UPDATE_CURSOR por cursorq. No hace falta RESOURCE_FLUSH porque
       el recurso del cursor no es un scanout. */
    return virtio_gpu_transfer_rect(resource_id, pitch, rect, true, &fence) &&
           fence != 0U;
}

static bool virtio_gpu_get_display_info(uint32_t *width, uint32_t *height) {
    virtio_gpu_ctrl_hdr_t request;
    virtio_gpu_resp_display_info_t response;
    kmemset(&request, 0, sizeof(request));
    request.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    if (!virtio_gpu_control_command(&request, sizeof(request), &response,
        sizeof(response), VIRTIO_GPU_RESP_OK_DISPLAY_INFO, false, NULL))
        return false;
    for (uint32_t i = 0; i < 16U; i++) {
        if (response.pmodes[i].enabled && response.pmodes[i].r.width &&
            response.pmodes[i].r.height) {
            if (width) *width = response.pmodes[i].r.width;
            if (height) *height = response.pmodes[i].r.height;
            return true;
        }
    }
    return false;
}

static bool virtio_gpu_download_capset(void) {
    virtio_gpu_get_capset_t request;
    uint8_t *response;
    uint32_t response_size, checksum=2166136261U;
    bool ok;
    if (!g_virtio.virgl_capset_id || !g_virtio.virgl_capset_size ||
        g_virtio.virgl_capset_size > 65536U) return false;
    response_size=(uint32_t)sizeof(virtio_gpu_ctrl_hdr_t)+
                  g_virtio.virgl_capset_size;
    response=(uint8_t *)kmalloc(response_size);
    if (!response) return false;
    kmemset(&request,0,sizeof(request));
    request.hdr.type=VIRTIO_GPU_CMD_GET_CAPSET;
    request.capset_id=g_virtio.virgl_capset_id;
    request.capset_version=g_virtio.virgl_capset_version;
    ok=virtio_gpu_control_command(&request,sizeof(request),response,
        response_size,VIRTIO_GPU_RESP_OK_CAPSET,false,NULL);
    if(ok) {
        for(uint32_t i=(uint32_t)sizeof(virtio_gpu_ctrl_hdr_t);
            i<response_size;i++) checksum=(checksum^response[i])*16777619U;
        g_virtio.virgl_capset_checksum=checksum;
    }
    kfree(response);
    return ok;
}

static bool virtio_gpu_create_virgl_context(void) {
    virtio_gpu_ctx_create_t request;
    virtio_gpu_ctrl_hdr_t response;
    static const char name[]="BlesKernOS TinyGL";
    if(!g_virtio.virgl_available||!g_virtio.virgl_capset_id)return false;
    kmemset(&request,0,sizeof(request));
    request.hdr.type=VIRTIO_GPU_CMD_CTX_CREATE;
    request.hdr.ctx_id=1U;
    request.nlen=(uint32_t)sizeof(name)-1U;
    if(g_virtio.context_init_available)
        request.context_init=g_virtio.virgl_capset_id&0xFFU;
    kmemcpy(request.debug_name,name,sizeof(name)-1U);
    if(!virtio_gpu_control_command(&request,sizeof(request),&response,
        sizeof(response),VIRTIO_GPU_RESP_OK_NODATA,false,NULL))return false;
    g_virtio.virgl_context_id=1U;
    g_virtio.virgl_context_ready=true;
    return true;
}

static bool virtio_gpu_resource_create_3d_ex(uint32_t resource_id,
        uint32_t target,uint32_t format,uint32_t bind,
        uint32_t width,uint32_t height) {
    virtio_gpu_resource_create_3d_t request;
    virtio_gpu_ctrl_hdr_t response;
    kmemset(&request,0,sizeof(request));
    request.hdr.type=VIRTIO_GPU_CMD_RESOURCE_CREATE_3D;
    request.resource_id=resource_id;
    request.target=target;request.format=format;request.bind=bind;
    request.width=width;request.height=height;
    request.depth=1U;request.array_size=1U;
    return virtio_gpu_control_command(&request,sizeof(request),&response,
        sizeof(response),VIRTIO_GPU_RESP_OK_NODATA,false,NULL);
}

static bool virtio_gpu_context_resource(uint32_t command,
                                        uint32_t resource_id) {
    virtio_gpu_ctx_resource_t request;
    virtio_gpu_ctrl_hdr_t response;
    kmemset(&request,0,sizeof(request));
    request.hdr.type=command;
    request.hdr.ctx_id=g_virtio.virgl_context_id;
    request.resource_id=resource_id;
    return virtio_gpu_control_command(&request,sizeof(request),&response,
        sizeof(response),VIRTIO_GPU_RESP_OK_NODATA,false,NULL);
}

static bool virtio_gpu_transfer_host_3d(uint32_t command,uint32_t resource_id,
        uint32_t width,uint32_t height,uint32_t stride) {
    virtio_gpu_transfer_host_3d_t request;
    virtio_gpu_ctrl_hdr_t response;
    kmemset(&request,0,sizeof(request));
    request.hdr.type=command;
    request.hdr.ctx_id=g_virtio.virgl_context_id;
    request.box.w=width;request.box.h=height;request.box.d=1U;
    request.resource_id=resource_id;
    request.stride=stride;
    request.layer_stride=stride*height;
    return virtio_gpu_control_command(&request,sizeof(request),&response,
        sizeof(response),VIRTIO_GPU_RESP_OK_NODATA,true,NULL);
}

static bool virtio_gpu_submit_3d(const uint32_t *commands,
                                 uint32_t dword_count) {
    virtio_gpu_cmd_submit_t *request;
    virtio_gpu_ctrl_hdr_t response;
    uint32_t bytes,total;
    bool ok;
    if(!g_virtio.virgl_context_ready||!commands||!dword_count||
       dword_count>16384U)return false;
    bytes=dword_count*4U;
    total=(uint32_t)sizeof(*request)+bytes;
    request=(virtio_gpu_cmd_submit_t *)kmalloc(total);
    if(!request)return false;
    kmemset(request,0,sizeof(*request));
    request->hdr.type=VIRTIO_GPU_CMD_SUBMIT_3D;
    request->hdr.ctx_id=g_virtio.virgl_context_id;
    request->size=bytes;
    kmemcpy((uint8_t *)request+sizeof(*request),commands,bytes);
    ok=virtio_gpu_control_command(request,total,&response,sizeof(response),
        VIRTIO_GPU_RESP_OK_NODATA,true,NULL);
    kfree(request);
    return ok;
}

/*
 * Exercise the complete host-3D path without exposing an unusable API:
 * create a render target, bind it to the context, submit a VirGL clear,
 * download the result, and require that the host changed guest backing.
 */
static bool virtio_gpu_virgl_selftest(void) {
    uint32_t commands[24],n=0U,resource,surface=1U;
    uint32_t *pixels;
    void *raw=NULL;
    bool created=false,backed=false,attached=false,ok=false;
    pixels=(uint32_t *)virtio_alloc_aligned(4096U,4096U,&raw);
    if(!pixels)return false;
    for(uint32_t i=0;i<256U;i++)pixels[i]=0x13579BDFU;
    resource=virtio_gpu_next_resource();
    if(!virtio_gpu_resource_create_3d_ex(resource,VIRGL_TARGET_TEXTURE_2D,
        VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM,VIRGL_BIND_RENDER_TARGET,
        16U,16U))goto done;
    created=true;
    if(!virtio_gpu_resource_attach(resource,pixels,4096U))goto done;
    backed=true;
    if(!virtio_gpu_context_resource(VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
                                    resource))goto done;
    attached=true;

    commands[n++]=VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT,VIRGL_OBJECT_SURFACE,5U);
    commands[n++]=surface;commands[n++]=resource;
    commands[n++]=VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    commands[n++]=0U;commands[n++]=0U;
    commands[n++]=VIRGL_CMD0(VIRGL_CCMD_SET_FRAMEBUFFER_STATE,0U,3U);
    commands[n++]=1U;commands[n++]=0U;commands[n++]=surface;
    commands[n++]=VIRGL_CMD0(VIRGL_CCMD_CLEAR,0U,8U);
    commands[n++]=VIRGL_CLEAR_COLOR0;
    commands[n++]=0x3E800000U; /* red   = 0.25 */
    commands[n++]=0x3F000000U; /* green = 0.50 */
    commands[n++]=0x3F400000U; /* blue  = 0.75 */
    commands[n++]=0x3F800000U; /* alpha = 1.00 */
    commands[n++]=0U;commands[n++]=0x3FF00000U; /* depth = 1.0 */
    commands[n++]=0U;
    commands[n++]=VIRGL_CMD0(VIRGL_CCMD_SET_FRAMEBUFFER_STATE,0U,2U);
    commands[n++]=0U;commands[n++]=0U;
    commands[n++]=VIRGL_CMD0(VIRGL_CCMD_DESTROY_OBJECT,VIRGL_OBJECT_SURFACE,1U);
    commands[n++]=surface;
    if(!virtio_gpu_submit_3d(commands,n) ||
       !virtio_gpu_transfer_host_3d(VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,
                                    resource,16U,16U,64U))goto done;
    ok=pixels[0]!=0x13579BDFU&&pixels[255]!=0x13579BDFU;
done:
    if(attached)(void)virtio_gpu_context_resource(
        VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE,resource);
    if(backed)(void)virtio_gpu_resource_detach(resource);
    if(created)(void)virtio_gpu_resource_unref(resource);
    kfree(raw);
    return ok;
}

static void virtio_gpu_destroy_virgl_context(void) {
    virtio_gpu_ctrl_hdr_t request,response;
    if(!g_virtio.device_ready||!g_virtio.virgl_context_ready)return;
    kmemset(&request,0,sizeof(request));
    request.type=VIRTIO_GPU_CMD_CTX_DESTROY;
    request.ctx_id=g_virtio.virgl_context_id;
    (void)virtio_gpu_control_command(&request,sizeof(request),&response,
        sizeof(response),VIRTIO_GPU_RESP_OK_NODATA,false,NULL);
    g_virtio.virgl_context_ready=false;
    g_virtio.virgl_commands_ready=false;
    g_virtio.virgl_context_id=0U;
}

static void virtio_gpu_probe_capsets(void) {
    uint32_t count;
    if (!g_virtio.virgl_available || !g_virtio.num_capsets) return;
    count = g_virtio.num_capsets;
    if (count > VIRTIO_GPU_MAX_CAPSETS) count = VIRTIO_GPU_MAX_CAPSETS;
    for (uint32_t i = 0; i < count; i++) {
        virtio_gpu_get_capset_info_t request;
        virtio_gpu_resp_capset_info_t response;
        kmemset(&request, 0, sizeof(request));
        request.hdr.type = VIRTIO_GPU_CMD_GET_CAPSET_INFO;
        request.capset_index = i;
        if (virtio_gpu_control_command(&request, sizeof(request), &response,
            sizeof(response), VIRTIO_GPU_RESP_OK_CAPSET_INFO, false, NULL)) {
            kprintf("[VIRTIOGPU.DVR] capset[%u]: id=%u version=%u size=%u\n",
                    i, response.capset_id, response.capset_max_version,
                    response.capset_max_size);
            /* VIRGL2 (id 2) is preferred, VIRGL (id 1) remains compatible. */
            if (((response.capset_id==2U && g_virtio.context_init_available) ||
                (!g_virtio.virgl_capset_id && response.capset_id==1U)) &&
                response.capset_max_size) {
                g_virtio.virgl_capset_id=response.capset_id;
                g_virtio.virgl_capset_version=response.capset_max_version;
                g_virtio.virgl_capset_size=response.capset_max_size;
            }
        }
    }
    if(g_virtio.virgl_capset_id && virtio_gpu_download_capset() &&
       virtio_gpu_create_virgl_context()) {
        g_virtio.virgl_commands_ready=virtio_gpu_virgl_selftest();
        kprintf("[VIRTIOGPU.DVR] VirGL transporte listo: capset=%u v%u bytes=%u hash=%x ctx=%u submit3d=%s\n",
                g_virtio.virgl_capset_id,g_virtio.virgl_capset_version,
                g_virtio.virgl_capset_size,g_virtio.virgl_capset_checksum,
                g_virtio.virgl_context_id,
                g_virtio.virgl_commands_ready?"OK":"fallo seguro");
    } else if(g_virtio.virgl_available)
        kprintf("[VIRTIOGPU.DVR] VirGL anunciado pero capset/contexto no quedaron operativos\n");
}

static void virtio_gpu_device_stop(void) {
    if (g_virtio.common && !virtio_gpu_reset_status())
        kprintf("[VIRTIOGPU.DVR] timeout al resetear el dispositivo\n");
    virtio_queue_release(&g_virtio.controlq);
    virtio_queue_release(&g_virtio.cursorq);
    g_virtio.device_ready = false;
}

static bool virtio_gpu_device_start(void) {
    uint8_t status;
    uint32_t features_low, features_high, accepted_low;
    uint16_t num_queues;

    if (g_virtio.device_ready) return true;
    if (!virtio_gpu_map_transport()) {
        kprintf("[VIRTIOGPU.DVR] faltan capacidades PCI modernas\n");
        return false;
    }
    if (!pci_enable_command(g_virtio.pci,
                            PCI_COMMAND_MEMORY | PCI_COMMAND_BUSMASTER)) {
        kprintf("[VIRTIOGPU.DVR] no se pudo habilitar MMIO/bus-master\n");
        return false;
    }

    if (!virtio_gpu_reset_status()) {
        kprintf("[VIRTIOGPU.DVR] el dispositivo no completo el reset\n");
        return false;
    }
    status = VIRTIO_STATUS_ACKNOWLEDGE;
    mmio_write8(g_virtio.common, COMMON_STATUS, status);
    status |= VIRTIO_STATUS_DRIVER;
    mmio_write8(g_virtio.common, COMMON_STATUS, status);

    mmio_write32(g_virtio.common, COMMON_DFSELECT, 0U);
    features_low = mmio_read32(g_virtio.common, COMMON_DF);
    mmio_write32(g_virtio.common, COMMON_DFSELECT, 1U);
    features_high = mmio_read32(g_virtio.common, COMMON_DF);
    if (!(features_high & VIRTIO_F_VERSION_1_HIGH)) {
        kprintf("[VIRTIOGPU.DVR] dispositivo sin VIRTIO_F_VERSION_1\n");
        mmio_write8(g_virtio.common, COMMON_STATUS,
                    (uint8_t)(status | VIRTIO_STATUS_FAILED));
        return false;
    }

    accepted_low = features_low & (VIRTIO_GPU_F_VIRGL | VIRTIO_GPU_F_EDID |
                                   VIRTIO_GPU_F_CONTEXT_INIT);
    mmio_write32(g_virtio.common, COMMON_GFSELECT, 0U);
    mmio_write32(g_virtio.common, COMMON_GF, accepted_low);
    mmio_write32(g_virtio.common, COMMON_GFSELECT, 1U);
    mmio_write32(g_virtio.common, COMMON_GF, VIRTIO_F_VERSION_1_HIGH);
    status |= VIRTIO_STATUS_FEATURES_OK;
    mmio_write8(g_virtio.common, COMMON_STATUS, status);
    if (!(mmio_read8(g_virtio.common, COMMON_STATUS) &
          VIRTIO_STATUS_FEATURES_OK)) {
        kprintf("[VIRTIOGPU.DVR] el host rechazo las features\n");
        mmio_write8(g_virtio.common, COMMON_STATUS,
                    (uint8_t)(status | VIRTIO_STATUS_FAILED));
        return false;
    }

    num_queues = mmio_read16(g_virtio.common, COMMON_NUMQ);
    if (num_queues < 2U ||
        !virtio_queue_setup(&g_virtio.controlq, VIRTIO_GPU_CONTROLQ) ||
        !virtio_queue_setup(&g_virtio.cursorq, VIRTIO_GPU_CURSORQ)) {
        kprintf("[VIRTIOGPU.DVR] no se pudieron crear controlq/cursorq\n");
        mmio_write8(g_virtio.common, COMMON_STATUS,
                    (uint8_t)(status | VIRTIO_STATUS_FAILED));
        virtio_gpu_device_stop();
        return false;
    }

    status |= VIRTIO_STATUS_DRIVER_OK;
    mmio_write8(g_virtio.common, COMMON_STATUS, status);
    g_virtio.device_features_low = features_low;
    g_virtio.virgl_available = (accepted_low & VIRTIO_GPU_F_VIRGL) != 0U;
    g_virtio.context_init_available =
        (accepted_low & VIRTIO_GPU_F_CONTEXT_INIT) != 0U;
    g_virtio.edid_available = (accepted_low & VIRTIO_GPU_F_EDID) != 0U;
    g_virtio.num_scanouts = mmio_read32(g_virtio.device_cfg,
                                        GPU_CONFIG_NUM_SCANOUTS);
    g_virtio.num_capsets = mmio_read32(g_virtio.device_cfg,
                                       GPU_CONFIG_NUM_CAPSETS);
    g_virtio.device_ready = true;
    kprintf("[VIRTIOGPU.DVR] modern PCI listo: queues=%u scanouts=%u virgl=%s edid=%s\n",
            num_queues, g_virtio.num_scanouts,
            g_virtio.virgl_available ? "si" : "no",
            g_virtio.edid_available ? "si" : "no");
    virtio_gpu_probe_capsets();
    return true;
}

static bool virtio_gpu_mode_supported(uint16_t width, uint16_t height,
                                      uint8_t bpp) {
    if (bpp != 32U) return false;
    for (uint32_t i = 0; i < sizeof(g_virtio_modes) / sizeof(g_virtio_modes[0]);
         i++) {
        if (g_virtio_modes[i].width == width &&
            g_virtio_modes[i].height == height) return true;
    }
    return false;
}

static bool virtio_gpu_clip_rect(int *x, int *y, int *w, int *h) {
    if (!x || !y || !w || !h || *w <= 0 || *h <= 0) return false;
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if ((uint32_t)*x >= g_virtio.width ||
        (uint32_t)*y >= g_virtio.height) return false;
    if ((uint32_t)(*x + *w) > g_virtio.width)
        *w = (int)g_virtio.width - *x;
    if ((uint32_t)(*y + *h) > g_virtio.height)
        *h = (int)g_virtio.height - *y;
    return *w > 0 && *h > 0;
}

static bool virtio_gpu_queue_dirty(int x, int y, int w, int h) {
    gfx_rect_t *rect;
    if (!g_virtio.active || !virtio_gpu_clip_rect(&x, &y, &w, &h))
        return false;
    if (g_virtio.dirty_count >= VIRTIO_GPU_MAX_DIRTY_RECTS) {
        g_virtio.dirty[0] = (gfx_rect_t){0, 0, (int32_t)g_virtio.width,
                                         (int32_t)g_virtio.height};
        g_virtio.dirty_count = 1U;
        return true;
    }
    rect = &g_virtio.dirty[g_virtio.dirty_count++];
    *rect = (gfx_rect_t){x, y, w, h};
    return true;
}

static bool virtio_gpu_dirty_union(gfx_rect_t *out) {
    int32_t x1, y1, x2, y2;
    if (!out || !g_virtio.dirty_count) return false;
    x1 = g_virtio.dirty[0].x;
    y1 = g_virtio.dirty[0].y;
    x2 = x1 + g_virtio.dirty[0].w;
    y2 = y1 + g_virtio.dirty[0].h;
    for (uint32_t i = 1; i < g_virtio.dirty_count; i++) {
        gfx_rect_t *r = &g_virtio.dirty[i];
        if (r->x < x1) x1 = r->x;
        if (r->y < y1) y1 = r->y;
        if (r->x + r->w > x2) x2 = r->x + r->w;
        if (r->y + r->h > y2) y2 = r->y + r->h;
    }
    *out = (gfx_rect_t){x1, y1, x2 - x1, y2 - y1};
    return true;
}

static bool virtio_gpu_submit_dirty(uint32_t *fence_out) {
    gfx_rect_t rect;
    bool ok;
    if (fence_out) *fence_out = 0U;
    if (!g_virtio.active) return false;
    if (!virtio_gpu_dirty_union(&rect)) return true;
    g_virtio.dirty_count = 0U;
    ok = virtio_gpu_upload_scanout_rect(g_virtio.display_resource,
                                          g_virtio.pitch, rect, fence_out);
    if (!ok) (void)virtio_gpu_queue_dirty(rect.x, rect.y, rect.w, rect.h);
    return ok;
}

static bool virtio_gpu_configure_mode(gfx_info_t *info, uint16_t width,
                                      uint16_t height) {
    uint32_t pitch = (uint32_t)width * 4U;
    uint32_t size;
    uint32_t resource;
    void *raw = NULL;
    uint32_t *pixels;
    uint32_t old_resource = g_virtio.display_resource;
    void *old_raw = g_virtio.framebuffer_raw;
    uint32_t old_width = g_virtio.width;
    uint32_t old_height = g_virtio.height;

    if (!info || !width || !height || pitch > 0xFFFFU) return false;
    if ((uint32_t)height > 0xFFFFFFFFU / pitch) return false;
    size = pitch * (uint32_t)height;
    pixels = (uint32_t *)virtio_alloc_aligned(
        ALIGN_UP(size, VIRTIO_GPU_PAGE_SIZE), VIRTIO_GPU_PAGE_SIZE, &raw);
    if (!pixels) return false;
    resource = virtio_gpu_next_resource();

    if (!virtio_gpu_resource_create(resource,
            VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM, width, height) ||
        !virtio_gpu_resource_attach(resource, pixels,
                                    ALIGN_UP(size, VIRTIO_GPU_PAGE_SIZE)) ||
        !virtio_gpu_set_scanout_resource(resource, width, height)) {
        virtio_gpu_resource_destroy(resource);
        kfree(raw);
        return false;
    }
    g_virtio.scanout_resource = resource;

    if (!virtio_gpu_upload_scanout_rect(resource, pitch,
            (gfx_rect_t){0, 0, width, height}, NULL)) {
        if (old_resource)
            (void)virtio_gpu_set_scanout_resource(old_resource,
                                                   old_width, old_height);
        g_virtio.scanout_resource = old_resource;
        virtio_gpu_resource_destroy(resource);
        kfree(raw);
        return false;
    }

    g_virtio.display_resource = resource;
    g_virtio.framebuffer_raw = raw;
    g_virtio.framebuffer = pixels;
    g_virtio.framebuffer_size = size;
    g_virtio.width = width;
    g_virtio.height = height;
    g_virtio.pitch = pitch;
    g_virtio.dirty_count = 0U;
    g_virtio.active = true;

    info->mode = GFX_MODE_VIRTIO_GPU;
    info->framebuffer = (uint32_t)(uintptr_t)pixels;
    info->width = width;
    info->height = height;
    info->pitch = (uint16_t)pitch;
    info->bpp = 32U;

    if (old_resource) virtio_gpu_resource_destroy(old_resource);
    if (old_raw) kfree(old_raw);
    kprintf("[VIRTIOGPU.DVR] scanout 0: %ux%ux32 pitch=%u recurso=%u\n",
            width, height, pitch, resource);
    return true;
}

static bool virtio_gpu_activate(gfx_info_t *info, uint16_t preferred_width,
                                uint16_t preferred_height) {
    uint32_t host_width = 0U, host_height = 0U;
    uint16_t width = 800U, height = 600U;
    if (!info || !virtio_gpu_device_start()) return false;
    if (preferred_width && preferred_height &&
        virtio_gpu_mode_supported(preferred_width, preferred_height, 32U)) {
        width = preferred_width;
        height = preferred_height;
    } else if (virtio_gpu_get_display_info(&host_width, &host_height) &&
               host_width <= 0xFFFFU && host_height <= 0xFFFFU &&
               virtio_gpu_mode_supported((uint16_t)host_width,
                                         (uint16_t)host_height, 32U)) {
        width = (uint16_t)host_width;
        height = (uint16_t)host_height;
    }
    return virtio_gpu_configure_mode(info, width, height);
}

static uint32_t virtio_gpu_runtime_capabilities(void) {
    if (!g_virtio.active) return 0U;
    return GFX_CAP_PRESENT_BUFFER | GFX_CAP_DIRTY_RECTS |
           GFX_CAP_HW_CURSOR | GFX_CAP_BITBLT | GFX_CAP_FILL;
}

static void virtio_gpu_free_surfaces(void) {
    for (uint32_t i = 0; i < VIRTIO_GPU_MAX_SURFACES; i++) {
        if (g_virtio.surfaces[i].used && g_virtio.surfaces[i].pixels)
            kfree(g_virtio.surfaces[i].pixels);
        kmemset(&g_virtio.surfaces[i], 0, sizeof(g_virtio.surfaces[i]));
    }
}

static void virtio_gpu_disable(void) {
    if (g_virtio.device_ready) {
        vg_reset();
        if (g_virtio.cursor_defined) {
            virtio_gpu_update_cursor_t hide;
            kmemset(&hide, 0, sizeof(hide));
            hide.hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
            (void)virtio_queue_submit_cursor(&g_virtio.cursorq,
                                              &hide, sizeof(hide));
        }
        if (g_virtio.display_resource)
            (void)virtio_gpu_set_scanout_resource(0U,
                                                   g_virtio.width,
                                                   g_virtio.height);
        if (g_virtio.cursor_resource)
            virtio_gpu_resource_destroy(g_virtio.cursor_resource);
        if (g_virtio.display_resource)
            virtio_gpu_resource_destroy(g_virtio.display_resource);
        virtio_gpu_destroy_virgl_context();
    }
    if (g_virtio.cursor_raw) kfree(g_virtio.cursor_raw);
    if (g_virtio.framebuffer_raw) kfree(g_virtio.framebuffer_raw);
    virtio_gpu_free_surfaces();
    g_virtio.cursor_raw = NULL;
    g_virtio.cursor_pixels = NULL;
    g_virtio.cursor_resource = 0U;
    g_virtio.cursor_defined = false;
    g_virtio.cursor_visible = false;
    g_virtio.cursor_host_defined = false;
    g_virtio.framebuffer_raw = NULL;
    g_virtio.framebuffer = NULL;
    g_virtio.display_resource = 0U;
    g_virtio.scanout_resource = 0U;
    g_virtio.virgl_capset_id = 0U;
    g_virtio.virgl_capset_version = 0U;
    g_virtio.virgl_capset_size = 0U;
    g_virtio.virgl_capset_checksum = 0U;
    g_virtio.active = false;
    g_virtio.dirty_count = 0U;
    virtio_gpu_device_stop();
}

static bool virtio_gpu_list_modes(gfx_display_mode_t *modes,
                                  uint32_t max_modes, uint32_t *count) {
    uint32_t total = sizeof(g_virtio_modes) / sizeof(g_virtio_modes[0]);
    if (count) *count = total;
    if (!modes || !max_modes) return true;
    if (max_modes > total) max_modes = total;
    for (uint32_t i = 0; i < max_modes; i++) modes[i] = g_virtio_modes[i];
    return true;
}

static bool virtio_gpu_set_mode(gfx_info_t *info, uint16_t width,
                                uint16_t height, uint8_t bpp) {
    if (!g_virtio.device_ready ||
        !virtio_gpu_mode_supported(width, height, bpp)) return false;
    return virtio_gpu_configure_mode(info, width, height);
}

static bool virtio_gpu_present_buffer(const gfx_info_t *info,
                                      const uint32_t *pixels,
                                      uint32_t source_pitch,
                                      const gfx_rect_t *rects,
                                      uint32_t rect_count,
                                      uint32_t *fence_out) {
    if (fence_out) *fence_out = 0U;
    if (!g_virtio.active || !info || !pixels || !rects || !rect_count ||
        source_pitch < info->width || info->mode != GFX_MODE_VIRTIO_GPU)
        return false;
    for (uint32_t i = 0; i < rect_count; i++) {
        int x = rects[i].x, y = rects[i].y;
        int w = rects[i].w, h = rects[i].h;
        if (!virtio_gpu_clip_rect(&x, &y, &w, &h)) continue;
        for (int row = 0; row < h; row++) {
            uint32_t *dst = g_virtio.framebuffer +
                (uint32_t)(y + row) * g_virtio.width + (uint32_t)x;
            const uint32_t *src = pixels +
                (uint32_t)(y + row) * source_pitch + (uint32_t)x;
            kmemcpy(dst, src, (uint32_t)w * sizeof(uint32_t));
        }
        (void)virtio_gpu_queue_dirty(x, y, w, h);
    }
    return virtio_gpu_submit_dirty(fence_out);
}

static bool virtio_gpu_update_rect(int x, int y, int w, int h) {
    return virtio_gpu_queue_dirty(x, y, w, h);
}

static bool virtio_gpu_flush(uint32_t *fence_out) {
    return virtio_gpu_submit_dirty(fence_out);
}

static bool virtio_gpu_wait_fence(uint32_t fence) {
    if (!fence || !g_virtio.active) return true;
    return (int32_t)(g_virtio.completed_fence - fence) >= 0;
}

static bool virtio_gpu_fill_rect(const gfx_info_t *info, int x, int y,
                                 int w, int h, uint32_t rgb,
                                 uint32_t *fence_out) {
    if (fence_out) *fence_out = 0U;
    if (!g_virtio.active || !info || info->mode != GFX_MODE_VIRTIO_GPU ||
        !virtio_gpu_clip_rect(&x, &y, &w, &h)) return false;
    rgb &= 0x00FFFFFFU;
    for (int row = 0; row < h; row++) {
        uint32_t *dst = g_virtio.framebuffer +
            (uint32_t)(y + row) * g_virtio.width + (uint32_t)x;
        for (int col = 0; col < w; col++) dst[col] = rgb;
    }
    (void)virtio_gpu_queue_dirty(x, y, w, h);
    return virtio_gpu_submit_dirty(fence_out);
}

static uint32_t virtio_gpu_apply_rop(uint32_t source, uint32_t destination,
                                     gfx_rop_t rop) {
    source &= 0x00FFFFFFU;
    destination &= 0x00FFFFFFU;
    switch (rop) {
        case GFX_ROP_XOR: return source ^ destination;
        case GFX_ROP_AND: return source & destination;
        case GFX_ROP_OR: return source | destination;
        case GFX_ROP_INVERT: return (~destination) & 0x00FFFFFFU;
        case GFX_ROP_COPY:
        default: return source;
    }
}

static bool virtio_gpu_clip_blit(const gfx_info_t *info, int *src_x,
                                 int *src_y, int *dst_x, int *dst_y,
                                 int *w, int *h) {
    if (!info || !src_x || !src_y || !dst_x || !dst_y || !w || !h ||
        *w <= 0 || *h <= 0) return false;
    if (*src_x < 0) { int d = -*src_x; *src_x = 0; *dst_x += d; *w -= d; }
    if (*src_y < 0) { int d = -*src_y; *src_y = 0; *dst_y += d; *h -= d; }
    if (*dst_x < 0) { int d = -*dst_x; *dst_x = 0; *src_x += d; *w -= d; }
    if (*dst_y < 0) { int d = -*dst_y; *dst_y = 0; *src_y += d; *h -= d; }
    if (*src_x + *w > info->width) *w = info->width - *src_x;
    if (*dst_x + *w > info->width) *w = info->width - *dst_x;
    if (*src_y + *h > info->height) *h = info->height - *src_y;
    if (*dst_y + *h > info->height) *h = info->height - *dst_y;
    return *w > 0 && *h > 0;
}

static bool virtio_gpu_bitblt(const gfx_info_t *info, int src_x, int src_y,
                              int dst_x, int dst_y, int w, int h,
                              gfx_rop_t rop, uint32_t *fence_out) {
    int row_start, row_end, row_step;
    if (fence_out) *fence_out = 0U;
    if (!g_virtio.active || !virtio_gpu_clip_blit(info, &src_x, &src_y,
        &dst_x, &dst_y, &w, &h)) return false;
    row_start = dst_y > src_y ? h - 1 : 0;
    row_end = dst_y > src_y ? -1 : h;
    row_step = dst_y > src_y ? -1 : 1;
    for (int row = row_start; row != row_end; row += row_step) {
        uint32_t *src = g_virtio.framebuffer +
            (uint32_t)(src_y + row) * g_virtio.width + (uint32_t)src_x;
        uint32_t *dst = g_virtio.framebuffer +
            (uint32_t)(dst_y + row) * g_virtio.width + (uint32_t)dst_x;
        if (dst_x > src_x && dst_x < src_x + w) {
            for (int col = w - 1; col >= 0; col--)
                dst[col] = virtio_gpu_apply_rop(src[col], dst[col], rop);
        } else {
            for (int col = 0; col < w; col++)
                dst[col] = virtio_gpu_apply_rop(src[col], dst[col], rop);
        }
    }
    (void)virtio_gpu_queue_dirty(dst_x, dst_y, w, h);
    return virtio_gpu_submit_dirty(fence_out);
}

static bool virtio_gpu_send_cursor(bool move_only) {
    virtio_gpu_update_cursor_t request;
    bool update;
    bool ok;

    if (!g_virtio.device_ready || !g_virtio.cursorq.ready) return false;
    update = !move_only || !g_virtio.cursor_host_defined;
    kmemset(&request, 0, sizeof(request));
    request.hdr.type = update ? VIRTIO_GPU_CMD_UPDATE_CURSOR
                              : VIRTIO_GPU_CMD_MOVE_CURSOR;
    request.pos.scanout_id = 0U;
    request.pos.x = g_virtio.cursor_x < 0 ? 0U : (uint32_t)g_virtio.cursor_x;
    request.pos.y = g_virtio.cursor_y < 0 ? 0U : (uint32_t)g_virtio.cursor_y;
    request.resource_id = g_virtio.cursor_visible
                        ? g_virtio.cursor_resource : 0U;
    request.hot_x = g_virtio.cursor_hot_x;
    request.hot_y = g_virtio.cursor_hot_y;
    ok = virtio_queue_submit_cursor(&g_virtio.cursorq,
                                    &request, sizeof(request));
    if (ok && update)
        g_virtio.cursor_host_defined = g_virtio.cursor_visible &&
                                       g_virtio.cursor_resource != 0U;
    return ok;
}

static bool virtio_gpu_cursor_define(const uint32_t *argb, uint16_t width,
                                     uint16_t height, uint16_t hot_x,
                                     uint16_t hot_y) {
    uint32_t resource;
    void *raw = NULL;
    uint32_t *pixels;
    uint32_t bytes = VIRTIO_GPU_CURSOR_SIZE * VIRTIO_GPU_CURSOR_SIZE * 4U;
    uint32_t old_resource = g_virtio.cursor_resource;
    void *old_raw = g_virtio.cursor_raw;

    if (!g_virtio.active || !argb || !width || !height ||
        width > VIRTIO_GPU_CURSOR_SIZE || height > VIRTIO_GPU_CURSOR_SIZE ||
        hot_x >= width || hot_y >= height) return false;
    pixels = (uint32_t *)virtio_alloc_aligned(bytes, VIRTIO_GPU_PAGE_SIZE,
                                               &raw);
    if (!pixels) return false;
    for (uint16_t y = 0; y < height; y++)
        kmemcpy(pixels + (uint32_t)y * VIRTIO_GPU_CURSOR_SIZE,
                argb + (uint32_t)y * width,
                (uint32_t)width * sizeof(uint32_t));

    resource = virtio_gpu_next_resource();
    if (!virtio_gpu_resource_create(resource,
            VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM,
            VIRTIO_GPU_CURSOR_SIZE, VIRTIO_GPU_CURSOR_SIZE) ||
        !virtio_gpu_resource_attach(resource, pixels, bytes) ||
        !virtio_gpu_upload_cursor_resource(resource,
            VIRTIO_GPU_CURSOR_SIZE * 4U,
            (gfx_rect_t){0, 0, VIRTIO_GPU_CURSOR_SIZE,
                         VIRTIO_GPU_CURSOR_SIZE})) {
        virtio_gpu_resource_destroy(resource);
        kfree(raw);
        return false;
    }

    g_virtio.cursor_resource = resource;
    g_virtio.cursor_raw = raw;
    g_virtio.cursor_pixels = pixels;
    g_virtio.cursor_width = width;
    g_virtio.cursor_height = height;
    g_virtio.cursor_hot_x = hot_x;
    g_virtio.cursor_hot_y = hot_y;
    g_virtio.cursor_defined = true;
    g_virtio.cursor_host_defined = false;
    if (old_resource) virtio_gpu_resource_destroy(old_resource);
    if (old_raw) kfree(old_raw);
    kprintf("[VIRTIOGPU.DVR] cursor HW cargado: %ux%u hotspot=%u,%u recurso=%u\n",
            width, height, hot_x, hot_y, resource);
    if (g_virtio.cursor_visible && g_virtio.cursor_position_valid)
        return virtio_gpu_send_cursor(false);
    return true;
}

static bool virtio_gpu_cursor_move(int x, int y) {
    if (!g_virtio.active || !g_virtio.cursor_defined) return false;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((uint32_t)x >= g_virtio.width) x = (int)g_virtio.width - 1;
    if ((uint32_t)y >= g_virtio.height) y = (int)g_virtio.height - 1;
    if (g_virtio.cursor_position_valid &&
        x == g_virtio.cursor_x && y == g_virtio.cursor_y) return true;
    g_virtio.cursor_x = x;
    g_virtio.cursor_y = y;
    g_virtio.cursor_position_valid = true;
    return virtio_gpu_send_cursor(true);
}

static bool virtio_gpu_cursor_show(bool visible) {
    if (!g_virtio.active || !g_virtio.cursor_defined) return false;
    if (g_virtio.cursor_visible == visible &&
        (!visible || g_virtio.cursor_host_defined)) return true;
    g_virtio.cursor_visible = visible;
    if (visible && !g_virtio.cursor_position_valid) {
        g_virtio.cursor_x = 0;
        g_virtio.cursor_y = 0;
        g_virtio.cursor_position_valid = true;
    }
    return virtio_gpu_send_cursor(false);
}

static gfx_surface_handle_t virtio_gpu_surface_handle(uint32_t slot) {
    return ((uint32_t)g_virtio.surfaces[slot].generation << 8) | (slot + 1U);
}

static virtio_gpu_surface_t *virtio_gpu_surface_from_handle(
    gfx_surface_handle_t handle) {
    uint32_t slot = handle & 0xFFU;
    uint8_t generation = (uint8_t)(handle >> 8);
    if (!slot || slot > VIRTIO_GPU_MAX_SURFACES) return NULL;
    slot--;
    if (!g_virtio.surfaces[slot].used ||
        g_virtio.surfaces[slot].generation != generation) return NULL;
    return &g_virtio.surfaces[slot];
}

static bool virtio_gpu_surface_create(uint16_t width, uint16_t height,
                                      gfx_surface_handle_t *handle_out) {
    uint32_t slot, pitch, size;
    virtio_gpu_surface_t *surface;
    if (handle_out) *handle_out = GFX_SURFACE_INVALID;
    if (!g_virtio.active || !width || !height || !handle_out) return false;
    pitch = (uint32_t)width * 4U;
    if ((uint32_t)height > 0xFFFFFFFFU / pitch) return false;
    size = pitch * height;
    for (slot = 0; slot < VIRTIO_GPU_MAX_SURFACES; slot++)
        if (!g_virtio.surfaces[slot].used) break;
    if (slot == VIRTIO_GPU_MAX_SURFACES) return false;
    surface = &g_virtio.surfaces[slot];
    surface->pixels = (uint32_t *)kzalloc(size);
    if (!surface->pixels) return false;
    if (!mm_set_allocation_owner(surface->pixels, 0U)) {
        kfree(surface->pixels);
        surface->pixels = NULL;
        return false;
    }
    surface->used = true;
    if (++surface->generation == 0U) surface->generation = 1U;
    surface->width = width;
    surface->height = height;
    surface->pitch = pitch;
    surface->size = size;
    *handle_out = virtio_gpu_surface_handle(slot);
    return true;
}

static bool virtio_gpu_surface_destroy(gfx_surface_handle_t handle) {
    virtio_gpu_surface_t *surface = virtio_gpu_surface_from_handle(handle);
    if (!surface) return false;
    if (surface->pixels) kfree(surface->pixels);
    surface->pixels = NULL;
    surface->used = false;
    surface->width = 0U;
    surface->height = 0U;
    surface->pitch = 0U;
    surface->size = 0U;
    return true;
}

static bool virtio_gpu_surface_upload(gfx_surface_handle_t handle,
                                      const uint32_t *pixels,
                                      uint32_t source_pitch,
                                      const gfx_rect_t *rect) {
    virtio_gpu_surface_t *surface = virtio_gpu_surface_from_handle(handle);
    int x, y, w, h;
    if (!surface || !pixels || !rect || source_pitch < surface->width)
        return false;
    x = rect->x; y = rect->y; w = rect->w; h = rect->h;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > surface->width) w = surface->width - x;
    if (y + h > surface->height) h = surface->height - y;
    if (w <= 0 || h <= 0) return false;
    for (int row = 0; row < h; row++) {
        uint32_t *dst = surface->pixels +
            (uint32_t)(y + row) * surface->width + (uint32_t)x;
        const uint32_t *src = pixels +
            (uint32_t)(y + row) * source_pitch + (uint32_t)x;
        kmemcpy(dst, src, (uint32_t)w * sizeof(uint32_t));
    }
    return true;
}

static bool virtio_gpu_surface_blit(const gfx_info_t *info,
                                    gfx_surface_handle_t handle,
                                    int src_x, int src_y, int dst_x,
                                    int dst_y, int w, int h,
                                    uint32_t *fence_out) {
    virtio_gpu_surface_t *surface = virtio_gpu_surface_from_handle(handle);
    if (fence_out) *fence_out = 0U;
    if (!surface || !info || w <= 0 || h <= 0) return false;
    if (src_x < 0) { int d = -src_x; src_x = 0; dst_x += d; w -= d; }
    if (src_y < 0) { int d = -src_y; src_y = 0; dst_y += d; h -= d; }
    if (dst_x < 0) { int d = -dst_x; dst_x = 0; src_x += d; w -= d; }
    if (dst_y < 0) { int d = -dst_y; dst_y = 0; src_y += d; h -= d; }
    if (src_x + w > surface->width) w = surface->width - src_x;
    if (src_y + h > surface->height) h = surface->height - src_y;
    if (dst_x + w > info->width) w = info->width - dst_x;
    if (dst_y + h > info->height) h = info->height - dst_y;
    if (w <= 0 || h <= 0) return false;
    for (int row = 0; row < h; row++) {
        const uint32_t *src = surface->pixels +
            (uint32_t)(src_y + row) * surface->width + (uint32_t)src_x;
        uint32_t *dst = g_virtio.framebuffer +
            (uint32_t)(dst_y + row) * g_virtio.width + (uint32_t)dst_x;
        kmemcpy(dst, src, (uint32_t)w * sizeof(uint32_t));
    }
    (void)virtio_gpu_queue_dirty(dst_x, dst_y, w, h);
    return virtio_gpu_submit_dirty(fence_out);
}

typedef struct {uint32_t d[768];uint32_t n;} virgl_builder_t;
typedef struct {float p[4],c[4],t[4];} virgl_vertex_t;

static uint32_t virgl_f32(float f){union{float f;uint32_t u;}v;v.f=f;return v.u;}
static bool virgl_put(virgl_builder_t *b,uint32_t v){
    if(!b||b->n>=sizeof(b->d)/sizeof(b->d[0]))return false;
    b->d[b->n++]=v;return true;
}
static bool virgl_shader(virgl_builder_t *b,uint32_t handle,uint32_t type,
                         const char *text) {
    uint32_t bytes=0U,words;
    while(text[bytes])bytes++;
    bytes++;words=(bytes+3U)/4U;
    if(b->n+6U+words>sizeof(b->d)/sizeof(b->d[0]))return false;
    virgl_put(b,VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT,VIRGL_OBJECT_SHADER,
                           words+5U));
    virgl_put(b,handle);virgl_put(b,type);virgl_put(b,bytes);
    virgl_put(b,300U);virgl_put(b,0U);
    kmemset(&b->d[b->n],0,words*4U);
    kmemcpy(&b->d[b->n],text,bytes);b->n+=words;return true;
}

#define VG_OBJ_VE 1U
#define VG_OBJ_BLEND_OPAQUE 10U
#define VG_OBJ_BLEND_ALPHA 11U
#define VG_OBJ_BLEND_ADDITIVE 12U
#define VG_OBJ_DSA_OFF 20U
#define VG_OBJ_DSA_BASE 21U
#define VG_OBJ_DSA_WRITE_BASE 29U
#define VG_OBJ_RS 40U
#define VG_SHADER_VS 40U
#define VG_SHADER_FS_COLOR 41U
#define VG_SHADER_FS_TEXTURE 42U
#define VG_SAMPLER_NEAREST 50U
#define VG_SAMPLER_LINEAR 51U

static void vg_destroy_resource(uint32_t resource);

static bool virtio_gpu_virgl_init_pipeline(void) {
    static const char vs[]="VERT\nDCL IN[0]\nDCL IN[1]\nDCL IN[2]\n"
        "DCL OUT[0], POSITION\nDCL OUT[1], COLOR\nDCL OUT[2], GENERIC[0]\n"
        "0: MOV OUT[0], IN[0]\n1: MOV OUT[1], IN[1]\n"
        "2: MOV OUT[2], IN[2]\n3: END\n";
    static const char fs_color[]="FRAG\nDCL IN[0], COLOR, LINEAR\n"
        "DCL OUT[0], COLOR\n0: MOV OUT[0], IN[0]\n1: END\n";
    static const char fs_tex[]="FRAG\nDCL IN[0], COLOR, LINEAR\n"
        "DCL IN[1], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\n"
        "DCL SAMP[0]\nDCL SVIEW[0], 2D, FLOAT\nDCL TEMP[0]\n"
        "0: TEX TEMP[0], IN[1], SAMP[0], 2D\n"
        "1: MUL OUT[0], TEMP[0], IN[0]\n2: END\n";
    virgl_builder_t b={{0},0U};uint32_t rt;
    if(g_virtio.virgl_pipeline_ready)return true;
    /* Three float4 attributes: clip position, color and texture coordinate. */
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT,
        VIRGL_OBJECT_VERTEX_ELEMENTS,13U));virgl_put(&b,VG_OBJ_VE);
    virgl_put(&b,0U);virgl_put(&b,0U);virgl_put(&b,0U);
    virgl_put(&b,VIRGL_FORMAT_R32G32B32A32_FLOAT);
    virgl_put(&b,16U);virgl_put(&b,0U);virgl_put(&b,0U);
    virgl_put(&b,VIRGL_FORMAT_R32G32B32A32_FLOAT);
    virgl_put(&b,32U);virgl_put(&b,0U);virgl_put(&b,0U);
    virgl_put(&b,VIRGL_FORMAT_R32G32B32A32_FLOAT);
    /* Opaque, source-alpha and additive source-alpha blend objects. */
    for(uint32_t blend=0;blend<3U;blend++){
        uint32_t handle=blend==0U?VG_OBJ_BLEND_OPAQUE:
            (blend==1U?VG_OBJ_BLEND_ALPHA:VG_OBJ_BLEND_ADDITIVE);
        virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT,
        VIRGL_OBJECT_BLEND,11U));virgl_put(&b,handle);
        virgl_put(&b,0U);virgl_put(&b,0U);
        rt=15U<<27;
        if(blend){uint32_t dst=blend==2U?1U:0x13U;
            rt|=1U|(3U<<4)|(dst<<9)|(1U<<17)|(dst<<22);}
        virgl_put(&b,rt);for(uint32_t i=1;i<8U;i++)virgl_put(&b,0U);
    }
    /* Complete OpenGL depth comparison set, with and without Z writes. */
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT,VIRGL_OBJECT_DSA,5U));
    virgl_put(&b,VG_OBJ_DSA_OFF);virgl_put(&b,0U);virgl_put(&b,0U);
    virgl_put(&b,0U);virgl_put(&b,0U);
    for(uint32_t write=0U;write<2U;write++)for(uint32_t func=0U;func<8U;func++){
        uint32_t handle=(write?VG_OBJ_DSA_WRITE_BASE:VG_OBJ_DSA_BASE)+func;
        uint32_t s0=1U|(write?2U:0U)|(func<<2);
        virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT,VIRGL_OBJECT_DSA,5U));
        virgl_put(&b,handle);virgl_put(&b,s0);virgl_put(&b,0U);
        virgl_put(&b,0U);virgl_put(&b,0U);
    }
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT,VIRGL_OBJECT_RASTERIZER,9U));
    virgl_put(&b,VG_OBJ_RS);virgl_put(&b,(1U<<1)|(1U<<29)|(1U<<30));
    virgl_put(&b,virgl_f32(1.0f));virgl_put(&b,0U);virgl_put(&b,0U);
    virgl_put(&b,virgl_f32(1.0f));virgl_put(&b,0U);virgl_put(&b,0U);virgl_put(&b,0U);
    /* Samplers: clamp/nearest and repeat/linear. */
    for(uint32_t i=0;i<2U;i++){
        uint32_t s0=i?((1U<<9)|(1U<<13)):0U;
        virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT,
            VIRGL_OBJECT_SAMPLER_STATE,9U));virgl_put(&b,VG_SAMPLER_NEAREST+i);
        virgl_put(&b,s0);virgl_put(&b,0U);virgl_put(&b,0U);
        virgl_put(&b,virgl_f32(16.0f));
        for(uint32_t j=0;j<4U;j++)virgl_put(&b,0U);
    }
    if(!virgl_shader(&b,VG_SHADER_VS,0U,vs)||
       !virgl_shader(&b,VG_SHADER_FS_COLOR,1U,fs_color)||
       !virgl_shader(&b,VG_SHADER_FS_TEXTURE,1U,fs_tex)||
       !virtio_gpu_submit_3d(b.d,b.n))return false;
    g_virtio.virgl_vbo_capacity=256U*1024U;
    for(uint32_t slot=0U;slot<3U;slot++){
        g_virtio.virgl_vbo[slot]=virtio_alloc_aligned(
            g_virtio.virgl_vbo_capacity,4096U,
            &g_virtio.virgl_vbo_raw[slot]);
        g_virtio.virgl_vbo_resource[slot]=virtio_gpu_next_resource();
        if(!g_virtio.virgl_vbo[slot]||!virtio_gpu_resource_create_3d_ex(
           g_virtio.virgl_vbo_resource[slot],VIRGL_TARGET_BUFFER,
           VIRGL_FORMAT_R8_UNORM,VIRGL_BIND_VERTEX_BUFFER,
           g_virtio.virgl_vbo_capacity,1U)||
           !virtio_gpu_resource_attach(g_virtio.virgl_vbo_resource[slot],
            g_virtio.virgl_vbo[slot],g_virtio.virgl_vbo_capacity)||
           !virtio_gpu_context_resource(VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
                g_virtio.virgl_vbo_resource[slot])){
            for(uint32_t j=0U;j<=slot;j++){
                vg_destroy_resource(g_virtio.virgl_vbo_resource[j]);
                if(g_virtio.virgl_vbo_raw[j])
                    kfree(g_virtio.virgl_vbo_raw[j]);
                g_virtio.virgl_vbo_resource[j]=0U;
                g_virtio.virgl_vbo_raw[j]=NULL;
                g_virtio.virgl_vbo[j]=NULL;
            }
            g_virtio.virgl_vbo_capacity=0U;
            return false;
        }
    }
    g_virtio.virgl_pipeline_ready=true;return true;
}

static gfx3d_surface_handle_t vg_handle(uint32_t i,uint8_t gen){
    return ((uint32_t)gen<<8)|(i+1U);
}
static virtio_gpu_3d_surface_t *vg_surface(gfx3d_surface_handle_t h){
    uint32_t i=(h&0xFFU);if(!i||i>VIRTIO_GPU_MAX_3D_SURFACES)return NULL;i--;
    if(!g_virtio.surfaces3d[i].used||g_virtio.surfaces3d[i].generation!=(h>>8))return NULL;
    return &g_virtio.surfaces3d[i];
}
static void vg_destroy_resource(uint32_t resource){
    if(!resource)return;
    (void)virtio_gpu_context_resource(
        VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE,resource);
    (void)virtio_gpu_resource_detach(resource);(void)virtio_gpu_resource_unref(resource);
}

static bool vg_probe(gfx3d_info_t *info){
    if(!virtio_gpu_device_start()||!g_virtio.virgl_commands_ready)return false;
    if(!virtio_gpu_virgl_init_pipeline()){
        g_virtio.virgl_commands_ready=false;return false;
    }
    if(info)*info=(gfx3d_info_t){true,"virtio_virgl","VirtIO GPU/VirGL",
        GFX3D_CAP_FIXED_FUNCTION|GFX3D_CAP_RENDER_TARGETS|
        GFX3D_CAP_VERTEX_BUFFERS|GFX3D_CAP_SURFACE_DMA|GFX3D_CAP_PRESENT|
        GFX3D_CAP_ALPHA_BLEND|GFX3D_CAP_TEXTURES|GFX3D_CAP_TINYGL|
        GFX3D_CAP_DEPTH_BUFFER|GFX3D_CAP_DEPTH_SURFACE_IO|
        GFX3D_CAP_DEPTH_FUNCS|GFX3D_CAP_BLEND_ADDITIVE|
        GFX3D_CAP_TEXTURE_REGION_UPLOAD,2U,1U,1U};
    return true;
}
static void vg_reset(void){
    virgl_builder_t b={{0},0U};
    g_virtio.virgl_target=0U;
    /* Resources are released by the normal surface owner. On a device reset
       the host drops them with the context; clear guest allocations here. */
    for(uint32_t i=0;i<VIRTIO_GPU_MAX_3D_SURFACES;i++){
        virtio_gpu_3d_surface_t *s=&g_virtio.surfaces3d[i];
        if(!s->used)continue;
        (void)vg_surface_destroy(vg_handle(i,s->generation));
    }
    if(g_virtio.virgl_pipeline_ready){
      const uint32_t handles[]={VG_OBJ_VE,VG_OBJ_BLEND_OPAQUE,
        VG_OBJ_BLEND_ALPHA,VG_OBJ_BLEND_ADDITIVE,VG_OBJ_DSA_OFF,VG_OBJ_RS,
        VG_SHADER_VS,VG_SHADER_FS_COLOR,VG_SHADER_FS_TEXTURE,
        VG_SAMPLER_NEAREST,VG_SAMPLER_LINEAR};
      const uint32_t objects[]={VIRGL_OBJECT_VERTEX_ELEMENTS,VIRGL_OBJECT_BLEND,
        VIRGL_OBJECT_BLEND,VIRGL_OBJECT_BLEND,VIRGL_OBJECT_DSA,
        VIRGL_OBJECT_RASTERIZER,VIRGL_OBJECT_SHADER,VIRGL_OBJECT_SHADER,
        VIRGL_OBJECT_SHADER,VIRGL_OBJECT_SAMPLER_STATE,VIRGL_OBJECT_SAMPLER_STATE};
      for(uint32_t i=0;i<sizeof(handles)/sizeof(handles[0]);i++){
        virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_DESTROY_OBJECT,objects[i],1U));
        virgl_put(&b,handles[i]);
      }
      for(uint32_t write=0U;write<2U;write++)for(uint32_t func=0U;func<8U;func++){
        virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_DESTROY_OBJECT,VIRGL_OBJECT_DSA,1U));
        virgl_put(&b,(write?VG_OBJ_DSA_WRITE_BASE:VG_OBJ_DSA_BASE)+func);
      }
      (void)virtio_gpu_submit_3d(b.d,b.n);
    }
    for(uint32_t slot=0U;slot<3U;slot++){
        vg_destroy_resource(g_virtio.virgl_vbo_resource[slot]);
        if(g_virtio.virgl_vbo_raw[slot])kfree(g_virtio.virgl_vbo_raw[slot]);
        g_virtio.virgl_vbo_resource[slot]=0U;
        g_virtio.virgl_vbo_raw[slot]=NULL;g_virtio.virgl_vbo[slot]=NULL;
        g_virtio.virgl_vbo_hash[slot]=0U;
        g_virtio.virgl_vbo_uploaded[slot]=0U;
    }
    g_virtio.virgl_vbo_capacity=0U;
    for(uint32_t slot=0U;slot<3U;slot++)g_virtio.virgl_vbo_used[slot]=0U;
    g_virtio.virgl_frame_command_count=0U;
    g_virtio.virgl_frame_batching=false;
    g_virtio.virgl_pipeline_ready=false;
}

static bool vg_surface_create(const gfx3d_surface_desc_t *desc,
                              gfx3d_surface_handle_t *out){
    virtio_gpu_3d_surface_t *s=NULL;uint32_t i,bytes,bind;
    if(out)*out=0U;
    if(!desc||!out||!desc->width||!desc->height||
       desc->format==GFX3D_FORMAT_Z16||!vg_probe(NULL))return false;
    for(i=0;i<VIRTIO_GPU_MAX_3D_SURFACES;i++)if(!g_virtio.surfaces3d[i].used){s=&g_virtio.surfaces3d[i];break;}
    if(!s)return false;
    bytes=(uint32_t)desc->width*desc->height*4U;
    s->raw=NULL;s->pixels=(uint32_t *)virtio_alloc_aligned(
        ALIGN_UP(bytes,4096U),4096U,&s->raw);if(!s->pixels)return false;
    s->resource=virtio_gpu_next_resource();s->width=desc->width;s->height=desc->height;
    s->bytes=bytes;s->flags=desc->flags;s->format=desc->format;
    bind=VIRGL_BIND_SAMPLER_VIEW;
    if(desc->flags&GFX3D_SURFACE_RENDER_TARGET)bind|=VIRGL_BIND_RENDER_TARGET;
    if(desc->flags&GFX3D_SURFACE_WINDOW)bind|=VIRGL_BIND_SCANOUT;
    if(!virtio_gpu_resource_create_3d_ex(s->resource,VIRGL_TARGET_TEXTURE_2D,
        VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM,bind,s->width,s->height)||
       !virtio_gpu_resource_attach(s->resource,s->pixels,ALIGN_UP(bytes,4096U))||
       !virtio_gpu_context_resource(VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,s->resource))goto fail;
    s->surface_object=100U+i;s->depth_surface_object=200U+i;
    s->sampler_view_object=300U+i;
    {virgl_builder_t b={{0},0U};
     virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT,VIRGL_OBJECT_SURFACE,5U));
     virgl_put(&b,s->surface_object);virgl_put(&b,s->resource);
     virgl_put(&b,VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM);virgl_put(&b,0U);virgl_put(&b,0U);
     virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT,VIRGL_OBJECT_SAMPLER_VIEW,6U));
     virgl_put(&b,s->sampler_view_object);virgl_put(&b,s->resource);
     virgl_put(&b,VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM);virgl_put(&b,0U);
     virgl_put(&b,0U);virgl_put(&b,0x688U);
     if(!virtio_gpu_submit_3d(b.d,b.n))goto fail;}
    if(desc->flags&GFX3D_SURFACE_RENDER_TARGET){uint32_t db=(uint32_t)s->width*s->height*2U;
        s->depth_pixels=virtio_alloc_aligned(ALIGN_UP(db,4096U),4096U,&s->depth_raw);
        s->depth_resource=virtio_gpu_next_resource();
        if(!s->depth_pixels||!virtio_gpu_resource_create_3d_ex(s->depth_resource,
           VIRGL_TARGET_TEXTURE_2D,VIRGL_FORMAT_Z16_UNORM,
           VIRGL_BIND_DEPTH_STENCIL,s->width,s->height)||
           !virtio_gpu_resource_attach(s->depth_resource,s->depth_pixels,
                                       ALIGN_UP(db,4096U))||
           !virtio_gpu_context_resource(VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
                                        s->depth_resource))goto fail;
        {virgl_builder_t b={{0},0U};
         virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT,VIRGL_OBJECT_SURFACE,5U));
         virgl_put(&b,s->depth_surface_object);virgl_put(&b,s->depth_resource);
         virgl_put(&b,VIRGL_FORMAT_Z16_UNORM);virgl_put(&b,0U);virgl_put(&b,0U);
         if(!virtio_gpu_submit_3d(b.d,b.n))goto fail;}
    }
    s->generation++;if(!s->generation)s->generation=1U;s->used=true;
    *out=vg_handle(i,s->generation);return true;
fail:
    vg_destroy_resource(s->depth_resource);vg_destroy_resource(s->resource);
    if(s->depth_raw)kfree(s->depth_raw);
    if(s->raw)kfree(s->raw);
    kmemset(s,0,sizeof(*s));return false;
}

static bool vg_surface_destroy(gfx3d_surface_handle_t h){
    virtio_gpu_3d_surface_t *s=vg_surface(h);virgl_builder_t b={{0},0U};
    if(!s)return false;
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_DESTROY_OBJECT,VIRGL_OBJECT_SURFACE,1U));
    virgl_put(&b,s->surface_object);
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_DESTROY_OBJECT,VIRGL_OBJECT_SAMPLER_VIEW,1U));
    virgl_put(&b,s->sampler_view_object);
    if(s->depth_resource){virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_DESTROY_OBJECT,
        VIRGL_OBJECT_SURFACE,1U));virgl_put(&b,s->depth_surface_object);}
    (void)virtio_gpu_submit_3d(b.d,b.n);
    vg_destroy_resource(s->depth_resource);vg_destroy_resource(s->resource);
    if(s->depth_raw)kfree(s->depth_raw);
    if(s->raw)kfree(s->raw);
    if(g_virtio.virgl_target==h)g_virtio.virgl_target=0U;
    {uint8_t generation=s->generation;kmemset(s,0,sizeof(*s));s->generation=generation;}
    return true;
}
static bool vg_surface_upload(gfx3d_surface_handle_t h,const uint32_t *pixels,
        uint32_t pitch,const gfx_rect_t *rect){
    virtio_gpu_3d_surface_t *s=vg_surface(h);gfx_rect_t r;
    if(!s||!pixels||pitch<s->width)return false;
    r=rect?*rect:(gfx_rect_t){0,0,s->width,s->height};
    if(r.x<0||r.y<0||r.w<=0||r.h<=0||r.x+r.w>s->width||r.y+r.h>s->height)return false;
    for(int y=0;y<r.h;y++)kmemcpy(s->pixels+(uint32_t)(r.y+y)*s->width+r.x,
        pixels+(uint32_t)(r.y+y)*pitch+r.x,(uint32_t)r.w*4U);
    return virtio_gpu_transfer_host_3d(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,
        s->resource,s->width,s->height,(uint32_t)s->width*4U);
}
static bool vg_surface_upload_region(gfx3d_surface_handle_t h,
        const uint32_t *pixels,uint32_t pitch,uint32_t dx,uint32_t dy,
        uint32_t width,uint32_t height){
    virtio_gpu_3d_surface_t *s=vg_surface(h);
    if(!s||!pixels||!pitch||pitch<width||!width||!height||
       dx+width>s->width||dy+height>s->height)return false;
    for(uint32_t y=0U;y<height;y++)
        kmemcpy(s->pixels+(dy+y)*s->width+dx,pixels+y*pitch,width*4U);
    return virtio_gpu_transfer_host_3d(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,
        s->resource,s->width,s->height,(uint32_t)s->width*4U);
}

static bool vg_surface_download(gfx3d_surface_handle_t h,uint32_t *pixels,
        uint32_t pitch,const gfx_rect_t *rect){
    virtio_gpu_3d_surface_t *s=vg_surface(h);gfx_rect_t r;
    if(!s||!pixels||pitch<s->width||!virtio_gpu_transfer_host_3d(
       VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,s->resource,s->width,s->height,
       (uint32_t)s->width*4U))return false;
    r=rect?*rect:(gfx_rect_t){0,0,s->width,s->height};
    if(r.x<0||r.y<0||r.w<=0||r.h<=0||r.x+r.w>s->width||r.y+r.h>s->height)return false;
    for(int y=0;y<r.h;y++)kmemcpy(pixels+(uint32_t)(r.y+y)*pitch+r.x,
        s->pixels+(uint32_t)(r.y+y)*s->width+r.x,(uint32_t)r.w*4U);
    return true;
}
static bool vg_depth_transfer(gfx3d_surface_handle_t h,uint16_t *depth,
        uint32_t pitch,const gfx_rect_t *rect,bool upload){
    virtio_gpu_3d_surface_t *s=vg_surface(h);gfx_rect_t r;
    uint16_t *storage;
    if(!s||!s->depth_resource||!s->depth_pixels||!depth||pitch<s->width)
        return false;
    r=rect?*rect:(gfx_rect_t){0,0,s->width,s->height};
    if(r.x<0||r.y<0||r.w<=0||r.h<=0||r.x+r.w>s->width||r.y+r.h>s->height)
        return false;
    storage=(uint16_t *)s->depth_pixels;
    if(!upload&&!virtio_gpu_transfer_host_3d(
       VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,s->depth_resource,
       s->width,s->height,(uint32_t)s->width*2U))return false;
    for(int y=0;y<r.h;y++){
        uint16_t *gpu=storage+(uint32_t)(r.y+y)*s->width+(uint32_t)r.x;
        uint16_t *cpu=depth+(uint32_t)(r.y+y)*pitch+(uint32_t)r.x;
        if(upload)kmemcpy(gpu,cpu,(uint32_t)r.w*2U);
        else kmemcpy(cpu,gpu,(uint32_t)r.w*2U);
    }
    return !upload||virtio_gpu_transfer_host_3d(
        VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,s->depth_resource,
        s->width,s->height,(uint32_t)s->width*2U);
}
static bool vg_depth_upload(gfx3d_surface_handle_t h,const uint16_t *depth,
        uint32_t pitch,const gfx_rect_t *rect){
    return vg_depth_transfer(h,(uint16_t *)depth,pitch,rect,true);
}
static bool vg_depth_download(gfx3d_surface_handle_t h,uint16_t *depth,
        uint32_t pitch,const gfx_rect_t *rect){
    return vg_depth_transfer(h,depth,pitch,rect,false);
}

static bool vg_clear_internal(virtio_gpu_3d_surface_t *s,uint32_t color,
                              float depth,uint32_t flags){
    virgl_builder_t b={{0},0U};uint32_t buffers=0U;
    if(!s)return false;
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_SET_FRAMEBUFFER_STATE,0U,3U));
    virgl_put(&b,1U);virgl_put(&b,s->depth_surface_object);virgl_put(&b,s->surface_object);
    if(flags&GFX3D_DRAW_CLEAR_COLOR)buffers|=VIRGL_CLEAR_COLOR0;
    if((flags&GFX3D_DRAW_CLEAR_DEPTH)&&s->depth_resource)buffers|=VIRGL_CLEAR_DEPTH;
    if(buffers){virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_CLEAR,0U,8U));virgl_put(&b,buffers);
      virgl_put(&b,virgl_f32((float)((color>>16)&255U)/255.0f));
      virgl_put(&b,virgl_f32((float)((color>>8)&255U)/255.0f));
      virgl_put(&b,virgl_f32((float)(color&255U)/255.0f));
      virgl_put(&b,virgl_f32((float)(color>>24)/255.0f));
      virgl_put(&b,0U);virgl_put(&b,depth>=1.0f?0x3FF00000U:0U);virgl_put(&b,0U);}
    return virtio_gpu_submit_3d(b.d,b.n);
}
static bool vg_surface_clear(gfx3d_surface_handle_t h,uint32_t color,uint32_t *f){
    bool ok=vg_clear_internal(vg_surface(h),color,0.0f,GFX3D_DRAW_CLEAR_COLOR);
    if(f)*f=0U;
    return ok;
}
static bool vg_surface_present(gfx3d_surface_handle_t h,const gfx_rect_t *r,
                               uint32_t *f){
    virtio_gpu_3d_surface_t *s=vg_surface(h);gfx_rect_t full;
    if(f)*f=0U;
    if(!s||!g_virtio.active||s->width!=g_virtio.width||
       s->height!=g_virtio.height)return false;
    full=r?*r:(gfx_rect_t){0,0,s->width,s->height};
    if((s->flags&GFX3D_SURFACE_WINDOW)&&
       (g_virtio.scanout_resource==s->resource||
        virtio_gpu_set_scanout_resource(s->resource,s->width,s->height))){
        g_virtio.scanout_resource=s->resource;
        if(virtio_gpu_flush_resource(s->resource,full,f))return true;
    }
    /* Compatibility fallback for hosts that reject scanout-capable VirGL
       resources. This is intentionally not used on a working virglrenderer. */
    if(!vg_surface_download(h,g_virtio.framebuffer,g_virtio.width,&full))return false;
    return virtio_gpu_upload_scanout_rect(g_virtio.display_resource,
                                          g_virtio.pitch,full,f);
}
static bool vg_begin(gfx3d_surface_handle_t h,uint32_t color,float depth,uint32_t flags){
    virtio_gpu_3d_surface_t *s=vg_surface(h);if(!s)return false;
    if(g_virtio.virgl_frame_batching)return false;
    g_virtio.virgl_target=h;
    g_virtio.virgl_vbo_index=(g_virtio.virgl_vbo_index+1U)%3U;
    for(uint32_t slot=0U;slot<3U;slot++)g_virtio.virgl_vbo_used[slot]=0U;
    g_virtio.virgl_frame_command_count=0U;
    g_virtio.virgl_frame_batching=true;
    if(vg_clear_internal(s,color,depth,flags))return true;
    g_virtio.virgl_target=0U;g_virtio.virgl_frame_batching=false;
    return false;
}
static bool vg_draw(gfx3d_surface_handle_t target,gfx3d_surface_handle_t texture,
        const gfx3d_vertex_t *vertices,uint32_t count,uint32_t flags,uint32_t *f){
    virtio_gpu_3d_surface_t *dst=vg_surface(target),*tex=texture?vg_surface(texture):NULL;
    virgl_vertex_t *v;uint32_t resource,bytes,offset;
    virgl_builder_t b={{0},0U};bool ok=false;
    if(f)*f=0U;
    if(!dst||!vertices||count<3U||(flags&GFX3D_DRAW_TEXTURED&&!tex))return false;
    bytes=count*sizeof(*v);
    if(!g_virtio.virgl_frame_batching||target!=g_virtio.virgl_target||
       bytes>g_virtio.virgl_vbo_capacity||
       !g_virtio.virgl_vbo_resource[g_virtio.virgl_vbo_index])return false;
    if(bytes>g_virtio.virgl_vbo_capacity-
       g_virtio.virgl_vbo_used[g_virtio.virgl_vbo_index]){
        uint32_t next=(g_virtio.virgl_vbo_index+1U)%3U;
        if(g_virtio.virgl_vbo_used[next])return false;
        g_virtio.virgl_vbo_index=next;
    }
    offset=g_virtio.virgl_vbo_used[g_virtio.virgl_vbo_index];
    v=(virgl_vertex_t *)((uint8_t *)g_virtio.virgl_vbo[
        g_virtio.virgl_vbo_index]+offset);
    for(uint32_t i=0;i<count;i++){uint32_t c=vertices[i].color;
      v[i].p[0]=(vertices[i].x/(float)dst->width)*2.0f-1.0f;
      v[i].p[1]=(vertices[i].y/(float)dst->height)*2.0f-1.0f;
      v[i].p[2]=vertices[i].z*2.0f-1.0f;v[i].p[3]=1.0f;
      v[i].c[0]=(float)((c>>16)&255U)/255.0f;v[i].c[1]=(float)((c>>8)&255U)/255.0f;
      v[i].c[2]=(float)(c&255U)/255.0f;v[i].c[3]=(float)(c>>24)/255.0f;
      v[i].t[0]=vertices[i].u;v[i].t[1]=vertices[i].v;v[i].t[2]=0.0f;v[i].t[3]=1.0f;}
    resource=g_virtio.virgl_vbo_resource[g_virtio.virgl_vbo_index];
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_SET_FRAMEBUFFER_STATE,0U,3U));
    virgl_put(&b,1U);virgl_put(&b,dst->depth_surface_object);virgl_put(&b,dst->surface_object);
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT,VIRGL_OBJECT_VERTEX_ELEMENTS,1U));virgl_put(&b,VG_OBJ_VE);
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_SET_VERTEX_BUFFERS,0U,3U));
    virgl_put(&b,sizeof(*v));virgl_put(&b,offset);virgl_put(&b,resource);
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT,VIRGL_OBJECT_BLEND,1U));
    virgl_put(&b,(flags&GFX3D_DRAW_BLEND)?
        ((flags&GFX3D_DRAW_BLEND_ADDITIVE)?VG_OBJ_BLEND_ADDITIVE:
         VG_OBJ_BLEND_ALPHA):VG_OBJ_BLEND_OPAQUE);
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT,VIRGL_OBJECT_DSA,1U));
    virgl_put(&b,(flags&GFX3D_DRAW_DEPTH_TEST)?
        (((flags&GFX3D_DRAW_DEPTH_WRITE)?VG_OBJ_DSA_WRITE_BASE:VG_OBJ_DSA_BASE)+
         (uint32_t)gfx3d_draw_depth_func(flags)):VG_OBJ_DSA_OFF);
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT,VIRGL_OBJECT_RASTERIZER,1U));virgl_put(&b,VG_OBJ_RS);
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_SET_VIEWPORT_STATE,0U,7U));virgl_put(&b,0U);
    virgl_put(&b,virgl_f32((float)dst->width*0.5f));
    virgl_put(&b,virgl_f32((float)dst->height*0.5f));virgl_put(&b,virgl_f32(0.5f));
    virgl_put(&b,virgl_f32((float)dst->width*0.5f));
    virgl_put(&b,virgl_f32((float)dst->height*0.5f));virgl_put(&b,virgl_f32(0.5f));
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_BIND_SHADER,0U,2U));virgl_put(&b,VG_SHADER_VS);virgl_put(&b,0U);
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_BIND_SHADER,0U,2U));
    virgl_put(&b,tex?VG_SHADER_FS_TEXTURE:VG_SHADER_FS_COLOR);virgl_put(&b,1U);
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_LINK_SHADER,0U,6U));virgl_put(&b,VG_SHADER_VS);
    virgl_put(&b,tex?VG_SHADER_FS_TEXTURE:VG_SHADER_FS_COLOR);
    for(uint32_t i=0;i<4U;i++)virgl_put(&b,0U);
    if(tex){virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_SET_SAMPLER_VIEWS,0U,3U));
      virgl_put(&b,1U);virgl_put(&b,0U);virgl_put(&b,tex->sampler_view_object);
      virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_BIND_SAMPLER_STATES,0U,3U));
      virgl_put(&b,1U);virgl_put(&b,0U);
      virgl_put(&b,(flags&GFX3D_DRAW_LINEAR)?VG_SAMPLER_LINEAR:VG_SAMPLER_NEAREST);}
    virgl_put(&b,VIRGL_CMD0(VIRGL_CCMD_DRAW_VBO,0U,12U));virgl_put(&b,0U);
    virgl_put(&b,count);virgl_put(&b,4U);virgl_put(&b,0U);virgl_put(&b,1U);
    virgl_put(&b,0U);virgl_put(&b,0U);virgl_put(&b,0U);virgl_put(&b,0U);
    virgl_put(&b,0U);virgl_put(&b,count-1U);virgl_put(&b,0U);
    if(g_virtio.virgl_frame_command_count+b.n>
       sizeof(g_virtio.virgl_frame_commands)/
       sizeof(g_virtio.virgl_frame_commands[0]))return false;
    kmemcpy(&g_virtio.virgl_frame_commands[
        g_virtio.virgl_frame_command_count],b.d,b.n*sizeof(uint32_t));
    g_virtio.virgl_frame_command_count+=b.n;
    g_virtio.virgl_vbo_used[g_virtio.virgl_vbo_index]+=bytes;
    ok=true;
    if(ok&&f){uint32_t fence=virtio_gpu_next_fence();g_virtio.completed_fence=fence;*f=fence;}
    return ok;
}
static bool vg_end(gfx3d_surface_handle_t h,uint32_t *f){
    bool ok=true;
    if(h!=g_virtio.virgl_target||!g_virtio.virgl_frame_batching)return false;
    for(uint32_t slot=0U;slot<3U&&ok;slot++)if(g_virtio.virgl_vbo_used[slot]){
        uint32_t hash=2166136261U;
        const uint32_t *words=(const uint32_t *)g_virtio.virgl_vbo[slot];
        uint32_t word_count=g_virtio.virgl_vbo_used[slot]/4U;
        for(uint32_t i=0U;i<word_count;i++)
            hash=(hash^words[i])*16777619U;
        /* Unchanged immediate-mode geometry remains in the host resource.
           Camera/object edits naturally change the transformed vertex hash. */
        if(g_virtio.virgl_vbo_uploaded[slot]!=g_virtio.virgl_vbo_used[slot]||
           g_virtio.virgl_vbo_hash[slot]!=hash){
            ok=virtio_gpu_transfer_host_3d(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,
                g_virtio.virgl_vbo_resource[slot],g_virtio.virgl_vbo_used[slot],
                1U,g_virtio.virgl_vbo_used[slot]);
            if(ok){g_virtio.virgl_vbo_hash[slot]=hash;
                   g_virtio.virgl_vbo_uploaded[slot]=
                       g_virtio.virgl_vbo_used[slot];}
        }
    }
    if(ok&&g_virtio.virgl_frame_command_count)
        ok=virtio_gpu_submit_3d(g_virtio.virgl_frame_commands,
                                g_virtio.virgl_frame_command_count);
    g_virtio.virgl_target=0U;
    g_virtio.virgl_frame_batching=false;
    g_virtio.virgl_frame_command_count=0U;
    for(uint32_t slot=0U;slot<3U;slot++)g_virtio.virgl_vbo_used[slot]=0U;
    if(!ok){if(f)*f=0U;return false;}
    if(f){*f=virtio_gpu_next_fence();g_virtio.completed_fence=*f;}return true;
}
static bool vg_composite(gfx3d_surface_handle_t source_handle,
        gfx3d_surface_handle_t destination_handle,
        const gfx3d_composite_t *operation,uint32_t *f){
    virtio_gpu_3d_surface_t *source=vg_surface(source_handle);
    virtio_gpu_3d_surface_t *destination=vg_surface(destination_handle);
    gfx3d_vertex_t v[6];gfx_rect_t src;uint32_t color,flags;
    float x0,y0,x1,y1,x2,y2,x3,y3,u0,v0,u1,v1;
    if(f)*f=0U;
    if(!source||!destination||!operation||
       !(source->flags&GFX3D_SURFACE_TEXTURE)||
       !(destination->flags&GFX3D_SURFACE_RENDER_TARGET))return false;
    src=operation->source;
    if(src.x<0||src.y<0||src.w<=0||src.h<=0||
       src.x+src.w>source->width||src.y+src.h>source->height)return false;
    x0=operation->transform.tx;y0=operation->transform.ty;
    x1=operation->transform.m00*src.w+operation->transform.tx;
    y1=operation->transform.m10*src.w+operation->transform.ty;
    x2=operation->transform.m00*src.w+operation->transform.m01*src.h+
       operation->transform.tx;
    y2=operation->transform.m10*src.w+operation->transform.m11*src.h+
       operation->transform.ty;
    x3=operation->transform.m01*src.h+operation->transform.tx;
    y3=operation->transform.m11*src.h+operation->transform.ty;
    u0=(float)src.x/source->width;v0=(float)src.y/source->height;
    u1=(float)(src.x+src.w)/source->width;
    v1=(float)(src.y+src.h)/source->height;
    color=operation->modulation_color?operation->modulation_color:0xFFFFFFFFU;
    color=(color&0x00FFFFFFU)|
          ((uint32_t)(((color>>24)*operation->opacity)/255U)<<24);
    flags=GFX3D_DRAW_TEXTURED|GFX3D_DRAW_BLEND;
    if(operation->filter==GFX3D_FILTER_LINEAR)flags|=GFX3D_DRAW_LINEAR;
    if(operation->source_premultiplied)flags|=GFX3D_DRAW_PREMULTIPLIED;
#define VG_CV(i,px,py,tu,tv) do{v[i]=(gfx3d_vertex_t){px,py,0.0f,1.0f,color,tu,tv};}while(0)
    VG_CV(0,x0,y0,u0,v0);VG_CV(1,x1,y1,u1,v0);VG_CV(2,x2,y2,u1,v1);
    VG_CV(3,x0,y0,u0,v0);VG_CV(4,x2,y2,u1,v1);VG_CV(5,x3,y3,u0,v1);
#undef VG_CV
    return vg_draw(destination_handle,source_handle,v,6U,flags,f);
}
static bool vg_wait(uint32_t f){return !f||(int32_t)(g_virtio.completed_fence-f)>=0;}
static bool vg_selftest(uint32_t *f){gfx3d_surface_desc_t d={32U,32U,
    GFX3D_FORMAT_ARGB8888,GFX3D_SURFACE_RENDER_TARGET};
    gfx3d_surface_desc_t td={2U,2U,GFX3D_FORMAT_ARGB8888,
        GFX3D_SURFACE_TEXTURE|GFX3D_SURFACE_DYNAMIC};
    gfx3d_surface_handle_t h,texture=0U;
    virtio_gpu_3d_surface_t *s;uint32_t fence=0U;bool ok;
    static const gfx3d_vertex_t triangle[3]={
      {4.0f,4.0f,0.75f,1.0f,0xFFFFFFFFU,0.0f,0.0f},
      {28.0f,4.0f,0.75f,1.0f,0xFFFFFFFFU,1.0f,0.0f},
      {16.0f,28.0f,0.75f,1.0f,0xFFFFFFFFU,0.5f,1.0f}};
    static const uint32_t texels[4]={0xFFFF6020U,0xFFFF6020U,
                                     0xFFFF6020U,0xFFFF6020U};
    gfx_rect_t texrect={0,0,2,2};
    if(f)*f=0U;
    if(!vg_surface_create(&d,&h))return false;
    if(!vg_surface_create(&td,&texture)||
       !vg_surface_upload(texture,texels,2U,&texrect)){
        vg_surface_destroy(h);return false;
    }
    s=vg_surface(h);ok=vg_begin(h,0xFF000000U,0.0f,
       GFX3D_DRAW_CLEAR_COLOR|GFX3D_DRAW_CLEAR_DEPTH)&&
       vg_draw(h,texture,triangle,3U,GFX3D_DRAW_TEXTURED|
               GFX3D_DRAW_DEPTH_TEST|
               GFX3D_DRAW_DEPTH_WRITE|
               GFX3D_DRAW_REVERSED_DEPTH,&fence)&&vg_end(h,&fence)&&s&&
       virtio_gpu_transfer_host_3d(VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,
        s->resource,s->width,s->height,(uint32_t)s->width*4U)&&s->pixels[0]!=0U;
    if(ok)ok=s->pixels[12U*s->width+16U]!=0xFF000000U;
    vg_surface_destroy(texture);vg_surface_destroy(h);return ok;}

static bool vg_register(void){static const gfx3d_driver_ops_t ops={
    BK_GFX3D_DRIVER_ABI_VERSION,sizeof(gfx3d_driver_ops_t),"virtio_virgl",245U,
    GFX3D_CAP_FIXED_FUNCTION|GFX3D_CAP_RENDER_TARGETS|GFX3D_CAP_VERTEX_BUFFERS|
    GFX3D_CAP_SURFACE_DMA|GFX3D_CAP_PRESENT|GFX3D_CAP_ALPHA_BLEND|
    GFX3D_CAP_TEXTURES|GFX3D_CAP_TINYGL|GFX3D_CAP_DEPTH_BUFFER|
    GFX3D_CAP_DEPTH_SURFACE_IO|GFX3D_CAP_DEPTH_FUNCS|
    GFX3D_CAP_BLEND_ADDITIVE|GFX3D_CAP_TEXTURE_REGION_UPLOAD|
    GFX3D_CAP_SCALE|GFX3D_CAP_TRANSFORM|GFX3D_CAP_WINDOW_SURFACES|
    GFX3D_CAP_GLYPH_ATLAS,
    vg_probe,vg_reset,vg_surface_create,vg_surface_destroy,vg_surface_upload,
    vg_surface_download,vg_surface_clear,vg_composite,vg_surface_present,vg_begin,vg_draw,
    vg_end,vg_wait,vg_selftest,vg_surface_upload_region,vg_depth_upload,
    vg_depth_download};uint32_t fence=0U;
    if(!gfx3d_register_driver(&ops))return false;
    if(g_virtio.virgl_commands_ready){
        if(!vg_selftest(&fence)){
            kprintf("[VIRTIOGPU.DVR] DRAW_VBO fallo; backend TinyGL deshabilitado\n");
            g_virtio.virgl_commands_ready=false;
        }else kprintf("[VIRTIOGPU.DVR] VirGL GFX3D/TinyGL: DRAW_VBO + Z + textura + descarga OK\n");
    }
    return true;}

static bool virtio_gpu_driver_init(void) {
    static const gfx_driver_ops_t ops = {
        BK_GFX_DRIVER_ABI_VERSION,
        sizeof(gfx_driver_ops_t),
        "virtio_gpu",
        240U,
        GFX_CAP_PRESENT_BUFFER | GFX_CAP_DIRTY_RECTS | GFX_CAP_HW_CURSOR |
        GFX_CAP_BITBLT | GFX_CAP_FILL,
        virtio_gpu_activate,
        virtio_gpu_runtime_capabilities,
        virtio_gpu_disable,
        virtio_gpu_list_modes,
        virtio_gpu_set_mode,
        virtio_gpu_present_buffer,
        virtio_gpu_update_rect,
        virtio_gpu_flush,
        virtio_gpu_wait_fence,
        virtio_gpu_fill_rect,
        virtio_gpu_bitblt,
        virtio_gpu_cursor_define,
        virtio_gpu_cursor_move,
        virtio_gpu_cursor_show,
        virtio_gpu_surface_create,
        virtio_gpu_surface_destroy,
        virtio_gpu_surface_upload,
        virtio_gpu_surface_blit
    };
    kmemset(&g_virtio, 0, sizeof(g_virtio));
    g_virtio.pci = virtio_gpu_find_device();
    if (!g_virtio.pci) {
        kprintf("[VIRTIOGPU.DVR] dispositivo 1AF4:1050/1010 no detectado\n");
        return false;
    }
    if (!virtio_gpu_map_transport()) {
        kprintf("[VIRTIOGPU.DVR] dispositivo detectado, transporte moderno ausente\n");
        return false;
    }
    if (!gfx_register_driver(&ops)||!vg_register()) return false;
    kprintf("[VIRTIOGPU.DVR] registrado en PCI %u:%u.%u, prioridad=%u\n",
            g_virtio.pci->bus, g_virtio.pci->slot,
            g_virtio.pci->function, ops.priority);
    return true;
}

static void virtio_gpu_driver_shutdown(void) {
    virtio_gpu_disable();
}

const bk_driver_module_t *bleskernos_driver_query(void) {
    static const bk_driver_module_t module = {
        BK_DRIVER_ABI_VERSION,
        sizeof(bk_driver_module_t),
        "virtio_gpu",
        "VirtIO GPU: scanout 2D rapido, cursor HW y VirGL GFX3D/TinyGL",
        virtio_gpu_driver_init,
        virtio_gpu_driver_shutdown
    };
    return &module;
}
