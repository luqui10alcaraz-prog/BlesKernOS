#ifndef BK_SVGA3D_PROTOCOL_H
#define BK_SVGA3D_PROTOCOL_H

#include "types.h"

/*
 * Minimal VMware SVGA3D fixed-function protocol subset.
 * The public interface definitions are derived from VMware's SVGA headers,
 * dual-licensed GPL-2.0 OR MIT. BlesKernOS uses the MIT grant.
 */

#define SVGA_CAP_3D 0x00004000U
#define SVGA_FIFO_3D_HWVERSION 7U
#define SVGA_FIFO_3D_HWVERSION_REVISED 19U
#define SVGA_FIFO_GUEST_3D_HWVERSION 288U
#define SVGA_FIFO_CAP_3D_HWVERSION_REVISED (1U << 8)
#define SVGA3D_MAKE_HWVERSION(major, minor) \
    (((uint32_t)(major) << 16) | (uint32_t)(minor))
#define SVGA3D_HWVERSION_WS65_B1 SVGA3D_MAKE_HWVERSION(2U, 0U)
#define SVGA3D_GUEST_HWVERSION SVGA3D_HWVERSION_WS65_B1

#define SVGA_3D_CMD_SURFACE_DEFINE      1040U
#define SVGA_3D_CMD_SURFACE_DESTROY     1041U
#define SVGA_3D_CMD_SURFACE_STRETCHBLT  1043U
#define SVGA_3D_CMD_SURFACE_DMA         1044U
#define SVGA_3D_CMD_CONTEXT_DEFINE      1045U
#define SVGA_3D_CMD_CONTEXT_DESTROY     1046U
#define SVGA_3D_CMD_SETRENDERSTATE      1049U
#define SVGA_3D_CMD_SETRENDERTARGET     1050U
#define SVGA_3D_CMD_SETTEXTURESTATE     1051U
#define SVGA_3D_CMD_SETVIEWPORT         1055U
#define SVGA_3D_CMD_CLEAR               1057U
#define SVGA_3D_CMD_PRESENT             1058U
#define SVGA_3D_CMD_DRAW_PRIMITIVES     1063U
#define SVGA_3D_CMD_SETSCISSORRECT      1064U
#define SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN 1069U

#define SVGA3D_INVALID_ID 0xFFFFFFFFU
#define SVGA_GMR_FRAMEBUFFER 0xFFFFFFFEU
#define SVGA3D_X8R8G8B8 1U
#define SVGA3D_A8R8G8B8 2U
#define SVGA3D_Z_D16     8U
#define SVGA3D_BUFFER    37U

#define SVGA3D_SURFACE_HINT_DYNAMIC      (1U << 2)
#define SVGA3D_SURFACE_HINT_VERTEXBUFFER (1U << 4)
#define SVGA3D_SURFACE_HINT_TEXTURE      (1U << 5)
#define SVGA3D_SURFACE_HINT_RENDERTARGET (1U << 6)
#define SVGA3D_SURFACE_HINT_DEPTHSTENCIL (1U << 7)

#define SVGA3D_WRITE_HOST_VRAM 1U
#define SVGA3D_READ_HOST_VRAM  2U
#define SVGA3D_STRETCH_BLT_POINT  0U
#define SVGA3D_STRETCH_BLT_LINEAR 1U

#define SVGA3D_CLEAR_COLOR 0x1U
#define SVGA3D_CLEAR_DEPTH 0x2U
#define SVGA3D_RT_DEPTH    0U
#define SVGA3D_RT_COLOR0   2U

#define SVGA3D_RS_ZENABLE        1U
#define SVGA3D_RS_ZWRITEENABLE   2U
#define SVGA3D_RS_BLENDENABLE    5U
#define SVGA3D_RS_LIGHTINGENABLE 9U
#define SVGA3D_RS_SRCBLEND       32U
#define SVGA3D_RS_DSTBLEND       33U
#define SVGA3D_RS_BLENDEQUATION  34U
#define SVGA3D_RS_CULLMODE       35U
#define SVGA3D_RS_ZFUNC          36U
#define SVGA3D_RS_COLORWRITEENABLE 47U
#define SVGA3D_RS_SCISSORTESTENABLE 55U
#define SVGA3D_COLORWRITE_ALL 0xFU

