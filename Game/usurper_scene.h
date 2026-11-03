#ifndef USURPER_SCENE_H
#define USURPER_SCENE_H

#include <stdint.h>
#include "usurper_profile.h"
#include "usurper_loadout.h"
#include "usurper_settings.h"

#include "../Engine/Software/sw/include/ts2d_command_defs.h"

#define USURPER_GPU_WIDTH 1024u
#define USURPER_GPU_HEIGHT 576u
#define USURPER_GPU_STRIDE 2048u
#define USURPER_DESIGN_WIDTH 960u
#define USURPER_DESIGN_HEIGHT 540u
#define USURPER_SPRITE_SIZE 24u
#define USURPER_ATLAS_BASE 0x02000000u
#define USURPER_ATLAS_WIDTH 1024u
#define USURPER_ATLAS_STRIDE (USURPER_ATLAS_WIDTH * 2u)
#define USURPER_SOLID_BASE 0x0c000000u
#define USURPER_SOLID_BYTES (USURPER_GPU_WIDTH * USURPER_GPU_HEIGHT * 2u)
#define USURPER_DYNAMIC_PALETTE_BASE 0x0c600000u
#define USURPER_DYNAMIC_PALETTE_ROWS 128u
#define USURPER_DYNAMIC_PALETTE_TILE_HEIGHT 16u
#define USURPER_CAPTIVE_SCRATCH_BASE 0x0c500000u
#ifdef USURPER_DIAGNOSTIC_STAGE
void usurper_scene_diagnostic_start(unsigned stage);
#endif
/* Upper 16 MiB of the documented 256 MiB DDR are the upload workspace.
 * Rebased source addresses let the atlas use unsigned 16-bit row metadata. */
#define USURPER_ASSET_COMPRESSED_BASE 0x0f000000u
#define USURPER_RLE_WORKSPACE_BASE 0x0d000000u
#define USURPER_RLE_WORKSPACE_LIMIT 0x02000000u
#define USURPER_TUTORIAL_FRAME_HEIGHT 102u
#define USURPER_TUTORIAL_FRAME_COUNT 17u
#define USURPER_MENU_FRAME_HEIGHT 540u
#define USURPER_MENU_FRAME_COUNT 2u
#define USURPER_ASSET_TRANSFER_LIMIT (16u * 1024u * 1024u)
#define USURPER_FB_A 0x01000000u
#define USURPER_FB_B 0x01400000u

typedef int (*usurper_emit_fn)(void *context, const ts2d_command_t *command);

enum usurper_control_flags {
    USURPER_CONTROL_CANCEL = 0x40u,
    USURPER_POINTER_HIDDEN = 0x80u,
};

enum usurper_phase {
    USURPER_PHASE_MENU = 0,
    USURPER_PHASE_BATTLE = 1,
    USURPER_PHASE_RESULT = 2,
    USURPER_PHASE_PAUSED = 3,
    USURPER_PHASE_TECH = 4,
    USURPER_PHASE_TUTORIAL = 5,
    USURPER_PHASE_ARRIVAL = 6,
    USURPER_PHASE_SETTINGS = 7,
    USURPER_PHASE_CREDITS = 8,
    USURPER_PHASE_QUIT = 9,
};

enum usurper_input {
    USURPER_INPUT_UP = 1u << 0,
    USURPER_INPUT_DOWN = 1u << 1,
    USURPER_INPUT_LEFT = 1u << 2,
    USURPER_INPUT_RIGHT = 1u << 3,
    USURPER_INPUT_CAPTURE = 1u << 4,
    USURPER_INPUT_START = 1u << 5,
    USURPER_INPUT_PAUSE = 1u << 6,
    USURPER_INPUT_RESET = 1u << 7,
    USURPER_INPUT_RELEASE = 1u << 8,
    USURPER_INPUT_TARGET = 1u << 9,
    USURPER_INPUT_TECH_NEXT = 1u << 10,
    USURPER_INPUT_TECH_BUY = 1u << 11,
    USURPER_INPUT_WHEEL = 1u << 12,
    USURPER_INPUT_FIRE_RELEASE = 1u << 13,
};

