/* SPDX-License-Identifier: MIT */
#include "usurper_cpu_renderer.h"
#include "usurper_triangle_command.h"
#include "usurper_scene.h"
#include "usurper_render_memory.h"
#include "usurper_research_icons.h"
#include "usurper_supply_glyphs_generated.h"
#include "usurper_interceptor_sprites_generated.h"

typedef struct {
    uint32_t base, bytes;
    uint16_t width, height, stride;
} SourceRegion;

#define EXTRA_SOURCE_COUNT 4u
static SourceRegion extra_sources[EXTRA_SOURCE_COUNT];
static unsigned extra_source_count;
static uint32_t packed_source_base,packed_source_bytes;

static int region_init(SourceRegion *r, uint32_t base, unsigned width,
                       unsigned height, unsigned stride) {
    uint64_t bytes;
    if (!r || !width || !height || width > 65535u || height > 65535u ||
        stride > 65535u || stride < width * 2u || (base & 1u) || (stride & 1u))
        return TS2D_ERR_ARGUMENT;
    bytes = (uint64_t)height * stride;
    if ((uint64_t)base + bytes > USURPER_DDR_LIMIT) return TS2D_ERR_RANGE;
    *r = (SourceRegion){base, (uint32_t)bytes, (uint16_t)width,
                       (uint16_t)height, (uint16_t)stride};
    return TS2D_OK;
}

void usurper_cpu_clear_sources(void) { extra_source_count = 0u; }
void usurper_cpu_clear_packed_source(void) { packed_source_base=packed_source_bytes=0; }
int usurper_cpu_register_packed_source(uint32_t base,uint32_t bytes) {
    if(!bytes || (base&1u) || (bytes&1u))return TS2D_ERR_ARGUMENT;
    if(base<USURPER_RLE_WORKSPACE_BASE || (uint64_t)base+bytes>0x0e800000u)
        return TS2D_ERR_RANGE;
    packed_source_base=base;packed_source_bytes=bytes;return TS2D_OK;
}

int usurper_cpu_register_source(uint32_t base, unsigned width, unsigned height,
                               unsigned stride_bytes) {
    SourceRegion region;
    int result = region_init(&region, base, width, height, stride_bytes);
    if (result) return result;
    if (base < USURPER_EXTRA_SOURCE_BASE ||
        (uint64_t)base + region.bytes > USURPER_EXTRA_SOURCE_LIMIT)
        return TS2D_ERR_RANGE;
    for (unsigned i = 0; i < extra_source_count; ++i) {
        const SourceRegion *r = &extra_sources[i];
        if (r->base == region.base && r->width == region.width &&
            r->height == region.height && r->stride == region.stride) return TS2D_OK;
        if ((uint64_t)base < (uint64_t)r->base + r->bytes &&
            (uint64_t)r->base < (uint64_t)base + region.bytes) return TS2D_ERR_RANGE;
    }
    if (extra_source_count == EXTRA_SOURCE_COUNT) return TS2D_ERR_CAPACITY;
    extra_sources[extra_source_count++] = region;
    return TS2D_OK;
}

static int region_surface(const SourceRegion *r, const ts2d_command_t *c,
                          int allow_rebase, ts2d_surface_t *surface) {
    uint32_t offset;
    if (c->src_base_addr < r->base ||
        (uint64_t)c->src_base_addr >= (uint64_t)r->base + r->bytes)
        return TS2D_ERR_RANGE;
    offset = c->src_base_addr - r->base;
    if ((!allow_rebase && offset) || offset % r->stride ||
        c->src_stride_bytes != r->stride) return TS2D_ERR_RANGE;
    *surface = (ts2d_surface_t){(uint16_t *)(uintptr_t)c->src_base_addr,
        r->width, (uint16_t)(r->height - offset / r->stride), r->stride};
    return TS2D_OK;
}

