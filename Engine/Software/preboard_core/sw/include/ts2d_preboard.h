/* SPDX-License-Identifier: MIT */
#ifndef TS2D_PREBOARD_H
#define TS2D_PREBOARD_H

#include <stddef.h>
#include <stdint.h>

#include "../../../sw/include/ts2d_command_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PB1 defines relative offsets only.  The platform supplies the MMIO base. */
enum ts2d_register_offset {
    TS2D_REG_IP_ID = 0x000,
    TS2D_REG_CONTROL = 0x008,
    TS2D_REG_STATUS = 0x00c,
    TS2D_REG_IRQ_STATUS = 0x010,
    TS2D_REG_ERROR_CODE = 0x014,
    TS2D_REG_SUBMIT = 0x01c,
    TS2D_REG_PERF_RENDER_CYCLES_LO = 0x054,
    TS2D_REG_PERF_RENDER_CYCLES_HI = 0x058,
    TS2D_REG_PERF_CMD_COUNT = 0x05c,
    TS2D_REG_PERF_PIXEL_COUNT = 0x060,
    TS2D_REG_PERF_DDR_RD_BYTES = 0x064,
    TS2D_REG_PERF_DDR_WR_BYTES = 0x068,
    TS2D_REG_PERF_AXI_STALL_CYCLES = 0x06c,
};

enum ts2d_result {
    TS2D_OK = 0,
    TS2D_ERR_ARGUMENT = -1,
    TS2D_ERR_CAPACITY = -2,
    TS2D_ERR_RANGE = -3,
    TS2D_ERR_BUSY = -4,
    TS2D_ERR_TIMEOUT = -5,
    TS2D_ERR_HW = -6,
};

typedef enum ts2d_mode {
    TS2D_MODE_CPU_SW = 0,
    TS2D_MODE_FPGA_IMMEDIATE = 1,
    TS2D_MODE_FPGA_TILE = 2,
} ts2d_mode_t;

typedef struct ts2d_surface {
    uint16_t *pixels;
    uint16_t width;
    uint16_t height;
    uint16_t stride_bytes;
} ts2d_surface_t;

typedef void (*ts2d_cache_fn)(const void *address, size_t bytes);
typedef void (*ts2d_poll_fn)(void *opaque);

typedef struct ts2d_config {
    volatile uint32_t *registers;
    volatile uint32_t *command_bank[2];
    uint16_t command_capacity;
    ts2d_cache_fn cache_flush;
    ts2d_poll_fn poll;
    void *poll_opaque;
} ts2d_config_t;

typedef struct ts2d_frame {
    ts2d_command_t *commands;
    uint16_t capacity;
    uint16_t count;
    uint16_t frame_id;
    uint8_t bank;
    uint8_t mode;
    uint32_t framebuffer_addr;
    uint32_t source_addr;
    uint16_t width;
    uint16_t height;
    uint16_t framebuffer_stride;
    uint16_t source_stride;
} ts2d_frame_t;

typedef struct ts2d_perf {
    uint64_t render_cycles;
    uint32_t command_count;
    uint32_t pixel_count;
    uint32_t ddr_read_bytes;
    uint32_t ddr_write_bytes;
    uint32_t axi_stall_cycles;
} ts2d_perf_t;

int ts2d_init(const ts2d_config_t *config);
int ts2d_begin_frame(ts2d_frame_t *frame, ts2d_mode_t mode);
int ts2d_fill_rect(ts2d_frame_t *frame, int16_t x, int16_t y,
                   uint16_t width, uint16_t height, uint16_t color);
int ts2d_blit(ts2d_frame_t *frame, int16_t src_x, int16_t src_y,
              int16_t dst_x, int16_t dst_y, uint16_t width, uint16_t height);
int ts2d_blit_colorkey(ts2d_frame_t *frame, int16_t src_x, int16_t src_y,
                       int16_t dst_x, int16_t dst_y, uint16_t width,
                       uint16_t height, uint16_t color_key);
int ts2d_blit_alpha(ts2d_frame_t *frame, int16_t src_x, int16_t src_y,
                    int16_t dst_x, int16_t dst_y, uint16_t width,
                    uint16_t height, uint8_t alpha);
int ts2d_end_frame(ts2d_frame_t *frame);
int ts2d_submit(ts2d_frame_t *frame);
int ts2d_wait(uint32_t timeout_polls);
int ts2d_wait_frame(uint32_t timeout_polls);
int ts2d_get_perf(ts2d_perf_t *out);

int renderer_sw_fill(ts2d_surface_t *dst, int x, int y, unsigned width,
                     unsigned height, uint16_t color);
int renderer_sw_blit(ts2d_surface_t *dst, const ts2d_surface_t *src,
                     int src_x, int src_y, int dst_x, int dst_y,
                     unsigned width, unsigned height);
int renderer_sw_colorkey(ts2d_surface_t *dst, const ts2d_surface_t *src,
                         int src_x, int src_y, int dst_x, int dst_y,
                         unsigned width, unsigned height, uint16_t color_key);
int renderer_sw_alpha(ts2d_surface_t *dst, const ts2d_surface_t *src,
                      int src_x, int src_y, int dst_x, int dst_y,
                      unsigned width, unsigned height, uint8_t alpha);
int renderer_sw_execute(ts2d_surface_t *dst, const ts2d_surface_t *src,
                        const ts2d_command_t *commands, size_t command_count);

#ifdef __cplusplus
}
#endif

#endif
