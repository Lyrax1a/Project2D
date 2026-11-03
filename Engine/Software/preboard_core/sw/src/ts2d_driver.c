/* SPDX-License-Identifier: MIT */
#include "../include/ts2d_preboard.h"

#include <string.h>

#define TS2D_STATUS_BUSY (1u << 1)
#define TS2D_STATUS_ERROR (1u << 3)

static ts2d_config_t g_config;
static uint8_t g_next_bank;
static int g_initialized;

static volatile uint32_t *reg_ptr(unsigned offset) {
    return g_config.registers + (offset / sizeof(uint32_t));
}

static void write_barrier(void) {
#if defined(__riscv)
    __asm__ volatile("fence iorw, iorw" ::: "memory");
#elif defined(__GNUC__)
    __sync_synchronize();
#else
    volatile uint32_t barrier = 0;
    (void)barrier;
#endif
}

static int append_draw(ts2d_frame_t *frame, uint8_t opcode,
                       int16_t src_x, int16_t src_y, int16_t dst_x,
                       int16_t dst_y, uint16_t width, uint16_t height,
                       uint8_t alpha, uint32_t parameter) {
    ts2d_command_t *command;
    if (frame == NULL || frame->commands == NULL || width == 0u || height == 0u)
        return TS2D_ERR_ARGUMENT;
    if (frame->count >= frame->capacity)
        return TS2D_ERR_CAPACITY;
    if ((uint32_t)width * 2u > frame->framebuffer_stride ||
        (opcode != TS2D_OP_FILL_RECT &&
         (uint32_t)width * 2u > frame->source_stride))
        return TS2D_ERR_RANGE;

    command = &frame->commands[frame->count++];
    memset(command, 0, sizeof(*command));
    command->opcode = opcode;
    command->flags = TS2D_FLAG_CLIP_TO_VIEWPORT;
    command->src_format = TS2D_FMT_RGB565;
    command->global_alpha = alpha;
    command->src_base_addr = frame->source_addr;
    command->dst_base_addr = frame->framebuffer_addr;
    command->src_x = src_x;
    command->src_y = src_y;
    command->dst_x = dst_x;
    command->dst_y = dst_y;
    command->width = width;
    command->height = height;
    command->src_stride_bytes =
        opcode == TS2D_OP_FILL_RECT ? 0u : frame->source_stride;
    command->dst_stride_bytes = frame->framebuffer_stride;
    command->parameter = parameter;
    return TS2D_OK;
}

int ts2d_init(const ts2d_config_t *config) {
    if (config == NULL || config->registers == NULL ||
        config->command_bank[0] == NULL || config->command_bank[1] == NULL ||
        config->command_capacity == 0u)
        return TS2D_ERR_ARGUMENT;
    g_config = *config;
    g_next_bank = 0u;
    g_initialized = 1;
    return TS2D_OK;
}

int ts2d_begin_frame(ts2d_frame_t *frame, ts2d_mode_t mode) {
    if (!g_initialized || frame == NULL || frame->commands == NULL ||
        frame->capacity == 0u || frame->framebuffer_stride == 0u ||
        frame->width == 0u || frame->height == 0u || mode > TS2D_MODE_FPGA_TILE)
        return TS2D_ERR_ARGUMENT;
    if (*reg_ptr(TS2D_REG_STATUS) & TS2D_STATUS_BUSY)
        return TS2D_ERR_BUSY;
    frame->count = 0u;
    frame->mode = (uint8_t)mode;
    frame->bank = g_next_bank;
    return TS2D_OK;
}

int ts2d_fill_rect(ts2d_frame_t *frame, int16_t x, int16_t y,
                   uint16_t width, uint16_t height, uint16_t color) {
    return append_draw(frame, TS2D_OP_FILL_RECT, 0, 0, x, y, width, height,
                       0u, color);
}

int ts2d_blit(ts2d_frame_t *frame, int16_t src_x, int16_t src_y,
              int16_t dst_x, int16_t dst_y, uint16_t width, uint16_t height) {
    return append_draw(frame, TS2D_OP_BLIT_OPAQUE, src_x, src_y, dst_x, dst_y,
                       width, height, 0u, 0u);
}

