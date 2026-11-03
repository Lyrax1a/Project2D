#include "usurper_frame_pacing.h"
#include "../Engine/Software/platform/board_conformance/firmware/runtime.h"
#include "../Engine/Software/platform/board_conformance/firmware/gpu_runtime.h"
#include "usurper_scene.h"
#include "usurper_cpu_renderer.h"
#include "usurper_baked_assets.h"
#include "usurper_profile_console.h"
#include "usurper_audio.h"
#include "usurper_export.h"
#include "usurper_warden_enemy.h"
#include "usurper_campaign.h"
#include "../Engine/Software/preboard_core/sw/include/ts2d_preboard.h"

#include <string.h>

static unsigned mode = 1;
static unsigned bank;
static unsigned used;
static unsigned commands;
static unsigned batch_count;
static uint32_t framebuffer = USURPER_FB_B;
/* Exclusive 32 KiB upload staging below the two framebuffer allocations.
 * Releasing this from the small firmware BSS leaves room for complete gameplay
 * and profile state; it is neither a rendering source nor a sample buffer. */
static ts2d_command_t *const command_list=(ts2d_command_t *)(uintptr_t)0x00f00000u;
#ifndef USURPER_COMMAND_BATCH_MAX
#define USURPER_COMMAND_BATCH_MAX 1023u
#endif
#if USURPER_COMMAND_BATCH_MAX < 1 || USURPER_COMMAND_BATCH_MAX > 1023
#error Invalid command batch capacity
#endif
#ifndef USURPER_EARLY_SUBMIT_COMMANDS
#define USURPER_EARLY_SUBMIT_COMMANDS 16u
#endif
#if USURPER_EARLY_SUBMIT_COMMANDS > USURPER_COMMAND_BATCH_MAX
#error Early submission exceeds command bank capacity
#endif
static unsigned command_batch_limit = USURPER_COMMAND_BATCH_MAX;
static unsigned normal_command_batch_limit(void) {
    return mode && usurper_scene_phase() == USURPER_PHASE_BATTLE &&
        USURPER_COMMAND_BATCH_MAX > 128u ? 128u : USURPER_COMMAND_BATCH_MAX;
}
static unsigned gpu_pending;
static uint32_t render_cycles;
static uint32_t submit_cycles;
static uint32_t stream_wait_cycles;
static uint32_t bus_read;
static uint32_t bus_write;
static uint32_t fps_x100;
static char osd_text[256];
static unsigned text_index;
static unsigned fast_asset_link;