static int source_surface(unsigned atlas_height, const ts2d_command_t *c,
                          ts2d_surface_t *surface) {
    SourceRegion region;
    uint64_t bg = (uint64_t)USURPER_ATLAS_BASE +
                  (uint64_t)atlas_height * USURPER_ATLAS_STRIDE;
    if (!atlas_height || atlas_height > 65535u ||
        bg + USURPER_BG_HEIGHT * USURPER_BG_STRIDE > USURPER_EXTRA_SOURCE_BASE)
        return TS2D_ERR_RANGE;
    if (c->src_base_addr >= USURPER_ATLAS_BASE && c->src_base_addr < bg) {
        if (region_init(&region, USURPER_ATLAS_BASE, USURPER_ATLAS_WIDTH,
                        atlas_height, USURPER_ATLAS_STRIDE)) return TS2D_ERR_RANGE;
        return region_surface(&region, c, 1, surface);
    }
    if (c->src_base_addr == bg) {
        if (region_init(&region, (uint32_t)bg, USURPER_BG_WIDTH,
                        USURPER_BG_HEIGHT, USURPER_BG_STRIDE)) return TS2D_ERR_RANGE;
        return region_surface(&region, c, 0, surface);
    }
    for (unsigned page = 0; page < 2u; ++page) {
        uint32_t base = USURPER_SOLID_BASE + page * USURPER_SOLID_BYTES;
        if (c->src_base_addr != base) continue;
        if (region_init(&region, base, USURPER_GPU_WIDTH, USURPER_GPU_HEIGHT,
                        USURPER_GPU_STRIDE)) return TS2D_ERR_RANGE;
        return region_surface(&region, c, 0, surface);
    }
    {
        uint32_t tile_bytes = USURPER_DYNAMIC_PALETTE_TILE_HEIGHT * USURPER_GPU_STRIDE;
        uint32_t palette_bytes = 2u * USURPER_DYNAMIC_PALETTE_ROWS * tile_bytes;
        if (c->src_base_addr >= USURPER_DYNAMIC_PALETTE_BASE &&
            c->src_base_addr < USURPER_DYNAMIC_PALETTE_BASE + palette_bytes) {
            if ((c->src_base_addr - USURPER_DYNAMIC_PALETTE_BASE) % tile_bytes)
                return TS2D_ERR_RANGE;
            if (region_init(&region, c->src_base_addr, USURPER_GPU_WIDTH,
                            USURPER_DYNAMIC_PALETTE_TILE_HEIGHT, USURPER_GPU_STRIDE))
                return TS2D_ERR_RANGE;
            return region_surface(&region, c, 0, surface);
        }
    }
    for (unsigned bank = 0; bank < 2u; ++bank)
        for (unsigned slot = 0; slot < USURPER_CAPTIVE_SLOT_COUNT; ++slot) {
            uint32_t base = USURPER_CAPTIVE_SCRATCH_BASE +
                bank * USURPER_CAPTIVE_BANK_BYTES + slot * USURPER_CAPTIVE_SLOT_BYTES;
            if (c->src_base_addr != base) continue;
            if (c->src_x || c->src_y || c->width > 96u || c->height > 64u ||
                c->src_stride_bytes != c->width * 2u) return TS2D_ERR_RANGE;
            if (region_init(&region, base, c->width, c->height, c->src_stride_bytes) ||
                region.bytes > USURPER_CAPTIVE_SLOT_BYTES) return TS2D_ERR_RANGE;
            return region_surface(&region, c, 0, surface);
        }
    if(c->src_base_addr>=US_RESEARCH_ICON_BASE && c->src_base_addr<US_RESEARCH_ICON_BASE+2u*US_RESEARCH_ICON_BANK_BYTES) {
        if((c->src_base_addr-US_RESEARCH_ICON_BASE)%US_RESEARCH_ICON_SLOT_BYTES)return TS2D_ERR_RANGE;
        if(region_init(&region,c->src_base_addr,128,128,256))return TS2D_ERR_RANGE;
        return region_surface(&region,c,0,surface);
    }
    if (c->src_base_addr == US_SUPPLY_GLYPH_BASE) {
        if (region_init(&region, US_SUPPLY_GLYPH_BASE, US_SUPPLY_GLYPH_WIDTH,
                        US_SUPPLY_GLYPH_HEIGHT, US_SUPPLY_GLYPH_WIDTH * 2u))
            return TS2D_ERR_RANGE;
        return region_surface(&region, c, 0, surface);
    }
    if (c->src_base_addr == US_INTERCEPTOR_SPRITE_BASE) {
        if (region_init(&region, US_INTERCEPTOR_SPRITE_BASE, US_INTERCEPTOR_SPRITE_WIDTH,
                        US_INTERCEPTOR_SPRITE_HEIGHT, US_INTERCEPTOR_SPRITE_WIDTH * 2u))
            return TS2D_ERR_RANGE;
        return region_surface(&region, c, 0, surface);
    }
    for (unsigned i = 0; i < extra_source_count; ++i)
        if (region_surface(&extra_sources[i], c, 1, surface) == TS2D_OK) return TS2D_OK;
    if(packed_source_bytes && c->src_base_addr>=packed_source_base &&
       (uint64_t)c->src_base_addr<(uint64_t)packed_source_base+packed_source_bytes) {
        if(c->src_x || c->src_y || c->width>320u || c->height>320u ||
           c->src_stride_bytes!=(uint32_t)c->width*2u)return TS2D_ERR_RANGE;
        if(region_init(&region,c->src_base_addr,c->width,c->height,c->src_stride_bytes) ||
           (uint64_t)c->src_base_addr+region.bytes>(uint64_t)packed_source_base+packed_source_bytes)
            return TS2D_ERR_RANGE;
        return region_surface(&region,c,0,surface);
    }
    return TS2D_ERR_RANGE;
}

