/* SPDX-License-Identifier: MIT */
#include "../include/ts2d_preboard.h"

static int surface_valid(const ts2d_surface_t *surface) {
    return surface != NULL && surface->pixels != NULL && surface->width != 0u &&
           surface->height != 0u &&
           surface->stride_bytes >= (uint32_t)surface->width * 2u &&
           (surface->stride_bytes & 1u) == 0u;
}

static uint16_t *pixel_at(ts2d_surface_t *surface, int x, int y) {
    uint8_t *row = (uint8_t *)(void *)surface->pixels +
                   (size_t)y * surface->stride_bytes;
    return (uint16_t *)(void *)(row + (size_t)x * 2u);
}

static const uint16_t *const_pixel_at(const ts2d_surface_t *surface, int x, int y) {
    const uint8_t *row = (const uint8_t *)(const void *)surface->pixels +
                         (size_t)y * surface->stride_bytes;
    return (const uint16_t *)(const void *)(row + (size_t)x * 2u);
}

static uint8_t expand5(unsigned value) { return (uint8_t)((value << 3) | (value >> 2)); }
static uint8_t expand6(unsigned value) { return (uint8_t)((value << 2) | (value >> 4)); }

static uint8_t blend_channel(uint8_t source, uint8_t destination, uint8_t alpha) {
    uint32_t value;
    if (alpha == 0u)
        return destination;
    if (alpha == 255u)
        return source;
    value = (uint32_t)source * alpha +
            (uint32_t)destination * (uint32_t)(255u - alpha);
    value += 128u;
    return (uint8_t)((value + (value >> 8)) >> 8);
}

static uint16_t blend_rgb565(uint16_t source, uint16_t destination, uint8_t alpha) {
    uint8_t sr = expand5((source >> 11) & 31u);
    uint8_t sg = expand6((source >> 5) & 63u);
    uint8_t sb = expand5(source & 31u);
    uint8_t dr = expand5((destination >> 11) & 31u);
    uint8_t dg = expand6((destination >> 5) & 63u);
    uint8_t db = expand5(destination & 31u);
    uint8_t r = blend_channel(sr, dr, alpha);
    uint8_t g = blend_channel(sg, dg, alpha);
    uint8_t b = blend_channel(sb, db, alpha);
    return (uint16_t)(((uint16_t)(r >> 3) << 11) |
                      ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3));
}

static uint16_t blend_argb4444(uint16_t s,uint16_t d,uint8_t alpha) {
    uint32_t a=(uint32_t)(s>>12)*17u*alpha+128u;
    a=(a+(a>>8))>>8;
    unsigned r=blend_channel(((s>>8)&15u)*17u,expand5(d>>11),(uint8_t)a);
    unsigned g=blend_channel(((s>>4)&15u)*17u,expand6((d>>5)&63u),(uint8_t)a);
    unsigned b=blend_channel((s&15u)*17u,expand5(d&31u),(uint8_t)a);
    return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
}

static int clipped_bounds(const ts2d_surface_t *dst, int x, int y,
                          unsigned width, unsigned height, int *x0, int *y0,
                          int *x1, int *y1) {
    int64_t right;
    int64_t bottom;
    if (!surface_valid(dst) || width == 0u || height == 0u)
        return TS2D_ERR_ARGUMENT;
    right = (int64_t)x + width;
    bottom = (int64_t)y + height;
    *x0 = x < 0 ? 0 : x;
    *y0 = y < 0 ? 0 : y;
    *x1 = right > dst->width ? dst->width : (int)right;
    *y1 = bottom > dst->height ? dst->height : (int)bottom;
    return TS2D_OK;
}

int renderer_sw_fill(ts2d_surface_t *dst, int x, int y, unsigned width,
                     unsigned height, uint16_t color) {
    int x0, y0, x1, y1, px, py;
    int result = clipped_bounds(dst, x, y, width, height, &x0, &y0, &x1, &y1);
    if (result != TS2D_OK)
        return result;
    for (py = y0; py < y1; ++py)
        for (px = x0; px < x1; ++px)
            *pixel_at(dst, px, py) = color;
    return TS2D_OK;
}