int usurper_receive_asset_bundle(void) {
    uint8_t tag[4];
    uint32_t size = 0;
    uint32_t expected = 0;
    const uint32_t serial_base = USURPER_ASSET_COMPRESSED_BASE;
    volatile uint8_t *destination = (volatile uint8_t *)(uintptr_t)serial_base;
    uint64_t deadline;
    uint32_t crc = 0xffffffffu;
receive_header:
    crc=0xffffffffu;size=expected=0;
    for (unsigned i = 0; i < 4u; ++i) {
        deadline = now64() + (uint64_t)CPU_HZ * 180u;
        int byte;
        do { byte = getch(); uart_service(); } while (byte < 0 && now64() < deadline);
        if (byte < 0) return -1;
        tag[i] = (uint8_t)byte;
    }
    if(tag[0]=='A'&&tag[1]=='S'&&tag[2]=='F'&&tag[3]=='T'){
        if(fast_asset_link||usurper_export_begin())return -1;
        fast_asset_link=1;goto receive_header;
    }
    unsigned cached=tag[2]=='C'&&tag[3]=='K';
    if (tag[0] != 'A' || tag[1] != 'S' || (!cached&&(tag[2] != 'S' || tag[3] != 'T'))) return -1;
    for (unsigned i = 0; i < 4u; ++i) {
        uint32_t value = 0;
        for (unsigned lane = 0; lane < 4u; ++lane) {
            deadline = now64() + (uint64_t)CPU_HZ * 180u;
            int byte;
            do { byte = getch(); uart_service(); } while (byte < 0 && now64() < deadline);
            if (byte < 0) return -1;
            value |= (uint32_t)(uint8_t)byte << (lane * 8u);
        }
        if (i == 0u) size = value;
        else expected = value;
        if (i == 1u) break;
    }
    if (size < 20u || size > USURPER_ASSET_TRANSFER_LIMIT) return -1;
    for (uint32_t i = 0; i < size; ++i) {
        uint8_t value;
        if(cached)value=destination[i];
        else {
            deadline = now64() + (uint64_t)CPU_HZ * 180u;
            int byte;
            do { byte = getch(); uart_service(); } while (byte < 0 && now64() < deadline);
            if (byte < 0) return -1;
            value=(uint8_t)byte;destination[i]=value;
        }
        crc ^= value;
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    if ((crc ^ 0xffffffffu) != expected) {
        if(cached){puts_uart("ASSET_CACHE=MISS\r\n");uart_flush();goto receive_header;}
        return -1;
    }
    if(cached){puts_uart("ASSET_CACHE=PASS\r\n");uart_flush();}
    drain();
    invalidate();
    return (int)size;
}

static void text(const char *value) {
    while (*value && text_index < sizeof(osd_text)) osd_text[text_index++] = *value++;
}

static void digits(unsigned value) {
    char buffer[11];
    unsigned index = sizeof(buffer) - 1u;
    buffer[index] = 0;
    do {
        buffer[--index] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    text(buffer + index);
}

static void phase_name(unsigned phase) {
    text(phase == USURPER_PHASE_MENU ? "MENU" :
         phase == USURPER_PHASE_TECH ? "RESEARCH" :
         phase == USURPER_PHASE_TUTORIAL ? "TUTORIAL" :
         phase == USURPER_PHASE_ARRIVAL ? "ARRIVAL" :
         phase == USURPER_PHASE_BATTLE ? "BATTLE" :
         phase == USURPER_PHASE_PAUSED ? "PAUSE" : "RESULT");
}

static void update_osd(void) {
    if (GPU(0x700) & 1u) return;
    memset(osd_text, ' ', sizeof(osd_text));
    text_index = 0;
    text("USURPER ");
    text(mode == 0u ? "CPU " : mode == 1u ? "FPGA IMMEDIATE " : "FPGA TILE ");
    phase_name(usurper_scene_phase());
    text(" T+");
    digits(usurper_scene_level_tick() / 60u);
    text("s FPS ");
    digits(fps_x100 / 100u);
    text(".");
    digits((fps_x100 % 100u) / 10u);
    text_index = 64;
    text("HULL ");
    digits(usurper_scene_player_hp());
    text("/");
    digits(usurper_scene_player_max_hp());
    text(" LIFE ");
    digits(usurper_scene_energy());
    text(" FLEET ");
    digits(usurper_scene_captured());
    text(" ALLOY ");
    digits(usurper_scene_alloy());
    text(" ARMOR ");
    digits(usurper_scene_boss_armor());
    text(" P");
    digits(usurper_scene_boss_phase());
    text_index = 128;
    text(usurper_scene_message());
    text_index = 192;
    text("WASD MOVE F USE V CYCLE H STRESS ~ PROFILE 0/1/2 MODE");
    for (unsigned i = 0; i < sizeof(osd_text); ++i) GPU(0x800u + i * 4u) = (unsigned char)osd_text[i];
    barrier();
    GPU(0x700) = 1;
    barrier();
}

/* Only called outside measured frames. Hardware OSD is separate from both
 * RGB565 buffers, so the exported framebuffer and its CRC are unchanged. */


static int finish_gpu(void) {
    if (!gpu_pending) return 0;
    if (gpu_wait_complete()) {
        puts_uart("GPU_WAIT_FAIL status=");
        number(GPU(0x0c));
        puts_uart(" error=");
        number(GPU(0x14));
        puts_uart(" completed=");
        number(GPU(0x3c));
        puts_uart("\r\n");
        return -1;
    }
    render_cycles += GPU(0x54);
    bus_read += GPU(0x64);
    bus_write += GPU(0x68);
    gpu_pending = 0;
    return 0;
}

/* bank is the inactive upload bank: submission toggles it only after waiting
 * for the preceding batch. The command ABI is eight little-endian words.
 * memcpy of one word avoids type-punning/strict-aliasing assumptions and lets
 * RV32 load each word once, without a write-through DDR staging copy. */

static void upload_command(unsigned index, const ts2d_command_t *command) {
    if (!index) GPU(0x34) = bank;
    volatile uint32_t *words = (volatile uint32_t *)(uintptr_t)
        (0xf8108000u + index * 32u);
    for (unsigned i = 0; i < 8u; ++i) {
        uint32_t word;
#if defined(__GNUC__)
        /* Firmware uses -ffreestanding: spell the builtin explicitly so a
         * constant four-byte copy does not call the generic byte-loop memcpy. */
        _Static_assert(_Alignof(ts2d_command_t) >= 4, "Command words require natural alignment");
        __builtin_memcpy(&word, __builtin_assume_aligned(
            (const unsigned char *)command + i * 4u, 4), sizeof(word));
#else
        memcpy(&word, (const unsigned char *)command + i * 4u, sizeof(word));
#endif
        words[i] = word;
    }
}

static int flush_commands(void) {
    if (!used) return 0;
    unsigned command_count = used + 1u;
    if (mode == 0) {
        command_list[used] = (ts2d_command_t){.opcode = TS2D_OP_FRAME_END};
        uint32_t start = now();
        if (usurper_cpu_execute(framebuffer, usurper_scene_asset_height(),
                               command_list, command_count)) return -1;
        drain();
        render_cycles += now() - start;
    } else {
        uint32_t start = now();
        const ts2d_command_t end = {.opcode = TS2D_OP_FRAME_END};
        upload_command(used, &end);
        barrier();
        submit_cycles += now() - start;
        if(gpu_pending){
            uint32_t wait_start=now();
            if(finish_gpu())return -1;
            stream_wait_cycles+=now()-wait_start;
        }
        start = now();
        gpu_submission_begin();
        GPU(0x1c) = (command_count << 1) | bank;
        barrier();
        submit_cycles += now() - start;
        gpu_pending = 1;
        bank ^= 1u;
    }
    used = 0;
    ++batch_count;
    return 0;
}

static int emit_command(void *context, const ts2d_command_t *command) {
    (void)context;
    if (command->opcode == TS2D_OP_FRAME_END) return flush_commands();
    /* Battle geometry is expensive to construct: stream bounded batches so
     * the GPU can work while later effects are generated. Tech/UI commands
     * benefit from the ordinary large batch; splitting their layers adds
     * waits without useful overlap. Both retain the exact emission order. */
    /* Immediate can build more commands while the earlier bank is busy.
     * Waiting at the soft 128-command limit needlessly stalls that work.
     * The physical bank capacity remains a hard bound including FRAME_END;
     * CPU/Tile retain their established grouping. Source pixels stay pinned
     * until the final completion, as for ordinary streamed submission. */
    if (used >= command_batch_limit &&
        (mode != 1u || used >= USURPER_COMMAND_BATCH_MAX ||
         !gpu_pending || !gpu_submission_pending()) && flush_commands()) return -1;
    if (mode == 0) command_list[used] = *command;
    else {
        /* Per-command clock reads distort this hot path. Uploads remain in
         * build and acknowledged presentation time; submit measures only
         * FRAME_END upload and the doorbell tail (US_EX_SUBMIT_SCOPE). */
        upload_command(used, command);
    }
    ++used;
    ++commands;
    /* Start the background as soon as its opaque copy is ready. Waiting for
     * the first sprite/effect layers wastes the overlap with their CPU build.
     * Other scenes use the bounded prefix as before. Only the first batch is
     * shortened: subsequent uploads retain both banks
     * and their ordinary capacity. Source pixels remain pinned for the whole
     * frame, and flush_commands waits for the previous batch before starting
     * its successor. CPU mode has no asynchronous engine to overlap.
     * Zero disables this scheduling optimization for a matched comparison. */
    if (mode && !batch_count && USURPER_EARLY_SUBMIT_COMMANDS &&
        (used >= USURPER_EARLY_SUBMIT_COMMANDS ||
         (command->opcode == TS2D_OP_BLIT_OPAQUE &&
          command->src_format == TS2D_FMT_RGB565 &&
          command->src_base_addr == USURPER_ATLAS_BASE +
              usurper_scene_asset_height() * USURPER_ATLAS_STRIDE)) &&
        flush_commands()) return -1;
    return 0;
}

static int present(void) {
    uint32_t start = now();
    GPU(0x408) = framebuffer;
    barrier();
    GPU(0x40c) = 1;
    barrier();
    while (GPU(0x404) & 1u) {
        uart_service();
        if ((uint32_t)(now() - start) > CPU_HZ) return -1;
    }
    framebuffer = framebuffer == USURPER_FB_A ? USURPER_FB_B : USURPER_FB_A;
    return 0;
}

/* Same renderer, caches, command banks and acknowledged display swap as the
 * ordinary game. The stress runner times this entire callback and its wait. */



int main(void) {
    uart_init();
    puts_uart("USURPER_BOARD 1080P60 16:9 RGB565; WASD MOVE, MOUSE AIM, LEFT/F USE, RIGHT/T MARK, WHEEL/V CYCLE, ENTER DEPLOY\r\n");
    if (GPU(0) != 0x54533244u || GPU(4) != 0x00020000u || GPU(56) != 1024u) {
        puts_uart("FAIL CSR version\r\n");
        uart_flush();
        return 1;
    }
    gpu_interrupt_init();
    usurper_audio_init();
    puts_uart("ASSET_UPLOAD_BAUD=460800\r\nASSET_READY\r\n");
    uart_flush();
    int source_asset_bytes = usurper_receive_asset_bundle();
    if (source_asset_bytes < 0 || usurper_scene_load_assets((unsigned)source_asset_bytes)) {
        puts_uart("FAIL SOURCE ASSETS\r\n");
        uart_flush();
        return 2;
    }
    puts_uart("ASSET_LOAD=PASS\r\n");
    uart_flush();
    if(usurper_baked_assets_load()){
        puts_uart("FAIL WARDEN ASSETS\r\n");uart_flush();return 2;
    }
    if(fast_asset_link){
        puts_uart("ASSET_SESSION_DONE\r\n");uart_flush();
        if(usurper_export_finish())return 2;
        fast_asset_link=0;
    }
    usurper_scene_reset();
#ifdef USURPER_DIAGNOSTIC_SUPPLIES
    usurper_scene_diagnostic_supplies();
#endif
#ifdef USURPER_DIAGNOSTIC_WARDEN
    usurper_scene_diagnostic_warden();
    puts_uart("BOARD_DIAGNOSTIC WARDEN=1 FORCED_UNIT=1 STATIC_THREATS=1 INITIAL_PLAYER_HP=9999\r\n");uart_flush();
#ifdef USURPER_DIAGNOSTIC_WARDEN_HULL
    puts_uart("BOARD_DIAGNOSTIC WARDEN_HULL=1 FORCED_ENEMY=1 FORCED_CAPTURE_PROGRESS=1 STATIC_ACTOR=1\r\n");uart_flush();
#endif
#endif
#ifdef USURPER_DIAGNOSTIC_STAGE
    usurper_scene_diagnostic_start(USURPER_DIAGNOSTIC_STAGE);
    puts_uart("BOARD_DIAGNOSTIC STAGE=");number(USURPER_DIAGNOSTIC_STAGE);
    puts_uart(" FORCED_BOSS=1 INITIAL_PLAYER_HP=9999\r\n");uart_flush();
#endif
    GPU(0x30) = (USURPER_GPU_HEIGHT << 16) | USURPER_GPU_WIDTH;
    GPU(0x2c) = USURPER_GPU_STRIDE;
    GPU(0x41c) = 0;
    /* Leave OSD 0x700 untouched: it is a page-commit toggle, not an enable bit. */
    barrier();
    uint32_t last = now();
    uint32_t accumulator = 0;
    uint32_t pacing_step_cost=0,pacing_draw_cost=0;
    uint32_t window = last;
    unsigned frames = 0;
    uint64_t step_total=0,build_total=0,wait_total=0,present_total=0,gpu_total=0,submit_total=0,steps_total=0;
    unsigned first_present = 1;
    for (;;) {
        uart_service();
        usurper_audio_service();
        int key;
        while ((key = getch()) >= 0) {
            if (usurper_scene_console_input_allowed() && key == '~') {
                if (finish_gpu()) return 2;
                usurper_audio_mute(1);usurper_profile_console();usurper_audio_mute(0);
                last=window=now();accumulator=frames=0;
                step_total=build_total=wait_total=present_total=gpu_total=submit_total=steps_total=0;
                continue;
            }
            usurper_scene_uart_byte((unsigned)key);
        }
        uint32_t start = now();
        accumulator += start - last;
        last = start;
        unsigned steps = 0;
        while (accumulator >= CPU_HZ / 60u && steps < 8u) {
            if(steps&&us_pacing_defer(accumulator,now()-start,pacing_step_cost,pacing_draw_cost,CPU_HZ/60u))break;
            uint32_t pacing_start=now();
            usurper_scene_step();
            pacing_step_cost=us_pacing_step_cost(pacing_step_cost,now()-pacing_start);
            accumulator -= CPU_HZ / 60u;
            ++steps;
        }
        uint32_t step_cycles=now()-start;
        step_total+=step_cycles;steps_total+=steps;
        unsigned render_alpha=(unsigned)((uint64_t)accumulator*256u/(CPU_HZ/60u));
        usurper_scene_render_display_alpha(render_alpha);
        GPU(8) = mode << 2;
        GPU(36) = framebuffer;
        barrier();
        used = commands = batch_count = 0;
        command_batch_limit = normal_command_batch_limit();
        render_cycles = submit_cycles = stream_wait_cycles = bus_read = bus_write = 0;
        uint32_t build_start = now();
        if (usurper_scene_build(framebuffer, emit_command, 0)) {
            puts_uart("FAIL render\r\n");
            uart_flush();
            return 2;
        }
        uint32_t build_cycles=now()-build_start;
        build_total+=build_cycles;uint32_t wait_start=now();
        if (finish_gpu()) {
            puts_uart("FAIL GPU completion\r\n");
            uart_flush();
            return 2;
        }
        uint32_t wait_cycles=now()-wait_start;
        pacing_draw_cost=build_cycles+wait_cycles;
        wait_total+=wait_cycles;uint32_t present_start=now();
        if (present()) {
            puts_uart("FAIL display present\r\n");
            uart_flush();
            return 2;
        }
        uint32_t present_cycles=now()-present_start;
        present_total+=present_cycles;gpu_total+=render_cycles;submit_total+=submit_cycles;

        ++frames;
        if (first_present) {
            puts_uart("GAME_FIRST_PRESENT=PASS\r\n");
            first_present = 0;
        }
        uint32_t elapsed = now() - window;
        if (elapsed >= CPU_HZ) {
            fps_x100 = (uint32_t)((uint64_t)frames * CPU_HZ * 100u / elapsed);
            /* The visible HUD stays active during recording. UART statistics
             * are exported after the recorded game, outside the timed loop. */
            if(1){
            puts_uart("GAME=USURPER_1_1 PHASE=");
            number(usurper_scene_phase());
            puts_uart(" TUTORIAL=");
            number(usurper_scene_tutorial_step());
            puts_uart(" WAVE=");
            number(usurper_scene_wave());
            puts_uart(" HP=");
            number(usurper_scene_player_hp());
            puts_uart(" ENERGY=");
            number(usurper_scene_energy());
            puts_uart(" CAPTURED=");
            number(usurper_scene_captured());
            puts_uart(" ENEMIES=");
            number(usurper_scene_enemy_count());
            puts_uart(" SUPPLIES_LIVE=");number(usurper_scene_supply_count());
            puts_uart(" SUPPLIES_PENDING=");number(usurper_scene_supply_pending());
#ifdef USURPER_DIAGNOSTIC_WARDEN
            puts_uart(" WARDEN_ACTIVE=");number(usurper_scene_warden_active());
            puts_uart(" WARDEN_INTERCEPTED=");number(usurper_scene_warden_intercepted());
            puts_uart(" INTERCEPTORS=");number(usurper_scene_interceptor_count());
            puts_uart(" INTERCEPTOR_HITS=");number(usurper_scene_interceptor_hits());
#ifdef USURPER_DIAGNOSTIC_WARDEN_HULL
            puts_uart(" WARDEN_HULL_PROGRESS=");number(usurper_scene_warden_hull_progress());
            puts_uart(" WARDEN_HULL_SCALE_x1000=");number(usurper_scene_warden_hull_scale());
#endif
#endif
            puts_uart(" WEAK=");
            number(usurper_scene_weak_count());
            puts_uart(" CLEARED=");
            number(usurper_scene_cleared());
            puts_uart(" BOSS_ARMOR=");
            number(usurper_scene_boss_armor());
            puts_uart(" BOSS_PHASE=");
            number(usurper_scene_boss_phase());
            puts_uart(" STAGE=");
            number(usurper_scene_stage());
            puts_uart(" COMPLETED_MASK=");
            number(usurper_scene_completed_mask());
            puts_uart(" BOSS_OPEN=");
            number(usurper_scene_boss_open());
            puts_uart(" FPS_x100=");
            number(fps_x100);
            puts_uart(" COMMANDS=");
            number(commands);
            puts_uart(" STEP_US=");number((uint32_t)(step_total/frames/(CPU_HZ/1000000u)));
            puts_uart(" BUILD_US=");number((uint32_t)(build_total/frames/(CPU_HZ/1000000u)));
            puts_uart(" GPU_WAIT_US=");number((uint32_t)(wait_total/frames/(CPU_HZ/1000000u)));
            puts_uart(" PRESENT_US=");number((uint32_t)(present_total/frames/(CPU_HZ/1000000u)));
            puts_uart(" GPU_US=");number((uint32_t)(gpu_total/frames/(CPU_HZ/1000000u)));
            puts_uart(" SUBMIT_TAIL_US=");number((uint32_t)(submit_total/frames/(CPU_HZ/1000000u)));
            puts_uart(" STEPS_x100=");number((uint32_t)(steps_total*100u/frames));
#ifdef USURPER_DIAGNOSTIC_STAGE
            puts_uart(" BOUNDARY_GEOM_US=");number(usurper_scene_boundary_time(0)/(CPU_HZ/1000000u));
            puts_uart(" BOUNDARY_PALETTE_US=");number(usurper_scene_boundary_time(1)/(CPU_HZ/1000000u));
            puts_uart(" BOUNDARY_RASTER_US=");number(usurper_scene_boundary_time(2)/(CPU_HZ/1000000u));
            puts_uart(" BOUNDARY_CAPTIVE_US=");number(usurper_scene_boundary_time(3)/(CPU_HZ/1000000u));
#endif
            puts_uart(" PRESENTED=");
            number(GPU(0x410));
            puts_uart(" UNDERFLOW=");
            number(GPU(0x414));
            puts_uart(" AXI_ERROR=");
            number(GPU(0x418));
            puts_uart(" FILTER=");
            number(GPU(0x41c));
            puts_uart(" POINTER_X=");
            number(usurper_scene_pointer_x());
            puts_uart(" POINTER_Y=");
            number(usurper_scene_pointer_y());
            puts_uart(" POINTER_VISIBLE=");
            number(usurper_scene_pointer_visible());
            puts_uart(" INPUT_HELD=");
            number(usurper_scene_controls_held());
            puts_uart("\r\n");
            if (mode) {
                puts_uart("RENDERER_BUS read=");
                number(bus_read);
                puts_uart(" write=");
                number(bus_write);
                puts_uart("\r\n");
            }
            }
            update_osd();
            /* Keep UART/OSD work in the following presentation interval. */
            window += elapsed;
            frames = 0;
            step_total=build_total=wait_total=present_total=gpu_total=submit_total=steps_total=0;
        }
    }
}