#define SVGA3D_BLENDOP_ONE         2U
#define SVGA3D_BLENDOP_SRCALPHA    5U
#define SVGA3D_BLENDOP_INVSRCALPHA 6U
#define SVGA3D_BLENDEQ_ADD          1U
#define SVGA3D_FACE_NONE            1U
#define SVGA3D_CMP_LESSEQUAL        4U
#define SVGA3D_CMP_GREATEREQUAL     7U

#define SVGA3D_TS_BIND_TEXTURE 1U
#define SVGA3D_TS_COLOROP      2U
#define SVGA3D_TS_COLORARG1    3U
#define SVGA3D_TS_COLORARG2    4U
#define SVGA3D_TS_ALPHAOP      5U
#define SVGA3D_TS_ALPHAARG1    6U
#define SVGA3D_TS_ALPHAARG2    7U
#define SVGA3D_TS_ADDRESSU     8U
#define SVGA3D_TS_ADDRESSV     9U
#define SVGA3D_TS_MIPFILTER   10U
#define SVGA3D_TS_MAGFILTER   11U
#define SVGA3D_TS_MINFILTER   12U

#define SVGA3D_TC_DISABLE    1U
#define SVGA3D_TC_SELECTARG1 2U
#define SVGA3D_TC_MODULATE   4U
#define SVGA3D_TA_DIFFUSE    3U
#define SVGA3D_TA_TEXTURE    4U
#define SVGA3D_TEX_ADDRESS_WRAP  1U
#define SVGA3D_TEX_ADDRESS_CLAMP 3U
#define SVGA3D_TEX_FILTER_NONE    0U
#define SVGA3D_TEX_FILTER_NEAREST 1U
#define SVGA3D_TEX_FILTER_LINEAR  2U

#define SVGA3D_DECLTYPE_FLOAT2   1U
#define SVGA3D_DECLTYPE_FLOAT4   3U
#define SVGA3D_DECLTYPE_D3DCOLOR 4U
#define SVGA3D_DECLMETHOD_DEFAULT 0U
#define SVGA3D_DECLUSAGE_POSITIONT 9U
#define SVGA3D_DECLUSAGE_COLOR    10U
#define SVGA3D_DECLUSAGE_TEXCOORD 5U
#define SVGA3D_PRIMITIVE_TRIANGLELIST 1U

#pragma pack(push, 1)
typedef struct { uint32_t id, size; } bk_svga3d_header_t;
typedef struct { uint32_t width, height, depth; } bk_svga3d_size_t;
typedef struct { uint32_t sid, face, mipmap; } bk_svga3d_image_id_t;
typedef struct { uint32_t num_mip_levels; } bk_svga3d_surface_face_t;
typedef struct {
    uint32_t sid;
    uint32_t surface_flags_low;
    uint32_t format;
    bk_svga3d_surface_face_t face[6];
} bk_svga3d_define_surface_t;
typedef struct { uint32_t sid; } bk_svga3d_destroy_surface_t;
typedef struct { uint32_t cid; } bk_svga3d_context_t;
typedef struct { uint32_t gmr_id, offset; } bk_svga_guest_ptr_t;
typedef struct { bk_svga_guest_ptr_t ptr; uint32_t pitch; } bk_svga_guest_image_t;
typedef struct {
    bk_svga_guest_image_t guest;
    bk_svga3d_image_id_t host;
    uint32_t transfer;
} bk_svga3d_surface_dma_t;
typedef struct {
    uint32_t x, y, z, w, h, d;
    uint32_t srcx, srcy, srcz;
} bk_svga3d_copy_box_t;
typedef struct { uint32_t x, y, z, w, h, d; } bk_svga3d_box_t;
typedef struct {
    bk_svga3d_image_id_t src, dest;
    bk_svga3d_box_t box_src, box_dest;
    uint32_t mode;
} bk_svga3d_stretchblt_t;
typedef struct { uint32_t cid; } bk_svga3d_set_render_state_t;
typedef struct { uint32_t state, value; } bk_svga3d_render_state_t;
typedef struct {
    uint32_t cid, type;
    bk_svga3d_image_id_t target;
} bk_svga3d_set_render_target_t;
typedef struct { uint32_t cid; } bk_svga3d_set_texture_state_t;
typedef struct { uint32_t stage, name, value; } bk_svga3d_texture_state_t;
typedef struct { uint32_t x, y, w, h; } bk_svga3d_rect_t;
typedef struct { uint32_t cid; bk_svga3d_rect_t rect; }
    bk_svga3d_set_rect_t;