int usurper_cpu_resolve(uint32_t framebuffer, unsigned atlas_height,
                        const ts2d_command_t *c,
                        ts2d_surface_t *destination, ts2d_surface_t *source) {
    int32_t x0, y0, x1, y1, sx0, sy0, sx1, sy1;
    int blit, result;
    if (!c || !destination || !source) return TS2D_ERR_ARGUMENT;
    *source = (ts2d_surface_t){0};
    *destination = (ts2d_surface_t){0};
    if (framebuffer != USURPER_FB_A && framebuffer != USURPER_FB_B)
        return TS2D_ERR_RANGE;
    if (c->opcode == TS2D_OP_NOP || c->opcode == TS2D_OP_FRAME_END) return TS2D_OK;
    blit = c->opcode == TS2D_OP_BLIT_OPAQUE || c->opcode == TS2D_OP_BLIT_COLORKEY ||
           c->opcode == TS2D_OP_BLIT_ALPHA_CONST || c->opcode == TS2D_OP_BLIT_ALPHA_PIXEL || c->opcode==7;
    if ((!blit && c->opcode != TS2D_OP_FILL_RECT && c->opcode != TS2D_OP_CLEAR_FRAME) ||
        !c->width || !c->height || !(c->flags & TS2D_FLAG_CLIP_TO_VIEWPORT) ||
        (c->flags & ~(TS2D_FLAG_CLIP_TO_VIEWPORT | TS2D_FLAG_FLIP_X | TS2D_FLAG_FLIP_Y |
                      TS2D_FLAG_SRC_ARGB4444 | TS2D_FLAG_PRESERVE_DST)))
        return TS2D_ERR_ARGUMENT;
    if (c->dst_base_addr != framebuffer || c->dst_stride_bytes != USURPER_GPU_STRIDE)
        return TS2D_ERR_RANGE;
    *destination = (ts2d_surface_t){(uint16_t *)(uintptr_t)framebuffer,
        USURPER_GPU_WIDTH, USURPER_GPU_HEIGHT, USURPER_GPU_STRIDE};
    if(c->opcode==7)return us_triangle_command_valid(c) ? TS2D_OK : TS2D_ERR_RANGE;
    if (!blit) return TS2D_OK;
    if ((c->opcode == TS2D_OP_BLIT_ALPHA_PIXEL &&
         (c->src_format != TS2D_FMT_ARGB4444 || !(c->flags & TS2D_FLAG_SRC_ARGB4444))) ||
        (c->opcode != TS2D_OP_BLIT_ALPHA_PIXEL &&
         (c->src_format != TS2D_FMT_RGB565 || (c->flags & TS2D_FLAG_SRC_ARGB4444))))
        return TS2D_ERR_ARGUMENT;
    if ((c->src_base_addr & 1u) || (c->src_stride_bytes & 1u) ||
        c->src_stride_bytes < (uint32_t)c->width * 2u) return TS2D_ERR_RANGE;
    result = source_surface(atlas_height, c, source);
    if (result) return result;
    x0 = c->dst_x < 0 ? 0 : c->dst_x;
    y0 = c->dst_y < 0 ? 0 : c->dst_y;
    x1 = (int32_t)c->dst_x + c->width;
    y1 = (int32_t)c->dst_y + c->height;
    if (x1 > (int32_t)USURPER_GPU_WIDTH) x1 = USURPER_GPU_WIDTH;
    if (y1 > (int32_t)USURPER_GPU_HEIGHT) y1 = USURPER_GPU_HEIGHT;
    if (x0 >= x1 || y0 >= y1) return TS2D_OK;
    sx0 = c->src_x + ((c->flags & TS2D_FLAG_FLIP_X) ?
        c->width - (x1 - c->dst_x) : x0 - c->dst_x);
    sy0 = c->src_y + ((c->flags & TS2D_FLAG_FLIP_Y) ?
        c->height - (y1 - c->dst_y) : y0 - c->dst_y);
    sx1 = sx0 + x1 - x0;
    sy1 = sy0 + y1 - y0;
    if (sx0 < 0 || sy0 < 0 || sx1 > source->width || sy1 > source->height)
        return TS2D_ERR_RANGE;
    /* The surface is already inside DDR; retain a wide final extent check so
     * future metadata changes cannot wrap source arithmetic on the RV32. */
    if ((uint64_t)c->src_base_addr + (uint64_t)(sy1 - 1) * source->stride_bytes +
        (uint64_t)sx1 * 2u > USURPER_DDR_LIMIT) return TS2D_ERR_RANGE;
    return TS2D_OK;
}

int usurper_cpu_execute(uint32_t framebuffer, unsigned atlas_height,
                        const ts2d_command_t *commands, size_t count) {
    ts2d_surface_t destination, source;
    if (!commands) return TS2D_ERR_ARGUMENT;
    for (size_t i = 0; i < count; ++i) {
        int result = usurper_cpu_resolve(framebuffer, atlas_height, &commands[i],
                                         &destination, &source);
        if (result) return result;
        if (commands[i].opcode == TS2D_OP_FRAME_END) break;
    }
    for (size_t i = 0; i < count; ++i) {
        int result;
        if (commands[i].opcode == TS2D_OP_FRAME_END) break;
        if (commands[i].opcode == TS2D_OP_NOP) continue;
        result = usurper_cpu_resolve(framebuffer, atlas_height, &commands[i],
                                     &destination, &source);
        if (result) return result;
        if(commands[i].opcode==7){
            result=us_triangle_command_execute(&destination,&commands[i]);
            if(result)return result;
            continue;
        }
        result = renderer_sw_execute(&destination, source.pixels ? &source : NULL,
                                      &commands[i], 1u);
        if (result) return result;
    }
    return TS2D_OK;
}