void usurper_scene_reset(void);
void usurper_scene_step(void);
void usurper_scene_render_alpha(unsigned alpha_q8);
void usurper_scene_render_display_alpha(unsigned alpha_q8);
void usurper_scene_input(unsigned input);
void usurper_scene_controls(unsigned held, unsigned aim_angle, int mouse_x, int mouse_y, int wheel);
void usurper_scene_uart_byte(unsigned byte);
int usurper_scene_console_input_allowed(void);
void usurper_scene_key(int key);
int usurper_scene_build(uint32_t framebuffer, usurper_emit_fn emit,
                        void *context);
int usurper_scene_load_assets(unsigned bundle_size);
unsigned usurper_scene_asset_height(void);
int usurper_receive_asset_bundle(void);

unsigned usurper_scene_phase(void);
unsigned usurper_scene_boss_active(void);
unsigned usurper_scene_boss_hp(void);
unsigned usurper_scene_result_victory(void);
unsigned usurper_scene_wave(void);
unsigned usurper_scene_tick(void);
unsigned usurper_scene_player_hp(void);
unsigned usurper_scene_player_max_hp(void);
unsigned usurper_scene_energy(void);
unsigned usurper_scene_captured(void);
unsigned usurper_scene_enemy_count(void);
unsigned usurper_scene_supply_count(void);
unsigned usurper_scene_supply_pending(void);
void usurper_scene_diagnostic_supplies(void);
void usurper_scene_diagnostic_warden(void);
unsigned usurper_scene_warden_hull_progress(void);
unsigned usurper_scene_warden_hull_scale(void);
unsigned usurper_scene_warden_active(void);
unsigned usurper_scene_warden_intercepted(void);
unsigned usurper_scene_interceptor_count(void);
unsigned usurper_scene_interceptor_hits(void);
unsigned usurper_scene_weak_count(void);
unsigned usurper_scene_visible(void);
unsigned usurper_scene_tutorial_step(void);
unsigned usurper_scene_tutorial_complete(void);
unsigned usurper_scene_alloy(void);
unsigned usurper_scene_released(void);
unsigned usurper_scene_tech_level(void);
unsigned usurper_scene_level_tick(void);
unsigned usurper_scene_boss_armor(void);
unsigned usurper_scene_boss_phase(void);
unsigned usurper_scene_stage(void);
unsigned usurper_scene_completed_mask(void);
unsigned usurper_scene_boss_open(void);
unsigned usurper_scene_cleared(void);
unsigned usurper_scene_menu_selection(void);
unsigned usurper_scene_menu_confirm(void);
unsigned usurper_scene_pointer_x(void);
unsigned usurper_scene_pointer_y(void);
unsigned usurper_scene_pointer_visible(void);
unsigned usurper_scene_controls_held(void);
unsigned usurper_scene_boundary_time(unsigned part);
const usurper_loadout_t *usurper_scene_loadout(void);
/* Rammer form uses its own aim/release without consuming the captured fleet. */
void usurper_scene_form_aim(unsigned pressed);
int usurper_scene_loadout_apply(const usurper_loadout_t *value);
int usurper_scene_select_form(int32_t index);
int usurper_scene_select_starting_species(unsigned species);
int usurper_scene_select_support(unsigned slot,unsigned species);
unsigned usurper_scene_starting_unit_spawned(void);
const char *usurper_scene_message(void);
const usurper_settings_t *usurper_scene_settings(void);
int usurper_scene_settings_apply(const usurper_settings_t *settings);
/* Available when firmware is built with USURPER_PROFILE_ENABLED and profile.c.
 * Only settled MENU/TECH state can be imported/exported. RAM is volatile. */
void usurper_scene_profile_rules(us_profile_rules_t *rules);
int usurper_scene_profile_export(us_profile_t *profile);
int usurper_scene_profile_import(const us_profile_t *profile);

#endif
