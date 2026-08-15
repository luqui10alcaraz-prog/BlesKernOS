#include "../../include/svga_transport.h"

static const bk_svga_transport_ops_t *g_transport;

bool svga_transport_register(const bk_svga_transport_ops_t *ops) {
    if (!ops || ops->abi_version != BK_SVGA_TRANSPORT_ABI_VERSION ||
        ops->descriptor_size != sizeof(*ops) || !ops->name ||
        !ops->is_active || !ops->generation ||
        !ops->device_capabilities || !ops->fifo_capabilities ||
        !ops->fifo_register_count || !ops->fifo_register_read ||
        !ops->fifo_register_write || !ops->emit || !ops->submit ||
        !ops->wait_fence || !ops->vram_allocate || !ops->vram_free ||
        !ops->vram_pointer || !ops->display_info) return false;
    if (g_transport && g_transport != ops) return false;
    g_transport = ops;
    return true;
}

bool svga_transport_unregister(const bk_svga_transport_ops_t *ops) {
    if (!ops || g_transport != ops) return false;
    g_transport = NULL;
    return true;
}

const bk_svga_transport_ops_t *svga_transport_get(void) {
    return g_transport;
}