int ts2d_blit_colorkey(ts2d_frame_t *frame, int16_t src_x, int16_t src_y,
                       int16_t dst_x, int16_t dst_y, uint16_t width,
                       uint16_t height, uint16_t color_key) {
    return append_draw(frame, TS2D_OP_BLIT_COLORKEY, src_x, src_y, dst_x,
                       dst_y, width, height, 0u, color_key);
}

int ts2d_blit_alpha(ts2d_frame_t *frame, int16_t src_x, int16_t src_y,
                    int16_t dst_x, int16_t dst_y, uint16_t width,
                    uint16_t height, uint8_t alpha) {
    return append_draw(frame, TS2D_OP_BLIT_ALPHA_CONST, src_x, src_y, dst_x,
                       dst_y, width, height, alpha, 0u);
}

int ts2d_end_frame(ts2d_frame_t *frame) {
    ts2d_command_t *command;
    if (frame == NULL || frame->commands == NULL)
        return TS2D_ERR_ARGUMENT;
    if (frame->count >= frame->capacity)
        return TS2D_ERR_CAPACITY;
    command = &frame->commands[frame->count++];
    memset(command, 0, sizeof(*command));
    command->opcode = TS2D_OP_FRAME_END;
    return TS2D_OK;
}

int ts2d_submit(ts2d_frame_t *frame) {
    volatile uint32_t *destination;
    const uint32_t *source;
    size_t words;
    size_t index;
    if (!g_initialized || frame == NULL || frame->commands == NULL ||
        frame->count == 0u || frame->count > g_config.command_capacity)
        return TS2D_ERR_ARGUMENT;
    if (*reg_ptr(TS2D_REG_STATUS) & TS2D_STATUS_BUSY)
        return TS2D_ERR_BUSY;
    destination = g_config.command_bank[frame->bank & 1u];
    source = (const uint32_t *)(const void *)frame->commands;
    words = (size_t)frame->count * TS2D_COMMAND_WORDS;
    for (index = 0; index < words; ++index)
        destination[index] = source[index];
    if (g_config.cache_flush != NULL)
        g_config.cache_flush((const void *)frame->commands,
                             (size_t)frame->count * TS2D_COMMAND_BYTES);
    write_barrier();
    *reg_ptr(TS2D_REG_SUBMIT) = ((uint32_t)frame->frame_id << 16) |
                                ((uint32_t)frame->count << 1) |
                                (uint32_t)(frame->bank & 1u);
    write_barrier();
    g_next_bank = (uint8_t)(frame->bank ^ 1u);
    return TS2D_OK;
}

int ts2d_wait(uint32_t timeout_polls) {
    uint32_t status;
    while (timeout_polls-- != 0u) {
        status = *reg_ptr(TS2D_REG_STATUS);
        if (status & TS2D_STATUS_ERROR)
            return TS2D_ERR_HW;
        if ((status & TS2D_STATUS_BUSY) == 0u)
            return TS2D_OK;
        if (g_config.poll != NULL)
            g_config.poll(g_config.poll_opaque);
    }
    return TS2D_ERR_TIMEOUT;
}

int ts2d_wait_frame(uint32_t timeout_polls) {
    return ts2d_wait(timeout_polls);
}

int ts2d_get_perf(ts2d_perf_t *out) {
    uint32_t hi_before;
    uint32_t lo;
    uint32_t hi_after;
    if (!g_initialized || out == NULL)
        return TS2D_ERR_ARGUMENT;
    do {
        hi_before = *reg_ptr(TS2D_REG_PERF_RENDER_CYCLES_HI);
        lo = *reg_ptr(TS2D_REG_PERF_RENDER_CYCLES_LO);
        hi_after = *reg_ptr(TS2D_REG_PERF_RENDER_CYCLES_HI);
    } while (hi_before != hi_after);
    out->render_cycles = ((uint64_t)hi_after << 32) | lo;
    out->command_count = *reg_ptr(TS2D_REG_PERF_CMD_COUNT);
    out->pixel_count = *reg_ptr(TS2D_REG_PERF_PIXEL_COUNT);
    out->ddr_read_bytes = *reg_ptr(TS2D_REG_PERF_DDR_RD_BYTES);
    out->ddr_write_bytes = *reg_ptr(TS2D_REG_PERF_DDR_WR_BYTES);
    out->axi_stall_cycles = *reg_ptr(TS2D_REG_PERF_AXI_STALL_CYCLES);
    return TS2D_OK;
}