static int render_blit(ts2d_surface_t *dst, const ts2d_surface_t *src,
                       int src_x, int src_y, int dst_x, int dst_y,
                       unsigned width, unsigned height, int operation,
                       uint16_t color_key, uint8_t alpha, unsigned flags) {
    int x0, y0, x1, y1, px, py;
    int result = clipped_bounds(dst, dst_x, dst_y, width, height,
                                &x0, &y0, &x1, &y1);
    if (result != TS2D_OK || !surface_valid(src))
        return TS2D_ERR_ARGUMENT;
    for (py = y0; py < y1; ++py) {
        unsigned local_y = (unsigned)(py - dst_y);
        int sy = src_y + (int)((flags & TS2D_FLAG_FLIP_Y) ?
                              height - 1u - local_y : local_y);
        for (px = x0; px < x1; ++px) {
            unsigned local_x = (unsigned)(px - dst_x);
            int sx = src_x + (int)((flags & TS2D_FLAG_FLIP_X) ?
                                  width - 1u - local_x : local_x);
            uint16_t source;
            uint16_t *destination;
            if (sx < 0 || sy < 0 || sx >= src->width || sy >= src->height)
                return TS2D_ERR_RANGE;
            source = *const_pixel_at(src, sx, sy);
            destination = pixel_at(dst, px, py);
            if (operation == TS2D_OP_BLIT_COLORKEY && source == color_key)
                continue;
            *destination = operation == TS2D_OP_BLIT_ALPHA_PIXEL ?
                           blend_argb4444(source,*destination,alpha) :
                           operation == TS2D_OP_BLIT_ALPHA_CONST ?
                           blend_rgb565(source, *destination, alpha) : source;
        }
    }
    return TS2D_OK;
}

int renderer_sw_blit(ts2d_surface_t *dst, const ts2d_surface_t *src,
                     int src_x, int src_y, int dst_x, int dst_y,
                     unsigned width, unsigned height) {
    return render_blit(dst, src, src_x, src_y, dst_x, dst_y, width, height,
                       TS2D_OP_BLIT_OPAQUE, 0u, 0u, 0u);
}

int renderer_sw_colorkey(ts2d_surface_t *dst, const ts2d_surface_t *src,
                         int src_x, int src_y, int dst_x, int dst_y,
                         unsigned width, unsigned height, uint16_t color_key) {
    return render_blit(dst, src, src_x, src_y, dst_x, dst_y, width, height,
                       TS2D_OP_BLIT_COLORKEY, color_key, 0u, 0u);
}

int renderer_sw_alpha(ts2d_surface_t *dst, const ts2d_surface_t *src,
                      int src_x, int src_y, int dst_x, int dst_y,
                      unsigned width, unsigned height, uint8_t alpha) {
    return render_blit(dst, src, src_x, src_y, dst_x, dst_y, width, height,
                       TS2D_OP_BLIT_ALPHA_CONST, 0u, alpha, 0u);
}

int renderer_sw_execute(ts2d_surface_t *dst, const ts2d_surface_t *src,
                        const ts2d_command_t *commands, size_t command_count) {
    size_t index;
    if (!surface_valid(dst) || commands == NULL)
        return TS2D_ERR_ARGUMENT;
    for (index = 0; index < command_count; ++index) {
        const ts2d_command_t *command = &commands[index];
        int result = TS2D_OK;
        if (command->opcode == TS2D_OP_FRAME_END)
            break;
        if (command->opcode == TS2D_OP_NOP)
            continue;
        if (command->width == 0u || command->height == 0u ||
            (command->flags & TS2D_FLAG_CLIP_TO_VIEWPORT) == 0u)
            return TS2D_ERR_ARGUMENT;
        if (command->opcode == TS2D_OP_FILL_RECT ||
            command->opcode == TS2D_OP_CLEAR_FRAME) {
            result = renderer_sw_fill(dst, command->dst_x, command->dst_y,
                                      command->width, command->height,
                                      (uint16_t)command->parameter);
        } else if (src != NULL &&
                   (command->opcode == TS2D_OP_BLIT_OPAQUE ||
                    command->opcode == TS2D_OP_BLIT_COLORKEY ||
                    command->opcode == TS2D_OP_BLIT_ALPHA_CONST ||
                    command->opcode == TS2D_OP_BLIT_ALPHA_PIXEL)) {
            if(command->opcode==TS2D_OP_BLIT_ALPHA_PIXEL &&
               (command->src_format!=TS2D_FMT_ARGB4444 || !(command->flags&TS2D_FLAG_SRC_ARGB4444)))
                return TS2D_ERR_ARGUMENT;
            result = render_blit(dst, src, command->src_x, command->src_y,
                                 command->dst_x, command->dst_y, command->width,
                                 command->height, command->opcode,
                                 (uint16_t)command->parameter,
                                 command->global_alpha, command->flags);
        } else {
            return TS2D_ERR_ARGUMENT;
        }
        if (result != TS2D_OK)
            return result;
    }
    return TS2D_OK;
}