typedef struct {
    uint32_t cid, clear_flags, color;
    float depth;
    uint32_t stencil;
} bk_svga3d_clear_t;
typedef struct { uint32_t sid; } bk_svga3d_present_t;
typedef struct { uint32_t x, y, w, h, srcx, srcy; } bk_svga3d_copy_rect_t;
typedef struct { uint32_t surface_id, offset, stride; } bk_svga3d_array_t;
typedef struct { uint32_t first, last; } bk_svga3d_range_hint_t;
typedef struct { uint32_t type, method, usage, usage_index; }
    bk_svga3d_vertex_identity_t;
typedef struct {
    bk_svga3d_vertex_identity_t identity;
    bk_svga3d_array_t array;
    bk_svga3d_range_hint_t range_hint;
} bk_svga3d_vertex_decl_t;
typedef struct {
    uint32_t prim_type, primitive_count;
    bk_svga3d_array_t index_array;
    uint32_t index_width;
    int32_t index_bias;
} bk_svga3d_primitive_range_t;
typedef struct { uint32_t cid, num_vertex_decls, num_ranges; }
    bk_svga3d_draw_primitives_t;
typedef struct { int32_t left, top, right, bottom; } bk_svga_signed_rect_t;
typedef struct {
    bk_svga3d_image_id_t src_image;
    bk_svga_signed_rect_t src_rect;
    uint32_t dest_screen_id;
    bk_svga_signed_rect_t dest_rect;
} bk_svga3d_blit_to_screen_t;
#pragma pack(pop)

#define BK_SVGA3D_SIZE_ASSERT(name_, expression_) \
    typedef char bk_svga3d_size_assert_##name_[(expression_) ? 1 : -1]
BK_SVGA3D_SIZE_ASSERT(header, sizeof(bk_svga3d_header_t) == 8U);
BK_SVGA3D_SIZE_ASSERT(define_surface,
                      sizeof(bk_svga3d_define_surface_t) == 36U);
BK_SVGA3D_SIZE_ASSERT(surface_dma, sizeof(bk_svga3d_surface_dma_t) == 28U);
BK_SVGA3D_SIZE_ASSERT(copy_box, sizeof(bk_svga3d_copy_box_t) == 36U);
BK_SVGA3D_SIZE_ASSERT(render_target,
                      sizeof(bk_svga3d_set_render_target_t) == 20U);
BK_SVGA3D_SIZE_ASSERT(clear, sizeof(bk_svga3d_clear_t) == 20U);
BK_SVGA3D_SIZE_ASSERT(copy_rect, sizeof(bk_svga3d_copy_rect_t) == 24U);
BK_SVGA3D_SIZE_ASSERT(vertex_decl, sizeof(bk_svga3d_vertex_decl_t) == 36U);
BK_SVGA3D_SIZE_ASSERT(primitive_range,
                      sizeof(bk_svga3d_primitive_range_t) == 28U);
BK_SVGA3D_SIZE_ASSERT(draw_primitives,
                      sizeof(bk_svga3d_draw_primitives_t) == 12U);
#undef BK_SVGA3D_SIZE_ASSERT

#endif
