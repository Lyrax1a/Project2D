#include "usurper_scene.h"
#include "usurper_background_rects.h"
#include "usurper_settings.h"
#include "usurper_audio.h"
#include "usurper_forms.h"
#include "usurper_form_combat.h"
#include "usurper_form_visual.h"
#include "usurper_render_memory.h"
#include "usurper_logic_fast.h"
#include "usurper_arc_offsets.h"
#include "usurper_campaign.h"
#include "usurper_mechanic.h"
#include "usurper_archon.h"
#include "usurper_flame.h"
#include "usurper_flame_visual.h"
#include "usurper_twin.h"
#include "usurper_boundary.h"
#include "usurper_boundary_visual.h"
#include "usurper_hangar_visual.h"
#include "usurper_controller_visual.h"
#include "usurper_heavy_bomb.h"
#include "usurper_supplies.h"
#include "usurper_rng.h"
#include "usurper_warden.h"
#include "usurper_warden_visual.h"
#include "usurper_warden_hull_visual.h"
#ifdef USURPER_WARDEN_BAKED
#include "usurper_warden_baked.h"
#endif
#include "usurper_polyline.h"
#include "usurper_triangle.h"
#include "usurper_triangle_math.h"
#include "usurper_command_pack.h"
#include "usurper_triangle_batch.h"
static us_triangle_batch_t triangle_batch;
#include "usurper_interceptor.h"
#include "usurper_threat_sort.h"
#include "usurper_geometry_cache.h"
#include "usurper_warden_enemy.h"
#include "usurper_supply_glyphs_generated.h"
#include "usurper_interceptor_sprites_generated.h"
#include "usurper_dart.h"
#include "usurper_raster.h"
#include "usurper_softfloat.h"
#include "usurper_asset_codec.h"
#include "../Assets/Generated/usurper_generated_content.h"
#include "../Assets/Generated/usurper_fidelity_content.h"
#include "usurper_sparse_ring.h"
static uint32_t sparse_ring_atlas_crc;
#ifndef USURPER_HOST
#include "../Engine/Software/platform/board_conformance/firmware/runtime.h"
#else
static void drain(void) {}
static void invalidate(void) {}
#endif

#include <string.h>

#define ENEMY_MAX 96u
#define USURPER_FLOW_VERSION 2u
#define USURPER_RAMMER_IMPLEMENTED 1u
#define USURPER_ENEMY_FRACTIONAL_HEALTH 1u
#define DEATH_FX_MAX 24u
#define DEATH_FX_TICKS 24u
#define PROJECTILE_MAX 128u
#define PICKUP_MAX 96u
#define ARRIVAL_MAX 8u
#define UNIT_MAX 8u
#define GUNNER_VOLLEY_MAX 16u
#define SKIMMER_SWEEP_MAX 16u
#define GAME_W 640
#define GAME_H 360
#define FIELD_W 800
#define FIELD_H 480
#define Q8 256
/* player.json / PlayerShip: 231.25 world units/s^2 and exp(-0.82/60).
 * Keep the coast factor in Q24 so release inertia survives Q8 rounding. */
#define PLAYER_ACCEL_STEP_Q8 ((231u * Q8 + Q8 / 4u) / 60u)
#define PLAYER_COAST_Q24 16549487
#define BOSS_WARNING_TICK 3528u
#define BOSS_TIME_TICK 3600u
#define ARRIVAL_TICKS 60u
#define UNIT_JOIN_TICKS 9u
#define TUTORIAL_ADVANCE_TICKS 168u
#define BOSS_PLATE_COUNT 21u
#define BOSS_TARGET_SLOTS 27u
#define BOSS_TARGET_CORE (ENEMY_MAX + BOSS_TARGET_SLOTS)
#define ATTACK_TARGET_MAX (BOSS_TARGET_CORE + 1u)
#define BOSS_BASE_HP 480u
#define DEFEAT_HOLD_TICKS 33u
#define DEPARTURE_TICKS 41u
#define RESULT_BUTTON_X 300
#define RESULT_BUTTON_Y 310
#define RESULT_BUTTON_STEP 118
#define RESULT_BUTTON_WIDTH 112u
#define RESULT_BUTTON_HEIGHT 54u
#define SETTINGS_BACK_X 636
#define SETTINGS_BACK_Y 474
#define SETTINGS_BACK_W 130u
#define SETTINGS_BACK_H 42u
#define ASSET_RECEIVE_LIMIT USURPER_ASSET_TRANSFER_LIMIT
#define UNIT_ORBIT_RADIUS 34u
#define TECH_ICON_SIZE 38u
#define USURPER_BG_BASE (USURPER_ATLAS_BASE + USURPER_ASSET_HEIGHT * USURPER_ATLAS_STRIDE)

enum { SPECIES_NONE, SPECIES_DART, SPECIES_SKIMMER, SPECIES_RAMMER, SPECIES_GUNNER,
       SPECIES_WARDEN, SPECIES_SPLITTER, SPECIES_BOMBER, SPECIES_LEECH, SPECIES_LANCER,
       SPECIES_SEEKER, SPECIES_PHASE_HUNTER, SPECIES_CARRIER, SPECIES_FRAGMENT };
enum { SHOT_ENEMY, SHOT_DART, SHOT_GUNNER };

typedef struct {
    int32_t x, y, prev_x, prev_y, vx, vy;
    uint16_t hp, max_hp, capture_q8, capture_away;
    uint32_t capture_fraction, decay_fraction;
    uint16_t attack_tick, contact_tick, ai_tick;
    uint16_t stun_ticks, stun_cooldown, knock_cooldown, ward_ticks;
    int32_t dash_vx, dash_vy;
    int8_t orbit_sign;
    uint8_t active, species, invincible, flash, elite, ai_state, capture_reserved;
    uint8_t carrier_offspring, expiry_queued, contact_inside;
    uint8_t supply,mechanic_supply,supply_marker;
    uint32_t offspring_expires;
    uint64_t offspring_owner;
} Enemy;

typedef struct {
    int32_t x, y;
    uint8_t active, age, species;
} DeathFx;

typedef struct {
    int32_t x, y, prev_x, prev_y, vx, vy;
    uint16_t age, lifetime;
    uint8_t active, kind, damage, pierce, rail, hull, burst;
    uint32_t hit_mask[3];
    uint32_t remaining_q8, boss_hit_mask;
    uint8_t boss_armor_hit;
    uint16_t emitter_index;
    uint32_t emitter_generation;
    uint32_t boss_hit_generation[BOSS_TARGET_SLOTS + 1u];
    uint8_t special;
    double form_damage;
} Projectile;

typedef struct {
    int32_t target_x, target_y, delay_q8;
    unsigned rounds, cadence_q8, reticle_q8;
} GunnerVolley;

typedef struct {
    uint32_t angle_q16, start_q16, step_q16;
    uint16_t ticks_left, radius, repeat_ticks, damage_q8;
    uint32_t elapsed_ticks;
    uint8_t echo, form_owned;
    double form_damage;
    uint32_t hit_tick[ATTACK_TARGET_MAX], echo_hit_tick[ATTACK_TARGET_MAX];
} SkimmerSweep;

typedef struct {
    uint32_t remaining_q16, duration_q16, age_q16;
    int32_t direction_x, direction_y;
    unsigned angle;
    uint8_t form_owned;
    uint8_t hit[ATTACK_TARGET_MAX];
} RammerCharge;

typedef struct {
    int32_t x, y, prev_x, prev_y;
    uint16_t age, recovery_q8;
    uint32_t amount;
    uint8_t active;
} Pickup;

typedef struct {
    uint16_t ticks_left;
    uint8_t count, active, species[8],sovereign_wave;
    unsigned wave_index;
    int32_t x[8], y[8];
} Arrival;

static Enemy enemies[ENEMY_MAX];
static uint16_t enemy_previous_draw_angle[ENEMY_MAX];
static us_dart_t offspring_motion[ENEMY_MAX];
static us_warden_ai_t warden_motion[ENEMY_MAX];
static us_warden_status_t warden_status[ENEMY_MAX];
static double enemy_ward_left[ENEMY_MAX];
static double enemy_damage_fraction[ENEMY_MAX];
static uint32_t enemy_generation[ENEMY_MAX];
static DeathFx death_fx[DEATH_FX_MAX];
static Projectile projectiles[PROJECTILE_MAX];
static uint32_t projectile_generation[PROJECTILE_MAX];
#define WARDEN_THREAT_COUNT (PROJECTILE_MAX+US_TWIN_BOMBS+6u+ENEMY_MAX)
typedef struct {us_interceptor_t motion;unsigned target_index;uint8_t born;} SceneInterceptor;
static SceneInterceptor interceptors[WARDEN_THREAT_COUNT];
static struct {uint32_t generation;uint8_t active;} warden_claims[WARDEN_THREAT_COUNT];
static GunnerVolley gunner_volleys[GUNNER_VOLLEY_MAX];
static SkimmerSweep skimmer_sweeps[SKIMMER_SWEEP_MAX+1u];
static RammerCharge rammer_charge;
static unsigned rammer_contact_guard, release_charge_q16;
static Pickup pickups[PICKUP_MAX];
static Arrival arrivals[ARRIVAL_MAX];
static uint8_t armor_alive[3][7];
static uint16_t armor_column_cooldown[7];
static uint8_t unit_species[UNIT_MAX];
static uint32_t unit_instance_id[UNIT_MAX], next_unit_instance_id;
static uint16_t unit_attack_tick[UNIT_MAX];
static double unit_support_clock[UNIT_MAX];
static float unit_hull_scale[UNIT_MAX];
static uint8_t unit_join_ticks[UNIT_MAX];
static int32_t unit_join_dx[UNIT_MAX], unit_join_dy[UNIT_MAX];
static uint32_t unit_bearing_q8[UNIT_MAX];
static uint32_t unit_prev_bearing_q8[UNIT_MAX];
static int32_t unit_prev_x[UNIT_MAX], unit_prev_y[UNIT_MAX];
static uint32_t unit_facing_q8[UNIT_MAX], unit_prev_facing_q8[UNIT_MAX];
static uint8_t unit_burst_left[UNIT_MAX], unit_burst_delay[UNIT_MAX];
static uint16_t species_seen_mask;
static unsigned tech_owned[USURPER_TECH_NODE_COUNT];
#ifdef USURPER_PROFILE_ENABLED
static unsigned profile_visited_stage_mask;
#endif

static unsigned phase, paused_phase, frame_tick, level_tick, spawn_index;
static unsigned campaign_stage_id;
static unsigned completed_stage_mask, boss_grace_ticks;
static usurper_loadout_t selected_loadout={-1,0,{0,0}};
static unsigned starting_unit_spawned;
static us_form_runtime_t player_form;
static us_form_combat_t form_combat;
static unsigned aim_is_form;
static double form_core_damage_fraction;
static double campaign_next_birth;
static double battle_elapsed;
static us_mechanic_t mechanic_boss;
static us_archon_t archon_boss;
static us_flame_t flame_boss;
static us_twin_t twin_boss;
static us_boundary_t boundary_boss;
static us_hangar_t hangar_boss;
static us_hangar_output_t hangar_spawn_pending;
static us_controller_t controller_boss;
static us_controller_output_t controller_events;
static us_supplies_t supplies;
static us_rng_t supply_rng;
static us_warden_field_t warden_field;
static double warden_shield_budget,warden_shield_fraction,warden_guard_left;
static double warden_pulse_clock;
/* Keep fractional guarded damage while retaining integer HUD counters. */
static double player_damage_fraction;
static unsigned warden_interceptor_hits;
static us_heavy_bomb_t controller_bombs[6];
static uint32_t controller_bomb_serial,controller_contact_serial[12];
static unsigned controller_contact_ticks[13],controller_contact_inside[13];
static int32_t controller_warning_x,controller_warning_y;
static unsigned controller_warning_valid;
static uint8_t enemy_sovereign_wave[ENEMY_MAX];
static unsigned enemy_wave_index[ENEMY_MAX];
static int32_t hangar_warning_x,hangar_warning_y;
static unsigned hangar_warning_valid,hangar_contact_inside,hangar_contact_tick;
static uint8_t boundary_unit_species[US_BOUNDARY_UNITS];
static uint32_t boundary_unit_facing[US_BOUNDARY_UNITS];
static uint32_t rammer_body_instance_id;
static unsigned twin_contact_ticks[2];
static struct { float x, y; double age; uint8_t active; } twin_explosions[8];
static unsigned tutorial_step, tutorial_timer, tutorial_complete_delay, tutorial_done;
static unsigned tutorial_seen, tutorial_rotated, tutorial_releases;
static unsigned tutorial_locked, tutorial_start_x, tutorial_start_y, tutorial_start_angle;
static unsigned tutorial_wheel_start;
static unsigned transition_ticks, boss_warning_ticks;
static unsigned boss_active, boss_spawned, boss_phase, boss_armor_left, boss_hits;
static unsigned boss_cycle_ticks, boss_shot_tick, boss_pattern, boss_hp, boss_angle, boss_locked_angle;
static int32_t boss_x, boss_y, boss_vx, boss_vy;
static unsigned boss_reforming_ticks;
static unsigned player_angle, player_hp, player_hp_max, player_shield, player_shield_max;
static unsigned player_shield_delay, player_invincible_ticks;
static unsigned player_energy_drain_fraction, player_move_ticks;
static unsigned player_recovery_fraction, player_shield_recharge_fraction, shield_emergency_used;
static unsigned recovery_guard_ticks, recovery_buffer_cooldown, release_cooldown_ticks;
static unsigned capture_focus_index, capture_focus_ticks;
static unsigned controls_held, controls_timeout, aim_target_angle;
static int pointer_x, pointer_y;
static unsigned pointer_visible, pointer_click_ticks;
static unsigned packet_size, packet_resync;
static uint8_t input_packet[10];
static int32_t player_x, player_y, player_vx, player_vy;
static int32_t previous_player_x, previous_player_y;
static int32_t render_player_x, render_player_y;
static unsigned render_alpha_q8 = 256u;
static unsigned previous_player_angle, render_player_angle;
static int32_t boss_prev_x, boss_prev_y;
static int move_x, move_y;
static unsigned unit_count, captured_total, released_total, kills_total;
static unsigned selected_node, tech_zoom_q8;
static int tech_pan_x, tech_pan_y;
static unsigned alloy_run, alloy_wallet, alloy_remainder, last_run_alloy;
static unsigned cleared_sectors, result_victory;
static unsigned candidate_index, manual_target_index, aim_capture_target_index, practice_dart_index, practice_skimmer_index;
static unsigned release_flash_ticks;
static unsigned contact_cooldown, aim_is_held, aim_charge_ticks;
static unsigned orbit_phase_q8;
static unsigned capture_flash_ticks;
static int32_t capture_flash_x, capture_flash_y;
static unsigned result_ticks, damage_flash_ticks;
static unsigned defeat_pending, defeat_tick;
static int32_t departure_start_x, departure_start_y;
static unsigned departure_angle, result_selection, result_action;
static int32_t camera_x, camera_y, previous_camera_x, previous_camera_y;
static int32_t render_camera_x, render_camera_y;
static unsigned launch_requested, buy_requested, next_tech_requested, skip_requested;
static unsigned pause_requested, target_requested, wheel_requested, reset_requested;
static unsigned assets_loaded, asset_height, render_failed;
enum { MENU_ITEM_COUNT = 3, MENU_LEFT = 195, MENU_TOP = 477,
       MENU_STEP = 200, MENU_WIDTH = 170, MENU_HEIGHT = 40 };
static unsigned menu_selection, menu_confirm, menu_confirm_selection;
static unsigned settings_selection;
static usurper_settings_t scene_settings=US_SETTINGS_DEFAULT;
static unsigned settings_shake_ticks;
static unsigned loadout_panel,loadout_selection;
static unsigned exhibition_stage_count(void){
#ifdef USURPER_EXHIBITION_THREE_STAGES
    return 3u;
#else
    return US_CAMPAIGN_STAGE_COUNT;
#endif
}
static void loadout_cycle(int direction){
    if(loadout_selection>=4u){loadout_panel=0;return;}
    if(loadout_selection>=2u&&loadout_selection-2u>=us_loadout_support_slots(completed_stage_mask))return;
    unsigned current=0;
    static const int32_t forms[]={-1,6,0,1,2,9};
    if(!loadout_selection){for(unsigned i=0;i<6u;++i)if(forms[i]==selected_loadout.form_index)current=i;}
    else current=loadout_selection==1u?selected_loadout.starting_species:selected_loadout.support_species[loadout_selection-2u];
    for(unsigned step=1;step<=6u;++step){
        unsigned candidate=(current+(direction<0?6u-step:step))%6u;
        int status=!loadout_selection?usurper_scene_select_form(forms[candidate]):
            loadout_selection==1u?usurper_scene_select_starting_species(candidate):usurper_scene_select_support(loadout_selection-2u,candidate);
        if(!status){usurper_audio_event(US_AUDIO_CLICK);return;}
    }
}
static void settings_adjust(int direction) {
    int value;
    if(settings_selection==0u)scene_settings.control_mode=(scene_settings.control_mode+(direction<0?2u:1u))%3u;
    else if(settings_selection==1u||settings_selection==2u){
        uint32_t *level=settings_selection==1u?&scene_settings.music_volume:&scene_settings.sfx_volume;
        value=(int)*level+direction*10;*level=(uint32_t)(value<0?0:value>100?100:value);
        scene_settings.flags|=settings_selection==1u?US_SETTINGS_MUSIC:US_SETTINGS_SFX;
    }else if(settings_selection==3u)scene_settings.flags^=US_SETTINGS_SHAKE;
    else if(settings_selection==4u)usurper_audio_event(US_AUDIO_CAPTURE);
    else if(settings_selection==5u)scene_settings=(usurper_settings_t)US_SETTINGS_DEFAULT;
    usurper_audio_configure(&scene_settings);
    if(settings_selection!=4u)usurper_audio_event(US_AUDIO_CLICK);
}
static unsigned menu_demo_tick, menu_demo_spawn_tick, menu_demo_run_index;
static unsigned menu_demo_shown_species_mask;
static unsigned aim_slow_phase, aim_wall_ticks;
static unsigned slow_motion_render_active;
static unsigned menu_demo_release_tick;
static int32_t menu_demo_target_x, menu_demo_target_y;
static unsigned menu_demo_target_angle;
static int32_t menu_demo_camera_x, menu_demo_camera_y;
static int32_t menu_demo_previous_camera_x, menu_demo_previous_camera_y;
static int32_t menu_demo_render_camera_x, menu_demo_render_camera_y;
static unsigned tech_hovered, tech_tooltip_visible, tech_help_visible;
static unsigned tech_feedback_ticks, tech_feedback_purchase;
static unsigned ui_return_phase;
static int ui_last_x, ui_last_y;
static unsigned ui_dragging;
static char state_message[112];
static usurper_emit_fn current_emit;
static void *current_emit_context;
static uint32_t current_framebuffer;
static unsigned graph_clip;

static unsigned min_u(unsigned a, unsigned b) { return a < b ? a : b; }
static unsigned max_u(unsigned a, unsigned b) { return a > b ? a : b; }
static int abs_i(int value) { return value < 0 ? -value : value; }
static int alloc_enemy(void);
static int alloc_projectile(void);
static int alloc_pickup(void);
static int spawn_enemy(unsigned species, int32_t x, int32_t y, unsigned invincible);
static int spawn_dart_projectile(unsigned angle, unsigned damage, unsigned pierce, unsigned rail);
static void projectile_step(void);
static void pickup_step(void);
static unsigned enemy_count(void);
static void complete_kill(unsigned index);
static void damage_enemy(unsigned index,double amount);
static void hostile_warden_counter(unsigned index,unsigned rail_tag,unsigned source_rail,unsigned source_other_rail,double duration);
static void reserve_capture_target(unsigned index);
static void menu_demo_reset(void);
static void menu_demo_step(void);
static void move_enemies(void);
static void capture_step(void);
static void unit_passive_step(void);
static void gunner_release_step(void);
static void rammer_charge_step(void);
static int start_form_dash(void);
static void release_selected(void);
static void damage_boss_target(unsigned target, unsigned damage_q8, unsigned rail);
static uint16_t rgb565(unsigned r,unsigned g,unsigned b);
static int nearest_gunner_target(int32_t x, int32_t y);
static unsigned gunner_direction(int32_t x, int32_t y, unsigned target);
static void skimmer_release_step(void);
static void release_selected(void);
static unsigned distance_q8(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
static int clamp_i(int value, int low, int high);
static void direction_q8(unsigned degrees, int *dx, int *dy);
static int angle_delta(int from, int to);
static unsigned angle_offset(unsigned angle, int offset);
static unsigned angle_to(int32_t dx, int32_t dy);
static unsigned distance_q8(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
static unsigned tech_capture_radius(void);
static unsigned tech_speed(void);
static unsigned tech_effect(const char *id);
static void cancel_combat_aim(void);
static void enter_main_menu(void);
static void settle_abandoned_run(void);
static void collect_remaining_pickups(void);
static void damage_player(unsigned damage);
static unsigned warden_controller_sweep(void *,float,float,float,float,float *,float *);
static unsigned warden_mechanic_sweep(void *,float,float,float,float,float *,float *);
static void warden_field_step(void);
static void warden_passive(unsigned slot,int32_t x,int32_t y);
static void interceptor_step(void);
static void start_defeat(void);
static unsigned advance_defeat_departure(void);
static int enemy_capture_eligible(const Enemy *enemy);
static int32_t render_interpolate(int32_t previous, int32_t current);
static void snapshot_scene_positions(void);

static int enemy_is_in_capture_range(const Enemy *enemy) {
    if(!enemy||!enemy->active)return 0;
    unsigned radius=tech_capture_radius()*Q8;
    return !us_logic_box_excludes(enemy->x,enemy->y,player_x,player_y,radius)&&
        distance_q8(enemy->x, enemy->y, player_x, player_y) <= radius;
}

static int us_strcmp(const char *left, const char *right) {
    while (*left && *left == *right) { ++left; ++right; }
    return (unsigned char)*left - (unsigned char)*right;
}

static void menu_demo_reset(void) {
    memset(enemies, 0, sizeof(enemies));
    memset(projectiles, 0, sizeof(projectiles));
    memset(pickups, 0, sizeof(pickups));
    memset(death_fx, 0, sizeof(death_fx));
    menu_demo_tick = menu_demo_spawn_tick = menu_demo_release_tick = 0;
    aim_slow_phase = aim_wall_ticks = 0;
    player_x = 400 * Q8;
    player_y = 240 * Q8;
    player_angle = 0;
    menu_demo_target_x = player_x;
    menu_demo_target_y = player_y;
    menu_demo_target_angle = player_angle;
    menu_demo_camera_x = player_x;
    menu_demo_camera_y = player_y;
    menu_demo_previous_camera_x = menu_demo_render_camera_x = player_x;
    menu_demo_previous_camera_y = menu_demo_render_camera_y = player_y;
}

static void menu_demo_spawn(unsigned species, int x, int y) {
    if (enemy_count() >= 7u) return;
    (void)spawn_enemy(species, x * Q8, y * Q8, 0u);
}

static unsigned menu_demo_turn_toward(unsigned current, unsigned target, unsigned step) {
    int delta = angle_delta((int)current, (int)target);
    return angle_offset(current, clamp_i(delta, -(int)step, (int)step));
}

static void menu_demo_choose_target(unsigned nearest, unsigned nearest_distance) {
    /* The source pilot continuously traverses the arena. A slow 18-second
     * offset makes the board demo look parked, so use a shorter racetrack
     * period when no threat is selected. */
    int32_t target_x = (400 + (int)(usurper_sin_q8[(menu_demo_tick / 2u) % 360u] * 112 / 256)) * Q8;
    int32_t target_y = (198 + (int)(usurper_sin_q8[(menu_demo_tick / 3u + 90u) % 360u] * 62 / 256)) * Q8;
    menu_demo_target_angle = (menu_demo_tick * 3u) % 360u;
    if (nearest < ENEMY_MAX && enemies[nearest].active) {
        Enemy *enemy = &enemies[nearest];
        int32_t dx = enemy->x - player_x, dy = enemy->y - player_y;
        unsigned distance = nearest_distance ? nearest_distance : Q8;
        if (!distance) distance = Q8;
        int nx = (int)((int64_t)dx * Q8 / distance);
        int ny = (int)((int64_t)dy * Q8 / distance);
        int tangent_x = -ny, tangent_y = nx;
        unsigned orbit = (menu_demo_tick * 2u + nearest * 97u) % 360u;
        int radial_wave = (int)usurper_sin_q8[(menu_demo_tick * 3u) % 360u];
        int lateral_wave = (int)usurper_sin_q8[orbit];
        /* Enter capture range on the approach, then widen the orbit once a
         * unit is retained. Both radii breathe so the pilot keeps traversing
         * instead of settling on one point beside the target. */
        int keep_distance = unit_count ? 148 : 72;
        int radial_amplitude = unit_count ? 28 : 10;
        int lateral_amplitude = unit_count ? 86 : 28;
        keep_distance += radial_wave * radial_amplitude / 256;
        target_x = enemy->x - nx * keep_distance + tangent_x * lateral_wave * lateral_amplitude / 256;
        target_y = enemy->y - ny * keep_distance + tangent_y * lateral_wave * lateral_amplitude / 256;
        menu_demo_target_angle = angle_to(dx, dy);
    }
    menu_demo_target_x = clamp_i((int)target_x, 124 * Q8, (FIELD_W - 124) * Q8);
    menu_demo_target_y = clamp_i((int)target_y, 132 * Q8, 350 * Q8);
    /* A retained target outside the pilot bounds can pin its orbit against
     * an edge. Continue along that edge until the approach is reachable. */
    if (distance_q8(player_x, player_y, menu_demo_target_x, menu_demo_target_y) < 18u * Q8) {
        if (menu_demo_target_x == 124 * Q8 || menu_demo_target_x == (FIELD_W - 124) * Q8)
            menu_demo_target_y = clamp_i((int)player_y + (player_y < 241 * Q8 ? 64 : -64) * Q8,
                                        132 * Q8, 350 * Q8);
        if (menu_demo_target_y == 132 * Q8 || menu_demo_target_y == 350 * Q8)
            menu_demo_target_x = clamp_i((int)player_x + (player_x < 400 * Q8 ? 64 : -64) * Q8,
                                        124 * Q8, (FIELD_W - 124) * Q8);
    }
}

static void menu_demo_move_player(void) {
    int32_t wanted_x = (menu_demo_target_x - player_x) * 7;
    int32_t wanted_y = (menu_demo_target_y - player_y) * 7;
    unsigned max_velocity = tech_speed() * Q8;
    unsigned wanted_speed = distance_q8(0, 0, wanted_x, wanted_y);
    /* Match PlayerShip's vector speed limit and move_toward acceleration.
     * Per-axis limits made diagonal showcase movement faster than gameplay. */
    if (wanted_speed > max_velocity) {
        wanted_x = (int32_t)((int64_t)wanted_x * max_velocity / (wanted_speed + 1u));
        wanted_y = (int32_t)((int64_t)wanted_y * max_velocity / (wanted_speed + 1u));
    }
    int32_t change_x = wanted_x - player_vx, change_y = wanted_y - player_vy;
    unsigned change_speed = distance_q8(0, 0, change_x, change_y);
    unsigned acceleration_step = PLAYER_ACCEL_STEP_Q8;
    if (change_speed > acceleration_step) {
        change_x = (int32_t)((int64_t)change_x * acceleration_step / (change_speed + 1u));
        change_y = (int32_t)((int64_t)change_y * acceleration_step / (change_speed + 1u));
    }
    player_vx += change_x;
    player_vy += change_y;
    player_x = clamp_i((int)(player_x + player_vx / 60), 124 * Q8, (FIELD_W - 124) * Q8);
    player_y = clamp_i((int)(player_y + player_vy / 60), 132 * Q8, 350 * Q8);
    player_angle = menu_demo_turn_toward(player_angle, menu_demo_target_angle, 7u);
}

static void menu_demo_move_camera(void) {
    /* Follow the pilot with easing and a small velocity look-ahead.
     * The camera therefore travels with the arena while the pilot remains
     * visibly off-centre during a manoeuvre. */
    int32_t target_x = player_x + player_vx / 5;
    int32_t target_y = player_y + player_vy / 5;
    target_x += (int32_t)usurper_sin_q8[(menu_demo_tick / 3u) % 360u] * 26;
    target_y += (int32_t)usurper_sin_q8[(menu_demo_tick / 4u + 90u) % 360u] * 18;
    target_x = clamp_i((int)target_x, (GAME_W / 2) * Q8, (FIELD_W - GAME_W / 2) * Q8);
    target_y = clamp_i((int)target_y, (GAME_H / 2) * Q8, (FIELD_H - GAME_H / 2) * Q8);
    menu_demo_camera_x += (target_x - menu_demo_camera_x) / 8;
    menu_demo_camera_y += (target_y - menu_demo_camera_y) / 8;
}

static void menu_demo_step(void) {
    if (defeat_pending) {
        if (advance_defeat_departure()) {
            /* Keep presentation state while GrowthSession._restart_menu_demo
             * replaces only the finished showcase battle. */
            unsigned selection = menu_selection, confirm = menu_confirm;
            unsigned confirm_selection = menu_confirm_selection;
            unsigned pending_launch = launch_requested, pending_pause = pause_requested;
            ++menu_demo_run_index;
            enter_main_menu();
            menu_selection = selection; menu_confirm = confirm;
            menu_confirm_selection = confirm_selection;
            launch_requested = pending_launch; pause_requested = pending_pause;
        }
        return;
    }
    if (player_invincible_ticks) --player_invincible_ticks;
    if (contact_cooldown) --contact_cooldown;
    if (player_shield_delay) --player_shield_delay;
    if (!player_shield_delay && player_shield < player_shield_max) {
        player_shield_recharge_fraction += 1u + tech_effect("H4");
        if (player_shield_recharge_fraction >= 120u) {
            player_shield = min_u(player_shield_max, player_shield + player_shield_recharge_fraction / 120u);
            player_shield_recharge_fraction %= 120u;
        }
    }
    warden_shield_budget+=3.0/60;if(warden_shield_budget>3)warden_shield_budget=3;
    warden_guard_left=warden_guard_left>1.0/60?warden_guard_left-1.0/60:0;
    ++menu_demo_tick;
    if (enemy_count() < 7u && (++menu_demo_spawn_tick >= 30u || !enemy_count())) {
        menu_demo_spawn_tick = 0;
        static const uint8_t roster[10] = {
            SPECIES_DART, SPECIES_SKIMMER, SPECIES_GUNNER, SPECIES_GUNNER,
            SPECIES_SKIMMER, SPECIES_DART, SPECIES_GUNNER, SPECIES_SKIMMER,
            SPECIES_SKIMMER, SPECIES_DART
        };
        static const int16_t spawn_points[10][2] = {
            {160, 116}, {640, 132}, {128, 198}, {680, 204},
            {220, 250}, {580, 250}, {260, 110}, {540, 112},
            {390, 104}, {400, 300}
        };
        /* The source rotates saved showcase profiles after each death. Vary
         * this three-unit slice's spawn sequence so a short run cannot repeat
         * the same Dart/Skimmer opening forever and starve the Gunner demo. */
        unsigned slot = (menu_demo_tick / 30u + menu_demo_run_index * 3u) % 10u;
        /* Keep showcase action in the unobscured arena above the menu chrome. */
        menu_demo_spawn(roster[slot], spawn_points[slot][0], spawn_points[slot][1]);
    }
    unsigned nearest = 0xffffffffu, nearest_distance = 0xffffffffu, nearest_score = 0xffffffffu;
    unsigned shown_mask = menu_demo_shown_species_mask;
    if (shown_mask == ((1u << SPECIES_DART) | (1u << SPECIES_SKIMMER) | (1u << SPECIES_GUNNER))) {
        menu_demo_shown_species_mask = shown_mask = 0u;
    }
    for (unsigned i = 0; i < ENEMY_MAX; ++i) {
        if (!enemies[i].active) continue;
        unsigned distance = distance_q8(enemies[i].x, enemies[i].y, player_x, player_y);
        unsigned score = distance;
        if (!unit_count && (shown_mask & (1u << enemies[i].species))) score += FIELD_W * Q8;
        if (score < nearest_score) { nearest = i; nearest_distance = distance; nearest_score = score; }
    }
    if (candidate_index < ENEMY_MAX && enemy_capture_eligible(&enemies[candidate_index]) &&
        enemy_is_in_capture_range(&enemies[candidate_index]) &&
        (unit_count || !(shown_mask & (1u << enemies[candidate_index].species)))) {
        nearest = candidate_index;
        nearest_distance = distance_q8(enemies[nearest].x, enemies[nearest].y, player_x, player_y);
    }
    if (!unit_count && nearest < ENEMY_MAX) manual_target_index = nearest;
    menu_demo_choose_target(nearest, nearest_distance);
    menu_demo_move_player();
    menu_demo_move_camera();
    move_enemies();
    if (defeat_pending) return;
    capture_step();
    unit_passive_step();
    ++menu_demo_release_tick;
    unsigned release_threat = 0u, close_threats = 0u;
    for (unsigned i = 0; i < ENEMY_MAX; ++i) {
        if (!enemies[i].active) continue;
        unsigned distance = distance_q8(enemies[i].x, enemies[i].y, player_x, player_y);
        if (distance < 56u * Q8) release_threat = 1u;
        if (distance < 118u * Q8) ++close_threats;
    }
    if (unit_count && menu_demo_release_tick >= 480u &&
        (release_threat || (enemy_count() >= 5u && close_threats >= 2u))) {
        unsigned releases_before = released_total;
        release_selected();
        if (released_total != releases_before) menu_demo_release_tick = 0u;
    }
    skimmer_release_step();
    gunner_release_step();
    projectile_step();
    warden_field_step();
    interceptor_step();
    if (defeat_pending) return;
    if (menu_demo_tick % 180u == 0u) {
        int pickup = alloc_pickup();
        if (pickup >= 0) {
            pickups[pickup] = (Pickup){.x = player_x + 40 * Q8, .y = player_y,
                .prev_x = player_x + 40 * Q8, .prev_y = player_y,
                .amount = 1u, .recovery_q8 = 256u, .active = 1u};
        }
    }
    pickup_step();
}

static void message(const char *text) {
    unsigned index = 0;
    while (index + 1u < sizeof(state_message) && text[index]) {
        state_message[index] = text[index];
        ++index;
    }
    state_message[index] = 0;
}

static uint32_t crc32_byte(uint32_t crc, uint8_t value) {
    crc ^= value;
    for (unsigned bit = 0; bit < 8u; ++bit)
        crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    return crc;
}

static uint16_t read_u16(const volatile uint8_t *bytes, unsigned *offset) {
    uint16_t value = bytes[*offset] | ((uint16_t)bytes[*offset + 1u] << 8);
    *offset += 2u;
    return value;
}

static uint32_t read_u32(const volatile uint8_t *bytes, unsigned *offset) {
    uint32_t value = (uint32_t)bytes[*offset] | ((uint32_t)bytes[*offset + 1u] << 8) |
        ((uint32_t)bytes[*offset + 2u] << 16) | ((uint32_t)bytes[*offset + 3u] << 24);
    *offset += 4u;
    return value;
}

static int expand_rle16(const volatile uint8_t *input, unsigned input_size,
                        unsigned *offset, volatile uint16_t *output,
                        uint32_t pixels, uint32_t expected_crc) {
    uint32_t produced = 0, crc = 0xffffffffu;
    while (produced < pixels) {
        if (*offset + 4u > input_size) return -1;
        uint16_t run = read_u16(input, offset), color = read_u16(input, offset);
        if (!run || run > pixels - produced) return -1;
        for (unsigned i = 0; i < run; ++i) {
            output[produced++] = color;
            crc = crc32_byte(crc, (uint8_t)color);
            crc = crc32_byte(crc, (uint8_t)(color >> 8));
        }
    }
    return (crc ^ 0xffffffffu) == expected_crc ? 0 : -1;
}

int usurper_scene_load_assets(unsigned bundle_size) {
    sparse_ring_atlas_crc = 0;
    const volatile uint8_t *bundle = (const volatile uint8_t *)(uintptr_t)USURPER_ASSET_COMPRESSED_BASE;
    unsigned offset = 0;
    if (bundle_size < 16u || bundle_size > ASSET_RECEIVE_LIMIT ||
        bundle[0] != 'U' || bundle[1] != 'S' ||
        !((bundle[2] == 'R' && bundle[3] == 'A') || (bundle[2] == 'Z' && bundle[3] == '4'))) return -1;
    offset = 4;
    unsigned width = read_u16(bundle, &offset), height = read_u16(bundle, &offset);
    uint32_t pixels = read_u32(bundle, &offset), expected_crc = read_u32(bundle, &offset);
    if (width != USURPER_ATLAS_WIDTH || height != USURPER_ASSET_HEIGHT || pixels != width * height) return -1;
    if (bundle[2] == 'Z') {
        if (bundle_size < 20u) return -1;
        unsigned rle_bytes = read_u32(bundle, &offset), produced;
        if (!rle_bytes || rle_bytes > USURPER_RLE_WORKSPACE_LIMIT ||
            USURPER_BG_BASE + USURPER_BG_STRIDE * USURPER_BG_HEIGHT > USURPER_RLE_WORKSPACE_BASE) return -1;
        volatile uint8_t *workspace = (volatile uint8_t *)(uintptr_t)USURPER_RLE_WORKSPACE_BASE;
        if (us_lz4_decode(bundle + offset, bundle_size - offset, workspace, rle_bytes, &produced) || produced != rle_bytes) return -1;
        bundle = workspace; bundle_size = rle_bytes; offset = 0;
    }
    if (expand_rle16(bundle, bundle_size, &offset,
            (volatile uint16_t *)(uintptr_t)USURPER_ATLAS_BASE, pixels, expected_crc) || offset != bundle_size) return -1;
    /* The arena reference is fully opaque. Convert only its two packed atlas
     * crops into a compact RGB565 surface so every gameplay frame can copy
     * the background without reading and blending the destination. */
    volatile uint16_t *background = (volatile uint16_t *)(uintptr_t)USURPER_BG_BASE;
    const volatile uint16_t *atlas = (const volatile uint16_t *)(uintptr_t)USURPER_ATLAS_BASE;
    for (unsigned y = 0; y < USURPER_BG_HEIGHT; ++y) {
        for (unsigned x = 0; x < us_ui_arena_left.w; ++x) {
            uint16_t source = atlas[(us_ui_arena_left.y + y) * (USURPER_ATLAS_STRIDE / 2u) + us_ui_arena_left.x + x];
            unsigned r = (source >> 8) & 0x0fu, g = (source >> 4) & 0x0fu, b = source & 0x0fu;
            background[y * USURPER_BG_WIDTH + x] = (uint16_t)(((r << 1 | r >> 3) << 11) |
                ((g << 2 | g >> 2) << 5) | (b << 1 | b >> 3));
        }
        for (unsigned x = 0; x < us_ui_arena_right.w; ++x) {
            uint16_t source = atlas[(us_ui_arena_right.y + y) * (USURPER_ATLAS_STRIDE / 2u) + us_ui_arena_right.x + x];
            unsigned r = (source >> 8) & 0x0fu, g = (source >> 4) & 0x0fu, b = source & 0x0fu;
            background[y * USURPER_BG_WIDTH + 1024u + x] = (uint16_t)(((r << 1 | r >> 3) << 11) |
                ((g << 2 | g >> 2) << 5) | (b << 1 | b >> 3));
        }
    }
#ifdef USURPER_FLAME_REFERENCE_SPRITES
    /* Procedural flame spans use RGB565 plus 8-bit constant alpha, keeping
     * more color/opacity precision than an ARGB4444 palette would provide. */
    volatile uint16_t *palette = (volatile uint16_t *)(uintptr_t)USURPER_ATLAS_BASE;
    for (unsigned row = 0; row < 8u; ++row)
        for (unsigned x = 0; x < us_ui_flame_palette.w; ++x)
            palette[(us_ui_flame_palette.y + row) * (USURPER_ATLAS_STRIDE / 2u) + us_ui_flame_palette.x + x] = usurper_flame_palette_rgb565[row];
#endif
#ifdef USURPER_TWIN_REFERENCE_SPRITES
    for (unsigned row = 0; row < 9u; ++row)
        for (unsigned x = 0; x < us_ui_twin_palette.w; ++x)
            ((volatile uint16_t *)(uintptr_t)USURPER_ATLAS_BASE)[(us_ui_twin_palette.y + row) * (USURPER_ATLAS_STRIDE / 2u) + us_ui_twin_palette.x + x] = usurper_twin_palette_rgb565[row];
#endif
    asset_height = height;
    const uint16_t wall_colors[2] = {rgb565(255,98,123), rgb565(98,120,136)};
    for (unsigned page = 0; page < 2u; ++page) {
        volatile uint16_t *solid = (volatile uint16_t *)(uintptr_t)(USURPER_SOLID_BASE + page * USURPER_SOLID_BYTES);
        for (unsigned i = 0; i < USURPER_GPU_WIDTH * USURPER_GPU_HEIGHT; ++i) solid[i] = wall_colors[page];
    }
    assets_loaded = 1;
    volatile uint16_t *supply_glyphs=(volatile uint16_t *)(uintptr_t)US_SUPPLY_GLYPH_BASE;
#ifdef US_SUPPLY_GLYPH_COMPRESSED
    unsigned glyph_bytes=0;
    if(us_lz4_decode(us_supply_glyph_compressed,sizeof(us_supply_glyph_compressed),
       (volatile uint8_t *)supply_glyphs,US_SUPPLY_GLYPH_RAW_BYTES,&glyph_bytes)||glyph_bytes!=US_SUPPLY_GLYPH_RAW_BYTES){assets_loaded=0;return -1;}
#else
    for(unsigned i=0;i<sizeof(us_supply_glyph_pixels)/sizeof(*us_supply_glyph_pixels);++i)supply_glyphs[i]=us_supply_glyph_pixels[i];
#endif
    volatile uint16_t *interceptor_pixels=(volatile uint16_t *)(uintptr_t)US_INTERCEPTOR_SPRITE_BASE;
    unsigned pixel=0;
    for(unsigned i=0;i<sizeof(us_interceptor_sprite_runs)/sizeof(*us_interceptor_sprite_runs);++i)
        for(unsigned j=0;j<us_interceptor_sprite_runs[i][1];++j)interceptor_pixels[pixel++]=us_interceptor_sprite_runs[i][0];
    /* The decoder's transient workspace is free after atlas expansion. */
#ifdef USURPER_WARDEN_BAKED
    us_geometry_cache_attach((void *)(uintptr_t)US_WARDEN_BAKED_CACHE_BASE,US_WARDEN_BAKED_CACHE_BYTES);
#else
    us_geometry_cache_attach((void *)(uintptr_t)USURPER_RLE_WORKSPACE_BASE,USURPER_RLE_WORKSPACE_LIMIT);
#endif
    sparse_ring_atlas_crc = expected_crc;
    drain();
    invalidate();
    return 0;
}

static unsigned sqrt_u64(uint64_t value) {
    return us_logic_sqrt_u64(value);
}

static unsigned distance_q8(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    int64_t dx = (int64_t)x1 - x0, dy = (int64_t)y1 - y0;
    return sqrt_u64((uint64_t)(dx * dx + dy * dy));
}

static void unit_world_position(unsigned slot, int32_t *x, int32_t *y) {
    if (!unit_count || slot >= unit_count) { *x = player_x; *y = player_y; return; }
    unsigned angle = (unit_bearing_q8[slot] >> 8) % 360u;
    int dx, dy;
    direction_q8(angle, &dx, &dy);
    int32_t target_x = dx * (int)UNIT_ORBIT_RADIUS;
    int32_t target_y = dy * (int)UNIT_ORBIT_RADIUS;
    int32_t offset_x = target_x, offset_y = target_y;
    if (unit_join_ticks[slot] < UNIT_JOIN_TICKS) {
        unsigned age = unit_join_ticks[slot] * 256u / UNIT_JOIN_TICKS;
        unsigned remaining = 256u - age;
        unsigned ease = 256u - (unsigned)(((uint64_t)remaining * remaining * remaining + 32768u) >> 16);
        offset_x = unit_join_dx[slot] + (int32_t)((int64_t)(target_x - unit_join_dx[slot]) * ease / 256);
        offset_y = unit_join_dy[slot] + (int32_t)((int64_t)(target_y - unit_join_dy[slot]) * ease / 256);
    }
    *x = player_x + offset_x;
    *y = player_y + offset_y;
}

static int32_t render_interpolate(int32_t previous, int32_t current) {
    return previous + (int32_t)(((int64_t)current - previous) * render_alpha_q8 / 256);
}

static unsigned render_interpolate_angle(unsigned previous, unsigned current) {
    int delta = angle_delta((int)(previous % 360u), (int)(current % 360u));
    return angle_offset(previous % 360u, delta * (int)render_alpha_q8 / 256);
}

/* Presentation history only: combat velocity, target and shot direction
 * stay unchanged. Snapshot on actual combat steps, including slow motion. */
static unsigned enemy_current_draw_angle(unsigned i) {
    const Enemy *e=&enemies[i];
    return e->supply?0u:e->species==SPECIES_GUNNER?
        angle_to(player_x-e->x,player_y-e->y):angle_to(e->vx,e->vy);
}
static unsigned enemy_render_draw_angle(unsigned i) {
    return render_interpolate_angle(enemy_previous_draw_angle[i],enemy_current_draw_angle(i));
}

static void snapshot_scene_positions(void) {
    previous_camera_x = camera_x;
    previous_camera_y = camera_y;
    menu_demo_previous_camera_x = menu_demo_camera_x;
    menu_demo_previous_camera_y = menu_demo_camera_y;
    previous_player_x = player_x;
    previous_player_y = player_y;
    boss_prev_x = boss_x;
    boss_prev_y = boss_y;
    for (unsigned i = 0; i < ENEMY_MAX; ++i) {
        enemies[i].prev_x = enemies[i].x;
        enemies[i].prev_y = enemies[i].y;
        if(enemies[i].active)enemy_previous_draw_angle[i]=(uint16_t)enemy_current_draw_angle(i);
    }
    for (unsigned i = 0; i < PROJECTILE_MAX; ++i) {
        projectiles[i].prev_x = projectiles[i].x;
        projectiles[i].prev_y = projectiles[i].y;
    }
    for (unsigned i = 0; i < PICKUP_MAX; ++i) {
        pickups[i].prev_x = pickups[i].x;
        pickups[i].prev_y = pickups[i].y;
    }
    for (unsigned i = 0; i < unit_count; ++i) {
        unit_prev_bearing_q8[i] = unit_bearing_q8[i];
        unit_prev_facing_q8[i] = unit_facing_q8[i];
        unit_world_position(i, &unit_prev_x[i], &unit_prev_y[i]);
    }
}

void usurper_scene_render_alpha(unsigned alpha_q8) {
    render_alpha_q8 = min_u(alpha_q8, 256u);
    menu_demo_render_camera_x = render_interpolate(menu_demo_previous_camera_x, menu_demo_camera_x);
    menu_demo_render_camera_y = render_interpolate(menu_demo_previous_camera_y, menu_demo_camera_y);
    render_player_x = render_interpolate(previous_player_x, player_x);
    render_player_y = render_interpolate(previous_player_y, player_y);
    render_player_angle = render_interpolate_angle(previous_player_angle, player_angle);
    render_camera_x = render_interpolate(previous_camera_x, camera_x);
    render_camera_y = render_interpolate(previous_camera_y, camera_y);
    if((scene_settings.flags&US_SETTINGS_SHAKE)&&settings_shake_ticks&&phase==USURPER_PHASE_BATTLE){
        render_camera_x+=(settings_shake_ticks&1u?1:-1)*(int32_t)(settings_shake_ticks*Q8/6u);
        render_camera_y+=(settings_shake_ticks&2u?1:-1)*(int32_t)(settings_shake_ticks*Q8/12u);
    }
}

/* Movement history spans actual combat updates, not skipped wall ticks.
 * Input rotation remains real-time. The raw alpha API is still used when
 * converting input/target coordinates, so drawing cannot change game rules.
 * Arena renders immediately after a wall step (substep=0); the ordinary
 * scheduler supplies its residual wall-step fraction. No extrapolation. */
static void scene_render_display_alpha(unsigned wall_alpha, unsigned substep_alpha) {
    wall_alpha = min_u(wall_alpha, 256u);
    unsigned world_alpha = wall_alpha;
    if (slow_motion_render_active && aim_is_held && aim_wall_ticks < 60u &&
        (phase == USURPER_PHASE_BATTLE || phase == USURPER_PHASE_TUTORIAL))
        world_alpha = min_u((aim_slow_phase * 256u + min_u(substep_alpha, 256u) * 22u) / 100u, 256u);
    usurper_scene_render_alpha(world_alpha);
    render_alpha_q8 = wall_alpha;
    render_player_angle = render_interpolate_angle(previous_player_angle, player_angle);
    render_alpha_q8 = world_alpha;
}
void usurper_scene_render_display_alpha(unsigned alpha_q8) {
    scene_render_display_alpha(alpha_q8, alpha_q8);
}

static void advance_unit_joins(void) {
    if (capture_flash_ticks) --capture_flash_ticks;
    for (unsigned i = 0; i < unit_count; ++i) {
        if (unit_join_ticks[i] < UNIT_JOIN_TICKS) ++unit_join_ticks[i];
        unsigned target = unit_count ?
            (orbit_phase_q8 + i * 360u * 256u / unit_count) % (360u * 256u) : 0u;
        if (aim_is_held && i == 0u) target = aim_target_angle * Q8;
        int delta = (int)target - (int)unit_bearing_q8[i];
        while (delta > 180 * 256) delta -= 360 * 256;
        while (delta < -180 * 256) delta += 360 * 256;
        int response = (aim_is_held && i == 0u) ? 4 : 9;
        int step = delta / response;
        if (!step && delta) step = delta > 0 ? 1 : -1;
        unit_bearing_q8[i] = (unsigned)((int)unit_bearing_q8[i] + step + 360 * 256) % (360u * 256u);
        if (unit_species[i] == SPECIES_GUNNER) {
            int32_t x, y;
            unit_world_position(i, &x, &y);
            int victim = nearest_gunner_target(x, y);
            unsigned facing = aim_is_held && !i ? aim_target_angle : victim >= 0 ?
                gunner_direction(x, y, (unsigned)victim) : player_angle;
            int turn = (int)(facing * Q8) - (int)unit_facing_q8[i];
            while (turn > 180 * Q8) turn -= 360 * Q8;
            while (turn < -180 * Q8) turn += 360 * Q8;
            unit_facing_q8[i] = (unsigned)((int)unit_facing_q8[i] + turn / 8 + 360 * Q8) % (360u * Q8);
        }
    }
}

static int clamp_i(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

static void direction_q8(unsigned degrees, int *dx, int *dy) {
    unsigned angle = degrees % 360u;
    *dx = usurper_sin_q8[(angle + 90u) % 360u];
    *dy = usurper_sin_q8[angle];
}

static unsigned angle_to(int32_t dx, int32_t dy) {
    if (!dx && !dy) return 0u;
    uint32_t ax = (uint32_t)abs_i((int)dx);
    uint32_t ay = (uint32_t)abs_i((int)dy);
    uint32_t minor = ax < ay ? ax : ay;
    uint32_t major = ax < ay ? ay : ax;
    unsigned low = 0u, high = 45u;
    while (low < high) {
        unsigned middle = (low + high) / 2u;
        int64_t cross = (int64_t)minor * usurper_sin_q8[(middle + 90u) % 360u] -
                        (int64_t)major * usurper_sin_q8[middle];
        if (cross > 0) low = middle + 1u;
        else high = middle;
    }
    unsigned acute = low;
    unsigned base = ax >= ay ? acute : 90u - acute;
    if (dx >= 0 && dy >= 0) return base;
    if (dx < 0 && dy >= 0) return 180u - base;
    if (dx < 0 && dy < 0) return 180u + base;
    return (360u - base) % 360u;
}

static int angle_delta(int from, int to) {
    int delta = (to - from + 540) % 360 - 180;
    return delta;
}

static unsigned angle_offset(unsigned angle, int offset) {
    int result = ((int)(angle % 360u) + offset) % 360;
    return (unsigned)(result < 0 ? result + 360 : result);
}

static int screen_delta_from_q8(int32_t delta) {
    /* Keep the sub-pixel remainder until after the 640x360 -> 960x540
     * projection. The previous integer division quantized moving sprites
     * before interpolation, which made smooth motion visibly step. */
    int64_t numerator = (int64_t)delta * 3;
    int64_t denominator = 2 * Q8;
    if (numerator >= 0) return (int)((numerator + denominator / 2) / denominator);
    return -(int)((-numerator + denominator / 2) / denominator);
}

static void world_to_screen(int32_t world_x, int32_t world_y, int *screen_x, int *screen_y) {
    /* Pressure-only full-field camera: 800x480 covers the entire1024x576 framebuffer
     * without side margins. Native campaign camera is unchanged. */
    
    int32_t view_x = phase == USURPER_PHASE_MENU ? menu_demo_render_camera_x : render_camera_x;
    int32_t view_y = phase == USURPER_PHASE_MENU ? menu_demo_render_camera_y : render_camera_y;
    *screen_x = GAME_W * 3 / 4 + screen_delta_from_q8(world_x - view_x);
    *screen_y = GAME_H * 3 / 4 + screen_delta_from_q8(world_y - view_y);
}

static int design_x(int x) { return x * (int)USURPER_DESIGN_WIDTH / GAME_W; }
static int design_y(int y) { return y * (int)USURPER_DESIGN_HEIGHT / GAME_H; }

/* Preserve the 960x540 reference composition while using the larger target
 * framebuffer. All destination geometry passes through this same scale. */
#include "usurper_scale_math.h"
static int scale_design_coord(int value) {
    if (value < 0) return -(int)us_scale_div15((unsigned)((-value) * 16 + 7));
    return (int)us_scale_div15((unsigned)(value * 16 + 7));
}

static unsigned scale_design_extent(unsigned value) {
    unsigned scaled = us_scale_div15(value * 16u + 7u);
    return scaled ? scaled : 1u;
}

static void tutorial_world_point(unsigned anchor_x_q8, unsigned anchor_y_q8, int32_t *x, int32_t *y) {
    unsigned sx = GAME_W * anchor_x_q8 / 256u;
    unsigned sy = GAME_H * anchor_y_q8 / 256u;
    if (sy < 154u) sy = 154u;
    *x = clamp_i((int)player_x + ((int)sx - GAME_W / 2) * Q8, 14 * Q8, (FIELD_W - 14) * Q8);
    *y = clamp_i((int)player_y + ((int)sy - GAME_H / 2) * Q8, 14 * Q8, (FIELD_H - 14) * Q8);
}

static int tech_index(const char *id) {
    /* The generated catalog is immutable. Cache only ID-to-index resolution,
     * never purchased/effective ranks, so buy/reset/import remain immediate.
     * 0=unseen,1=absent,index+2=present; long/Unicode IDs retain exact search. */
    static uint8_t cache[26][10];
    uint8_t *entry=0;
    if(id[0]>='A'&&id[0]<='Z'&&id[1]>='0'&&id[1]<='9'&&!id[2]){
        entry=&cache[(unsigned)(id[0]-'A')][(unsigned)(id[1]-'0')];
        if(*entry)return (int)*entry-2;
    }
    for (unsigned index = 0; index < USURPER_TECH_NODE_COUNT; ++index)
        if (us_strcmp(usurper_tech_nodes[index].id, id) == 0){
            if(entry)*entry=(uint8_t)(index+2u);
            return (int)index;
        }
    if(entry)*entry=1;
    return -1;
}

static unsigned tech_rank(unsigned index) {
    return usurper_tech_nodes[index].base_rank + tech_owned[index];
}

static unsigned tech_effective_rank(unsigned index) {
    unsigned owned = tech_rank(index);
    if (!owned) return 0u;
    return usurper_tech_nodes[index].effective_ranks[min_u(owned, 8u) - 1u];
}

#ifdef USURPER_BASE_RANK_COUNT
static int tech_base_index(const char *id) {
    /* Base-rank overrides are a separate immutable catalog and take priority
     * over purchasable nodes. Cache their index, including absent IDs; never
     * cache the dynamic purchased/effective rank of ordinary nodes. */
    _Static_assert(USURPER_BASE_RANK_COUNT<=254u,"base-rank cache index width");
    static uint8_t cache[26][10];
    uint8_t *entry=0;
    if(id[0]>='A'&&id[0]<='Z'&&id[1]>='0'&&id[1]<='9'&&!id[2]){
        entry=&cache[(unsigned)(id[0]-'A')][(unsigned)(id[1]-'0')];
        if(*entry)return (int)*entry-2;
    }
    for (unsigned i = 0; i < USURPER_BASE_RANK_COUNT; ++i)
        if (!us_strcmp(id, usurper_base_ranks[i].id)){
            if(entry)*entry=(uint8_t)(i+2u);
            return (int)i;
        }
    if(entry)*entry=1;
    return -1;
}
#endif

static unsigned tech_effect(const char *id) {
#ifdef USURPER_BASE_RANK_COUNT
    int base_index=tech_base_index(id);
    if(base_index>=0)return usurper_base_ranks[(unsigned)base_index].rank;
#endif
    int index = tech_index(id);
    return index >= 0 ? tech_effective_rank((unsigned)index) : 0u;
}

static unsigned tech_sum(const char *first, const char *second, const char *third, const char *fourth) {
    unsigned result = 0;
    const char *ids[4] = {first, second, third, fourth};
    for (unsigned i = 0; i < 4u; ++i) {
        int index = tech_index(ids[i]);
        if (index >= 0) result += tech_effective_rank((unsigned)index);
    }
    return result;
}

static unsigned tech_capacity(void) {
    unsigned capacity = 2u + tech_sum("F1", "F6", "F9", "Z0");
    return min_u(capacity, UNIT_MAX);
}

static unsigned tech_control_q8(void) {
    int index = tech_index("C1");
    unsigned rank = index >= 0 ? tech_effective_rank((unsigned)index) : 0u;
    static const unsigned power[4] = {102u, 154u, 230u, 307u};
    unsigned result = power[rank > 3u ? 3u : rank];
    index = tech_index("C9");
    if (index >= 0 && tech_rank((unsigned)index)) result = result * 135u / 100u;
    return result;
}

static unsigned tech_capture_radius(void) {
    int index = tech_index("C2");
    return 96u + (index >= 0 ? 8u * tech_effective_rank((unsigned)index) : 0u);
}

static unsigned tech_hull(void) {
    int index = tech_index("H1");
    unsigned value = 30u + 15u * (index >= 0 ? tech_effective_rank((unsigned)index) : 0u);
    index = tech_index("ZHP");
    if (index >= 0 && tech_rank((unsigned)index)) value += 65u;
    return value;
}

static unsigned tech_shield(void) {
    int index = tech_index("H2");
    unsigned value = 8u * (index >= 0 ? tech_effective_rank((unsigned)index) : 0u);
    index = tech_index("ZSH");
    if (index >= 0 && tech_rank((unsigned)index)) value += 40u;
    return value;
}

static unsigned tech_speed(void) {
    int index = tech_index("H3");
    return 130u + 7u * (index >= 0 ? tech_effective_rank((unsigned)index) : 0u);
}

static unsigned seen_bit(unsigned species) {
    return species > 0u && species <= 13u ? 1u << (species - 1u) : 0u;
}

static int tech_unlocked(unsigned index) {
    const usurper_tech_node_t *node = &usurper_tech_nodes[index];
    unsigned rank = tech_rank(index);
    if ((species_seen_mask & node->requires_seen_mask) != node->requires_seen_mask) return 0;
    if (node->species_mask && (species_seen_mask & node->species_mask) != node->species_mask) return 0;
    unsigned gate_index = rank < 8u ? rank : 7u;
    if (cleared_sectors < node->rank_gates[gate_index]) return 0;
    if (!node->parent[0]) return 1;
    int parent = tech_index(node->parent);
    return parent >= 0 && tech_rank((unsigned)parent) >= node->parent_rank;
}

static int tech_revealed(unsigned index) {
    if (us_strcmp(usurper_tech_nodes[index].id, "核心") == 0 || tech_rank(index)) return 1;
    int parent = tech_index(usurper_tech_nodes[index].parent);
    return parent >= 0 && (tech_rank((unsigned)parent) || tech_unlocked((unsigned)parent));
}

static unsigned tech_cost(unsigned index) {
    unsigned rank = tech_rank(index);
    const usurper_tech_node_t *node = &usurper_tech_nodes[index];
    if (rank >= node->ranks || rank >= 8u) return 0xffffffffu;
    return (unsigned)node->costs[rank];
}

static int tech_can_buy(unsigned index) {
    unsigned cost = tech_cost(index);
    return tech_unlocked(index) && cost != 0xffffffffu && alloy_wallet >= cost;
}

static void reset_tech_view(void) {
    int core = tech_index("核心");
    selected_node = core >= 0 ? (unsigned)core : 0u;
    tech_pan_x = 0;
    tech_pan_y = 0;
    tech_zoom_q8 = 256u;
    tech_hovered = 0xffffffffu;
    tech_tooltip_visible = tech_help_visible = ui_dragging = 0;
}

static void reset_run(void) {
    memset(enemies, 0, sizeof(enemies));
    memset(warden_motion,0,sizeof(warden_motion));
    memset(warden_status,0,sizeof(warden_status));
    memset(enemy_ward_left,0,sizeof(enemy_ward_left));
    memset(enemy_damage_fraction,0,sizeof(enemy_damage_fraction));
    memset(enemy_generation, 0, sizeof(enemy_generation));
    memset(enemy_previous_draw_angle,0,sizeof(enemy_previous_draw_angle));
    memset(projectile_generation,0,sizeof(projectile_generation));
    memset(interceptors,0,sizeof(interceptors));
    memset(warden_claims,0,sizeof(warden_claims));
    warden_pulse_clock=0;
    warden_interceptor_hits=0;
    memset(projectiles, 0, sizeof(projectiles));
    memset(gunner_volleys, 0, sizeof(gunner_volleys));
    memset(unit_facing_q8, 0, sizeof(unit_facing_q8));
    memset(unit_prev_facing_q8, 0, sizeof(unit_prev_facing_q8));
    memset(unit_burst_left, 0, sizeof(unit_burst_left));
    memset(unit_burst_delay, 0, sizeof(unit_burst_delay));
    memset(pickups, 0, sizeof(pickups));
    memset(arrivals, 0, sizeof(arrivals));
    memset(death_fx, 0, sizeof(death_fx));
    memset(armor_alive, 0, sizeof(armor_alive));
    memset(armor_column_cooldown, 0, sizeof(armor_column_cooldown));
    memset(unit_species, 0, sizeof(unit_species));
    memset(unit_instance_id, 0, sizeof(unit_instance_id)); next_unit_instance_id = 0;
    memset(unit_attack_tick, 0, sizeof(unit_attack_tick));
    memset(unit_support_clock,0,sizeof(unit_support_clock));
    memset(unit_hull_scale,0,sizeof(unit_hull_scale));
    memset(unit_join_ticks, 0, sizeof(unit_join_ticks));
    memset(unit_join_dx, 0, sizeof(unit_join_dx));
    memset(unit_join_dy, 0, sizeof(unit_join_dy));
    memset(unit_bearing_q8, 0, sizeof(unit_bearing_q8));
    memset(unit_prev_bearing_q8, 0, sizeof(unit_prev_bearing_q8));
    memset(unit_prev_x, 0, sizeof(unit_prev_x));
    memset(unit_prev_y, 0, sizeof(unit_prev_y));
    memset(skimmer_sweeps, 0, sizeof(skimmer_sweeps));
    memset(&rammer_charge, 0, sizeof(rammer_charge));
    rammer_contact_guard = release_charge_q16 = 0u;
    capture_flash_ticks = 0;
    unit_count = captured_total = released_total = kills_total = 0;
    alloy_run = alloy_remainder = 0;
    level_tick = spawn_index = 0;
    campaign_next_birth = 1.0;
    battle_elapsed=0;
    boss_grace_ticks = 0u;
    memset(&mechanic_boss, 0, sizeof(mechanic_boss));
    memset(&archon_boss, 0, sizeof(archon_boss));
    memset(&flame_boss, 0, sizeof(flame_boss));
    memset(&twin_boss, 0, sizeof(twin_boss));
    memset(&boundary_boss, 0, sizeof(boundary_boss));
    memset(&hangar_boss,0,sizeof(hangar_boss));
    memset(&controller_boss,0,sizeof(controller_boss));memset(&controller_events,0,sizeof(controller_events));memset(controller_bombs,0,sizeof(controller_bombs));
    us_supplies_init(&supplies);us_rng_seed(&supply_rng,1);
    supplies.loadout[0]=selected_loadout.support_species[0];
    supplies.loadout[1]=selected_loadout.support_species[1];
    starting_unit_spawned=0;
    us_form_reset(&player_form);
    us_form_combat_reset(&form_combat);
    aim_is_form=0;form_core_damage_fraction=0;
    memset(&warden_field,0,sizeof(warden_field));warden_shield_budget=warden_shield_fraction=warden_guard_left=player_damage_fraction=0;
    memset(controller_contact_ticks,0,sizeof(controller_contact_ticks));memset(controller_contact_inside,0,sizeof(controller_contact_inside));memset(controller_contact_serial,0,sizeof(controller_contact_serial));
    memset(enemy_sovereign_wave,0,sizeof(enemy_sovereign_wave));memset(enemy_wave_index,0,sizeof(enemy_wave_index));controller_warning_valid=controller_bomb_serial=0;
    memset(&hangar_spawn_pending,0,sizeof(hangar_spawn_pending));
    hangar_warning_valid=hangar_contact_inside=hangar_contact_tick=0;
    memset(boundary_unit_species, 0, sizeof(boundary_unit_species));
    memset(boundary_unit_facing, 0, sizeof(boundary_unit_facing));
    rammer_body_instance_id = 0;
    memset(twin_contact_ticks, 0, sizeof(twin_contact_ticks));
    memset(twin_explosions, 0, sizeof(twin_explosions));
    boss_active = boss_spawned = boss_phase = boss_armor_left = boss_hits = 0;
    boss_cycle_ticks = boss_shot_tick = boss_pattern = boss_hp = boss_angle = boss_locked_angle = 0;
    boss_vx = boss_vy = 0; boss_reforming_ticks = 0u;
    player_x = FIELD_W / 2 * Q8;
    player_y = FIELD_H / 2 * Q8;
    previous_player_x = player_x;
    previous_player_y = player_y;
    render_player_x = player_x;
    render_player_y = player_y;
    camera_x = previous_camera_x = render_camera_x = player_x;
    camera_y = previous_camera_y = render_camera_y = player_y;
    previous_player_angle = render_player_angle = 0u;
    player_vx = player_vy = 0;
    player_angle = 0;
    player_hp_max = tech_hull();
    player_hp = player_hp_max;player_damage_fraction=0;
    player_shield_max = tech_shield();
    player_shield = player_shield_max;
    player_shield_delay = player_invincible_ticks = 0;
    player_energy_drain_fraction = 0;
    player_recovery_fraction = player_shield_recharge_fraction = shield_emergency_used = 0u;
    recovery_guard_ticks = recovery_buffer_cooldown = release_cooldown_ticks = 0u;
    capture_focus_index = 0xffffffffu; capture_focus_ticks = 0u;
    player_move_ticks = move_x = move_y = 0;
    controls_held = controls_timeout = 0;
    aim_target_angle = player_angle;
    contact_cooldown = 0;
    orbit_phase_q8 = 0;
    capture_flash_ticks = 0;
    capture_flash_x = capture_flash_y = 0;
    aim_is_held = aim_charge_ticks = aim_slow_phase = aim_wall_ticks = 0;
    slow_motion_render_active = 0;
    candidate_index = manual_target_index = aim_capture_target_index = 0xffffffffu;
    practice_dart_index = practice_skimmer_index = 0xffffffffu;
    tutorial_step = tutorial_timer = tutorial_complete_delay = tutorial_done = 0;
    tutorial_rotated = tutorial_releases = tutorial_locked = 0;
    tutorial_start_x = tutorial_start_y = tutorial_start_angle = 0;
    tutorial_wheel_start = 0;
    transition_ticks = 0;
    release_flash_ticks = 0;
    launch_requested = buy_requested = next_tech_requested = skip_requested = 0u;
    pause_requested = target_requested = wheel_requested = reset_requested = 0u;
    result_action = 0u;
    result_victory = 0;
    defeat_pending = defeat_tick = 0;
    departure_start_x = departure_start_y = 0;
    departure_angle = result_selection = result_action = 0;
    message("SECTOR 1-1");
}

static int gameplay_input_allowed(void) {
    return !defeat_pending && (phase == USURPER_PHASE_BATTLE || phase == USURPER_PHASE_TUTORIAL);
}

static void cancel_combat_aim(void) {
    aim_is_form=0;
    aim_is_held = aim_charge_ticks = aim_slow_phase = aim_wall_ticks = 0u;
    aim_capture_target_index = 0xffffffffu;
    player_move_ticks = 0u;
    move_x = move_y = 0;
}

static void begin_combat_aim(void) {
    if (aim_is_held || !gameplay_input_allowed() || !unit_count || release_cooldown_ticks || rammer_charge.remaining_q16 ||
        (phase == USURPER_PHASE_TUTORIAL && tutorial_step != 3u)) return;
    aim_is_held = 1u;
    release_charge_q16 = 0u;
    aim_capture_target_index = candidate_index;
    aim_charge_ticks = aim_slow_phase = aim_wall_ticks = 0u;
}

static void commit_combat_aim(void) {
    release_charge_q16 = min_u(aim_wall_ticks, 48u) * 65536u / 48u;
    aim_is_held = aim_slow_phase = aim_wall_ticks = 0u;
    if(aim_is_form){aim_is_form=0;start_form_dash();}
    else release_selected();
}

static void enter_main_menu(void) {
    reset_run();
    phase = USURPER_PHASE_MENU;
    menu_selection = menu_confirm = menu_confirm_selection = 0u;
    menu_demo_reset();
}

static int alloc_enemy(void) {
    for (unsigned i = 0; i < ENEMY_MAX; ++i) if (!enemies[i].active) {return (int)i;}
    
    return -1;
}

static int alloc_projectile(void) {
    for (unsigned i = 0; i < PROJECTILE_MAX; ++i) if (!projectiles[i].active) {++projectile_generation[i];return (int)i;}
    
    return -1;
}

static int alloc_pickup(void) {
    for (unsigned i = 0; i < PICKUP_MAX; ++i) if (!pickups[i].active) return (int)i;
    return -1;
}

static unsigned enemy_base_hp(unsigned species) {
    if (!species) return 0u;
    unsigned id = species <= US_CAMPAIGN_UNIT_COUNT ? species : SPECIES_DART;
    return (us_campaign_enemy_health_q16(campaign_stage_id, id) + US_CAMPAIGN_Q16 / 2u) / US_CAMPAIGN_Q16;
}

static unsigned enemy_work(unsigned species) {
    if (!species) return 0u;
    const us_campaign_unit_t *unit = us_campaign_unit(species <= US_CAMPAIGN_UNIT_COUNT ? species : SPECIES_DART);
    return unit->capture_work;
}

static unsigned enemy_alloy(unsigned species) {
    if (!species) return 0u;
    const us_campaign_unit_t *unit = us_campaign_unit(species <= US_CAMPAIGN_UNIT_COUNT ? species : SPECIES_DART);
    return unit->alloy;
}

static unsigned enemy_recovery_q8(unsigned species) {
    if (!species) return 0u;
    const us_campaign_unit_t *unit = us_campaign_unit(species <= US_CAMPAIGN_UNIT_COUNT ? species : SPECIES_DART);
    return (unit->recovery_q16 + 128u) / 256u;
}

static unsigned enemy_speed_q8(unsigned species) {
    if (!species) return 0u;
    unsigned id = species <= US_CAMPAIGN_UNIT_COUNT ? species : SPECIES_DART;
    return (us_campaign_enemy_speed_q16(campaign_stage_id, id) + 128u) / 256u;
}

static unsigned enemy_count(void) {
    unsigned count = 0;
    for (unsigned i = 0; i < ENEMY_MAX; ++i) count += enemies[i].active&&!enemies[i].supply;
    return count;
}

static int enemy_capture_eligible(const Enemy *enemy) {
    return enemy->species != SPECIES_FRAGMENT;
}

static int spawn_enemy(unsigned species, int32_t x, int32_t y, unsigned invincible) {
    int index = alloc_enemy();
    if (index < 0) return -1;
    ++enemy_generation[index];
    enemy_ward_left[index]=enemy_damage_fraction[index]=0;us_warden_enemy_init(&warden_motion[index],x/256.0f,y/256.0f,80);
    warden_status[index]=(us_warden_status_t){.scale=1};
    enemy_sovereign_wave[index]=0;enemy_wave_index[index]=0;
    rammer_charge.hit[index] = 0u;
    unsigned hp = enemy_base_hp(species);
    enemies[index] = (Enemy){
        .x = clamp_i((int)x, 18 * Q8, (FIELD_W - 18) * Q8),
        .y = clamp_i((int)y, 18 * Q8, (FIELD_H - 18) * Q8),
        .hp = (uint16_t)hp, .max_hp = (uint16_t)hp,
        .active = 1, .species = (uint8_t)species, .invincible = (uint8_t)invincible,
        .attack_tick = species == SPECIES_GUNNER ? 42u : 0u,
        .orbit_sign = (int8_t)((index & 1) ? 1 : -1),
    };
    enemies[index].prev_x = enemies[index].x;
    enemies[index].prev_y = enemies[index].y;
    enemy_previous_draw_angle[index]=(uint16_t)enemy_current_draw_angle((unsigned)index);
    /* Hit cooldown belongs to this enemy instance, not a reused pool slot. */
    /* Full reset already zeroed these arrays. The arena's initial
     * population is synchronous, before any simulation or attack can run. */
    if(!0)
        for (unsigned i = 0; i <= SKIMMER_SWEEP_MAX; ++i)
            skimmer_sweeps[i].hit_tick[index] = skimmer_sweeps[i].echo_hit_tick[index] = 0u;
    if (phase != USURPER_PHASE_MENU) species_seen_mask |= (uint16_t)seen_bit(species);
    if(!0)
        for (unsigned i = 0; i < PROJECTILE_MAX; ++i)
            projectiles[i].hit_mask[index / 32u] &= ~(1u << (index % 32u));
    return index;
}

static void spawn_practice(unsigned species, unsigned ax_q8, unsigned ay_q8, unsigned *index_out) {
    int32_t x, y;
    tutorial_world_point(ax_q8, ay_q8, &x, &y);
    int index = spawn_enemy(species, x, y, 1u);
    *index_out = index >= 0 ? (unsigned)index : 0xffffffffu;
}

static double current_player_health(void){
    double value=player_hp-player_damage_fraction-player_energy_drain_fraction/60000.0+player_recovery_fraction/256.0;
    return value<0?0:value>player_hp_max?player_hp_max:value;
}
static unsigned visible_player_health(void){double value=current_player_health();unsigned whole=(unsigned)value;return whole<value?whole+1:whole;}
static void store_player_health(double value){
    if(value<0)value=0;else if(value>player_hp_max)value=player_hp_max;
    player_hp=(unsigned)value;if(player_hp<value)++player_hp;
    player_damage_fraction=player_hp-value;
    player_energy_drain_fraction=player_recovery_fraction=0;
}
static void receive_pickup_recovery(unsigned recovery_q8) {
    double before=current_player_health();
    if (before<=0 || defeat_pending || phase == USURPER_PHASE_RESULT) return;
    double bonus=0.12*tech_effect("N2");
    if(before/player_hp_max<0.3)bonus+=0.08*tech_effect("N4");
    store_player_health(before+recovery_q8/256.0*(1+bonus));
    if (!recovery_buffer_cooldown && tech_effect("N5")) {
        recovery_guard_ticks = tech_effect("N5") > 1u ? 60u : 36u;
        recovery_buffer_cooldown = 180u;
    }
}

static void collect_remaining_pickups(void) {
    for (unsigned i = 0; i < PICKUP_MAX; ++i) {
        if (!pickups[i].active) continue;
        alloy_run += pickups[i].amount;
        pickups[i].active = 0u;
    }
}

static unsigned supply_stock(void *context,unsigned mechanic,unsigned species){
    (void)context;unsigned count=0;
    for(unsigned i=0;i<ENEMY_MAX;++i)count+=enemies[i].active&&enemies[i].supply&&enemies[i].mechanic_supply==mechanic&&(!species||enemies[i].species==species);
    return count;
}
static void supply_constrain(void *context,float *x,float *y){
    (void)context;
    if(campaign_stage_id==5u&&boss_spawned&&boundary_boss.active){
        float rect[4];us_boundary_safe_rect(&boundary_boss,rect);
        *x=*x<rect[0]+24?rect[0]+24:*x>rect[2]-24?rect[2]-24:*x;
        *y=*y<rect[1]+24?rect[1]+24:*y>rect[3]-24?rect[3]-24:*y;
    }
}
static void supply_spawn(void *context,unsigned species,float x,float y,unsigned mechanic){
    (void)context;int index=spawn_enemy(species,(int32_t)(x*Q8),(int32_t)(y*Q8),1);
    if(index<0)return;
    Enemy *hull=&enemies[index];hull->x=hull->prev_x=(int32_t)(x*Q8);hull->y=hull->prev_y=(int32_t)(y*Q8);
    hull->supply=1;hull->mechanic_supply=(uint8_t)mechanic;hull->supply_marker=1;
    hull->vx=hull->vy=0;hull->attack_tick=65535;hull->capture_q8=0;
    /* Source supplies call setup(player,1), independent of campaign scaling. */
    hull->hp=hull->max_hp=(uint16_t)us_supplies_hull_health(species);
}
static void supply_cull(void *context,float px,float py){
    (void)context;
    for(unsigned i=0;i<ENEMY_MAX;++i)if(enemies[i].active&&enemies[i].supply){
        float dx=enemies[i].x/256.0f-px,dy=enemies[i].y/256.0f-py;
        if(us_fx_sqrt_float(dx*dx+dy*dy)>450){
            enemies[i].active=0;
            if(aim_is_held&&aim_capture_target_index==i)cancel_combat_aim();
        }
    }
}
static float supply_random_angle(void *context){(void)context;return us_rng_range(&supply_rng,0,6.28318530717958647692f);}
static void supply_hide(void *context){(void)context;for(unsigned i=0;i<ENEMY_MAX;++i)if(enemies[i].supply)enemies[i].supply_marker=0;}
static const us_supply_callbacks_t supply_callbacks={NULL,supply_stock,supply_spawn,supply_cull,supply_constrain,supply_random_angle,supply_hide};
static void supply_step(void){
    us_supply_context_t context={.elapsed=battle_elapsed,.delta=1.0/60,.stage=campaign_stage_id,.completed_mask=completed_stage_mask,
        .capacity=unit_count,.releases=released_total,.r7=tech_effect("R7"),.r8=tech_effect("R8"),.boss_spawned=(uint8_t)boss_spawned,.boss_present=(uint8_t)boss_active,
        .boss_phase=controller_boss.phase,.core_open=(uint8_t)(boss_active&&(campaign_stage_id==7u?1:campaign_stage_id==6u?hangar_boss.exposed:campaign_stage_id==5u?boundary_boss.exposed:
            campaign_stage_id==4u?twin_boss.exposed:campaign_stage_id==3u?flame_boss.core_exposed:campaign_stage_id==2u?archon_boss.core_exposed:campaign_stage_id==1u?mechanic_boss.core_exposed:!boss_armor_left)),
        .px=player_x/256.0f,.py=player_y/256.0f,.rotation=(float)(player_angle*0.01745329251994329577),.camera_x=camera_x/256.0f,.camera_y=camera_y/256.0f,
        .boss_x=boss_x/256.0f,.boss_y=boss_y/256.0f};
    us_supplies_step(&supplies,&context,&supply_callbacks);
}

static void damage_player(unsigned damage) {
    if (!player_hp || player_invincible_ticks || defeat_pending ||
        phase == USURPER_PHASE_TUTORIAL || phase == USURPER_PHASE_RESULT) return;
    if(phase==USURPER_PHASE_BATTLE){settings_shake_ticks=12u;usurper_audio_event(US_AUDIO_HURT);}
    
    double shield_before=player_shield+warden_shield_fraction;
    double amount=damage*(warden_guard_left>0?0.8:1.0);
    double absorbed=shield_before<amount?shield_before:amount;
    double shield_after=shield_before-absorbed;
    player_shield=(unsigned)shield_after;warden_shield_fraction=shield_after-player_shield;
    store_player_health(current_player_health()-(amount-absorbed));
    player_shield_delay = 360u;
    player_shield_recharge_fraction = 0u;
    player_invincible_ticks = 27u;
    if (shield_before>0 && shield_after<=0 && tech_effect("H5") && !shield_emergency_used) {
        shield_emergency_used = 1u;
        player_invincible_ticks = 30u;
    }
    if (!player_hp && (phase == USURPER_PHASE_BATTLE || phase == USURPER_PHASE_MENU)) start_defeat();
}

static void spawn_pickup(int32_t x, int32_t y, unsigned amount, unsigned recovery_q8) {
    int index = alloc_pickup();
    if (index < 0) {
        alloy_run += amount;
        receive_pickup_recovery(recovery_q8);
        return;
    }
    pickups[index] = (Pickup){.x = x, .y = y, .amount = amount,
        .recovery_q8 = (uint16_t)recovery_q8, .active = 1};
    pickups[index].prev_x = x;
    pickups[index].prev_y = y;
}

static void award_drop(const Enemy *enemy, unsigned captured) {
    unsigned bonus = captured ? 10u * tech_effect("R4") : 5u * tech_effect("R2");
    unsigned value = enemy_alloy(enemy->species) * 24u * (100u + bonus) *
        us_campaign_stage(campaign_stage_id)->alloy_multiplier;
    if(enemy->carrier_offspring)value/=4u;
    value+=alloy_remainder;
    unsigned amount = value / 10000u;
    alloy_remainder = value % 10000u;
    unsigned recovery=enemy_recovery_q8(enemy->species);
    if(enemy->carrier_offspring)recovery=recovery*35u/100u;
    spawn_pickup(enemy->x, enemy->y, amount, recovery);
}

static void spawn_death_fx(const Enemy *enemy) {
    unsigned slot = DEATH_FX_MAX;
    unsigned oldest_age = 0u;
    for (unsigned i = 0; i < DEATH_FX_MAX; ++i) {
        if (!death_fx[i].active) { slot = i; break; }
        if (death_fx[i].age > oldest_age) {
            oldest_age = death_fx[i].age;
            slot = i;
        }
    }
    if (slot >= DEATH_FX_MAX) return;
    death_fx[slot] = (DeathFx){.x = enemy->x, .y = enemy->y,
        .active = 1u, .age = 0u, .species = enemy->species};
}

static void advance_death_fx(void) {
    for (unsigned i = 0; i < DEATH_FX_MAX; ++i) {
        if (!death_fx[i].active) continue;
        if (++death_fx[i].age >= DEATH_FX_TICKS) death_fx[i].active = 0u;
    }
}

static void complete_kill(unsigned index) {
    if (index >= ENEMY_MAX || !enemies[index].active) return;
    Enemy *enemy = &enemies[index];
    /* A linked target is reserved by the usurpation channel.  Godot's
     * HurtboxComponent rejects friendly damage in this state, so preserve the
     * same invariant for passive fire, sweeps, and projectiles on the board. */
    if (enemy->capture_reserved || enemy->hp) return;
    award_drop(enemy, 0u);
    spawn_death_fx(enemy);
    enemy->active = 0;
    ++kills_total;
    if(phase!=USURPER_PHASE_MENU)usurper_audio_event(US_AUDIO_KILL);
}

static double current_enemy_health(unsigned index){
    return enemies[index].hp-enemy_damage_fraction[index];
}
static void damage_enemy_received(unsigned index,double amount){
    Enemy *enemy=&enemies[index];
    if(!enemy->active||enemy->capture_reserved||amount<=0)return;
    
    if(phase!=USURPER_PHASE_MENU)usurper_audio_event(US_AUDIO_HIT);
    /* BaseEnemy._on_hurt applies ward before HealthComponent.take_damage.
     * Keep the ceiling integer for existing HUD/users and retain the deficit. */
    if(enemy_ward_left[index]>0)amount*=1.0-0.3;
    double health=current_enemy_health(index)-amount;
    if(health<0)health=0;
    enemy->hp=(uint16_t)health;
    if(enemy->hp<health)++enemy->hp;
    enemy_damage_fraction[index]=enemy->hp-health;
    enemy->flash=7;
    if(enemy->species==SPECIES_WARDEN)us_warden_status_hit(&warden_status[index]);
    if(!enemy->hp)complete_kill(index);
}
static void damage_enemy(unsigned index,double amount){
    if(!enemies[index].invincible)damage_enemy_received(index,amount);
}

static void reserve_capture_target(unsigned index) {
    for (unsigned i = 0; i < ENEMY_MAX; ++i)
        enemies[i].capture_reserved = 0;
    if (index < ENEMY_MAX && enemies[index].active)
        enemies[index].capture_reserved = 1;
}

static void join_captured_enemy(unsigned index,unsigned summoned) {
    if (index >= ENEMY_MAX || !enemies[index].active || unit_count >= tech_capacity() || unit_count >= UNIT_MAX) return;
    Enemy *enemy = &enemies[index];
    if (!enemy_capture_eligible(enemy) || enemy->capture_q8 < 256u ||
        !enemy_is_in_capture_range(enemy)) return;
    unsigned species = enemy->species;
    enemy->capture_reserved = 0;
    if (!enemy->invincible) award_drop(enemy, 1u);
    unsigned slot = unit_count++;
    unit_species[slot] = (uint8_t)species;
    unit_hull_scale[slot]=species==SPECIES_WARDEN?warden_status[index].scale:1;
    unit_instance_id[slot] = ++next_unit_instance_id;
    unit_attack_tick[slot] = 0u;
    unit_support_clock[slot] = 0;
    unit_burst_left[slot] = unit_burst_delay[slot] = 0u;
    unit_facing_q8[slot] = unit_prev_facing_q8[slot] =
        (species == SPECIES_GUNNER ? angle_to(player_x - enemy->x, player_y - enemy->y) : player_angle) * Q8;
    unit_join_ticks[slot] = 0u;
    unit_join_dx[slot] = enemy->x - player_x;
    unit_join_dy[slot] = enemy->y - player_y;
    unit_bearing_q8[slot] = (orbit_phase_q8 + slot * 360u * 256u / unit_count) % (360u * 256u);
    unit_prev_bearing_q8[slot] = unit_bearing_q8[slot];
    unit_prev_x[slot] = enemy->x;
    unit_prev_y[slot] = enemy->y;
    capture_flash_ticks = 18u;
    capture_flash_x = enemy->x;
    capture_flash_y = enemy->y;
    if (phase == USURPER_PHASE_MENU) menu_demo_release_tick = 0u;
    enemy->active = 0;
    if(!summoned){++captured_total;if(phase!=USURPER_PHASE_MENU)usurper_audio_event(US_AUDIO_CAPTURE);}
    manual_target_index = aim_capture_target_index = 0xffffffffu;
    message("USURPED UNIT JOINED THE FLEET");
}

static void complete_capture(unsigned index) { join_captured_enemy(index,0u); }

static void spawn_starting_unit(void) {
    unsigned species=selected_loadout.starting_species;
    if(starting_unit_spawned || phase!=USURPER_PHASE_BATTLE || defeat_pending ||
       !us_loadout_species_unlocked(species,completed_stage_mask) ||
       unit_count>=tech_capacity() || unit_count>=UNIT_MAX)return;
    int index=spawn_enemy(species,player_x,player_y,1);
    if(index<0)return;
    Enemy *hull=&enemies[index];
    hull->supply=1;hull->supply_marker=0;
    hull->hp=hull->max_hp=(uint16_t)us_supplies_hull_health(species);
    hull->capture_q8=256;
    unsigned before=unit_count;
    join_captured_enemy((unsigned)index,1u);
    if(unit_count>before)starting_unit_spawned=1;
    else hull->active=0;
}

static int spawn_enemy_projectile(int32_t x, int32_t y, unsigned angle,
                                   unsigned damage, unsigned speed) {
    int index = alloc_projectile();
    if (index < 0) return -1;
    int dx, dy;
    direction_q8(angle, &dx, &dy);
    projectiles[index] = (Projectile){.x = x, .y = y, .prev_x = x, .prev_y = y,
        .vx = dx * (int)speed,
        .vy = dy * (int)speed, .lifetime = 360u, .active = 1,
        .kind = SHOT_ENEMY, .damage = (uint8_t)damage, .emitter_index = ENEMY_MAX};
    return index;
}

static int spawn_ally_projectile(int32_t x, int32_t y, unsigned angle,
                                 unsigned damage, unsigned speed, unsigned active) {
    int index = alloc_projectile();
    if (index < 0) return -1;
    if(phase!=USURPER_PHASE_MENU)usurper_audio_event(US_AUDIO_SHOT);
    int dx, dy;
    direction_q8(angle, &dx, &dy);
    projectiles[index] = (Projectile){.x = x, .y = y, .prev_x = x, .prev_y = y,
        .vx = dx * (int)speed,
        .vy = dy * (int)speed, .lifetime = (520u * 60u + speed - 1u) / speed, .active = 1,
        .kind = SHOT_GUNNER, .damage = (uint8_t)damage, .pierce = 1u, .burst = (uint8_t)active};
    if (tech_effect("J1")) {
        for (unsigned i = 0; i < unit_count; ++i)
            if (unit_species[i] == SPECIES_DART) projectiles[index].pierce = (uint8_t)(2u + tech_effect("G6"));
    }
    return index;
}

static unsigned tech_damage_q8_roster(unsigned base_q100, unsigned species, unsigned active, unsigned omit_front) {
    static const char families[14] = {'\0','D','S','A','G','W','P','B','L','E','M','V','K','X'};
    char family = species < 14u ? families[species] : '\0';
    char id[3] = {family, '1', '\0'};
    unsigned bonus = 12u * tech_effect("F2") + (active ? 15u * tech_effect("F3") : 0u);
    if (family && family != 'W')
        bonus += tech_effect(id) * (family == 'A' || family == 'E' || family == 'K' ? 25u : family == 'X' ? 20u : 15u);
    if (family == 'S' && tech_effect("S1")) bonus += 15u;
    if (tech_effect("F8")) {
        unsigned kinds = 0u, count = 0u;
        for (unsigned i = omit_front ? 1u : 0u; i < unit_count; ++i) kinds |= 1u << unit_species[i];
        while (kinds) { count += kinds & 1u; kinds >>= 1; }
        if (count >= 3u) bonus += 12u;
    }
    unsigned damage = base_q100 * (100u + min_u(130u, bonus)) * Q8 / 10000u;
    if (tech_effect("F10")) damage = damage * 5u / 4u;
    if (tech_effect("ZDM")) damage = damage * 3u / 2u;
    return damage;
}

static unsigned tech_damage_q8(unsigned base_q100, unsigned species, unsigned active) {
    return tech_damage_q8_roster(base_q100, species, active, active);
}

static unsigned gunner_damage(unsigned base_q100, unsigned active) {
    return (tech_damage_q8(base_q100, SPECIES_GUNNER, active) + Q8 / 2u) / Q8;
}

static int controller_target_position(unsigned local,int32_t *x,int32_t *y){
    if(local<6){const us_controller_nest_t *n=&controller_boss.nests[local];if(!n->active)return 0;*x=(int32_t)(n->x*Q8);*y=(int32_t)(n->y*Q8);return 1;}
    if(local<18){const us_controller_fighter_t *f=&controller_boss.fighters[local-6];if(!f->present||f->queued)return 0;*x=(int32_t)(f->x*Q8);*y=(int32_t)(f->y*Q8);return 1;}
    if(local<24){const us_heavy_bomb_t *b=&controller_bombs[local-18];if(!b->active)return 0;*x=(int32_t)(b->x*Q8);*y=(int32_t)(b->y*Q8);return 1;}
    return 0;
}
static uint32_t controller_target_serial(unsigned target){
    if(target==BOSS_TARGET_CORE)return 1;
    unsigned local=target-ENEMY_MAX;
    return local<6?controller_boss.nests[local].serial:local<18?controller_boss.fighters[local-6].serial:local<24?controller_bombs[local-18].serial:0;
}
static unsigned controller_target_radius(unsigned target){
    if(target==BOSS_TARGET_CORE)return 23u*Q8;
    unsigned local=target-ENEMY_MAX;
    return local<6?(unsigned)(20*Q8*(1+controller_boss.nests[local].hit_kick*0.1)):local<18?(unsigned)(5.5*Q8*0.65*(1+controller_boss.fighters[local-6].hit_kick*0.1)):2u*Q8;
}
/* Three lock offsets are pure functions of the exact float pose. Repeated
 * sweeps/projectiles may query them many times between pose changes. Presence
 * and phase remain live checks, so a destroyed/reformed lock is never revived
 * by this derived-position cache. No float operations or rounding are changed. */
static unsigned mechanic_target_position_cached(unsigned target, int32_t *x, int32_t *y) {
    if (!us_mechanic_target_present(&mechanic_boss, target)) return 0;
    if (target == US_MECHANIC_CORE) { *x=mechanic_boss.x; *y=mechanic_boss.y; return 1; }
    static uint32_t cached_pose[3];
    static int32_t cached_x[3],cached_y[3];
    static unsigned valid;
    uint32_t pose[3];
    memcpy(&pose[0],&mechanic_boss.x_float,4);
    memcpy(&pose[1],&mechanic_boss.y_float,4);
    memcpy(&pose[2],&mechanic_boss.rotation,4);
    if (pose[0]!=cached_pose[0] || pose[1]!=cached_pose[1] || pose[2]!=cached_pose[2]) {
        memcpy(cached_pose,pose,sizeof pose);valid=0;
    }
    unsigned index=target%3u,bit=1u<<index;
    if (!(valid&bit)) {
        if (!us_mechanic_target_position(&mechanic_boss,target,&cached_x[index],&cached_y[index])) return 0;
        valid|=bit;
    }
    *x=cached_x[index];*y=cached_y[index];return 1;
}

/* Presence is checked on every query. Cache only the exact float-to-Q16
 * conversion, which projectiles repeatedly request between turret movement. */
static unsigned archon_target_position_cached(unsigned target,int32_t *x,int32_t *y) {
    if(!us_archon_target_open(&archon_boss,target))return 0;
    if(target==US_ARCHON_CORE){*x=archon_boss.x;*y=archon_boss.y;return 1;}
    static struct {uint32_t x,y,valid;int32_t px,py;} cache[US_ARCHON_PARTS];
    uint32_t kx,ky;memcpy(&kx,&archon_boss.parts[target].x,4);memcpy(&ky,&archon_boss.parts[target].y,4);
    if(!cache[target].valid||cache[target].x!=kx||cache[target].y!=ky) {
        if(!us_archon_target_position(&archon_boss,target,&cache[target].px,&cache[target].py))return 0;
        cache[target].x=kx;cache[target].y=ky;cache[target].valid=1;
    }
    *x=cache[target].px;*y=cache[target].py;return 1;
}

static int attack_target_position(unsigned target, int32_t *x, int32_t *y) {
    if (target < ENEMY_MAX) {
        if (!enemies[target].active||enemies[target].supply) return 0;
        *x = enemies[target].x; *y = enemies[target].y;
        return 1;
    }
    if (!boss_active || target > BOSS_TARGET_CORE) return 0;
    if(campaign_stage_id==7u){
        if(target==BOSS_TARGET_CORE){if(boss_grace_ticks)return 0;*x=boss_x;*y=boss_y;return 1;}
        return controller_target_position(target-ENEMY_MAX,x,y);
    }
    if(campaign_stage_id==6u) {
        if(target==BOSS_TARGET_CORE&&boss_grace_ticks)return 0;
        unsigned local=target==BOSS_TARGET_CORE?US_HANGAR_CORE:target-ENEMY_MAX;
        if(target!=BOSS_TARGET_CORE&&local>=US_HANGAR_PARTS)return 0;
        if(local<US_HANGAR_PARTS&&!us_hangar_target_open(&hangar_boss,local))return 0;
        int32_t tx,ty;if(!us_hangar_target_position(&hangar_boss,local,&tx,&ty))return 0;
        *x=tx/256;*y=ty/256;return 1;
    }
    if (campaign_stage_id == 5u) {
        if (boss_grace_ticks) return 0;
        if (target == BOSS_TARGET_CORE) { *x = 400 * Q8; *y = 240 * Q8; return 1; }
        unsigned local = target - ENEMY_MAX;
        if (local >= US_BOUNDARY_PARTS || !boundary_boss.parts[local].active) return 0;
        *x = (int32_t)(boundary_boss.parts[local].x * Q8); *y = (int32_t)(boundary_boss.parts[local].y * Q8); return 1;
    }
    if (campaign_stage_id == 4u) {
        int32_t tx, ty;
        if (boss_grace_ticks) return 0;
        unsigned local = target == BOSS_TARGET_CORE ? US_TWIN_BOMBER : target - ENEMY_MAX;
        if (target != BOSS_TARGET_CORE && local >= 7u && local < 7u + US_TWIN_BOMBS) {
            const us_twin_bomb_t *bomb = &twin_boss.bombs[local - 7u];
            if (!bomb->active) return 0;
            *x = (int32_t)(bomb->x * Q8); *y = (int32_t)(bomb->y * Q8); return 1;
        }
        if (target != BOSS_TARGET_CORE && local >= 7u) return 0;
        if (local < US_TWIN_PARTS && !us_twin_target_open(&twin_boss, local)) return 0;
        if (!us_twin_target_position(&twin_boss, local, &tx, &ty)) return 0;
        *x = tx / 256; *y = ty / 256; return 1;
    }
    if (campaign_stage_id == 3u) {
        if (target != BOSS_TARGET_CORE || boss_grace_ticks) return 0;
        *x = boss_x; *y = boss_y; return 1;
    }
    if (campaign_stage_id == 1u) {
        int32_t tx, ty;
        if (target != BOSS_TARGET_CORE && target - ENEMY_MAX >= US_MECHANIC_PARTS) return 0;
        unsigned local = target == BOSS_TARGET_CORE ? US_MECHANIC_CORE : target - ENEMY_MAX;
        if (target == BOSS_TARGET_CORE && boss_grace_ticks) return 0;
        if (!mechanic_target_position_cached(local, &tx, &ty)) return 0;
        *x = tx / 256; *y = ty / 256; return 1;
    }
    if (campaign_stage_id == 2u) {
        int32_t tx, ty;
        if (target != BOSS_TARGET_CORE && target - ENEMY_MAX >= US_ARCHON_PARTS) return 0;
        if (target == BOSS_TARGET_CORE && boss_grace_ticks) return 0;
        if (!archon_target_position_cached(target == BOSS_TARGET_CORE ? US_ARCHON_CORE : target - ENEMY_MAX, &tx, &ty)) return 0;
        *x = tx / 256; *y = ty / 256; return 1;
    }
    *x = boss_x; *y = boss_y;
    if (target == BOSS_TARGET_CORE) return 1;
    unsigned plate = target - ENEMY_MAX, row = plate / 7u, col = plate % 7u;
    if(plate>=BOSS_PLATE_COUNT)return 0;
    if (boss_reforming_ticks || !armor_alive[row][col]) return 0;
    /* ArmorBoss uses (column - 3) * 0.34 radians, not rounded offsets. */
    int angle_q16 = (int)(boss_angle * 65536u) + ((int)col - 3) * 1276678;
    angle_q16 = (angle_q16 + 360 * 65536) % (360 * 65536);
    unsigned degree = (unsigned)angle_q16 / 65536u, fraction = (unsigned)angle_q16 % 65536u;
    int cx = usurper_sin_q8[(degree + 90u) % 360u], cy = usurper_sin_q8[degree];
    int nx = usurper_sin_q8[(degree + 91u) % 360u], ny = usurper_sin_q8[(degree + 1u) % 360u];
    int radius = 58 - (int)row * 14;
    *x += (int32_t)((int64_t)(cx * 65536 + (nx - cx) * (int)fraction) * radius / 65536);
    *y += (int32_t)((int64_t)(cy * 65536 + (ny - cy) * (int)fraction) * radius / 65536);
    return 1;
}

static int attack_target_selectable(unsigned target) {
    if (target < ENEMY_MAX) return enemies[target].active && !enemies[target].supply && !enemies[target].capture_reserved;
    if(campaign_stage_id==7u){int32_t x,y;return boss_active&&target<=BOSS_TARGET_CORE&&(target!=BOSS_TARGET_CORE||!boss_grace_ticks)&&attack_target_position(target,&x,&y);}
    if(campaign_stage_id==6u) {
        if(!boss_active||(target==BOSS_TARGET_CORE&&boss_grace_ticks))return 0;
        unsigned local=target==BOSS_TARGET_CORE?US_HANGAR_CORE:target-ENEMY_MAX;
        if(target!=BOSS_TARGET_CORE&&local>=US_HANGAR_PARTS)return 0;
        return us_hangar_target_open(&hangar_boss,local);
    }
    if (campaign_stage_id == 5u) {
        if (!boss_active || boss_grace_ticks) return 0;
        return target == BOSS_TARGET_CORE ? boundary_boss.exposed : target - ENEMY_MAX < US_BOUNDARY_PARTS && boundary_boss.parts[target - ENEMY_MAX].active;
    }
    if (campaign_stage_id == 4u) {
        if (target != BOSS_TARGET_CORE && target - ENEMY_MAX >= 7u) return 0;
        return boss_active && !boss_grace_ticks && us_twin_target_open(&twin_boss,
            target == BOSS_TARGET_CORE ? US_TWIN_BOMBER : target - ENEMY_MAX);
    }
    if (campaign_stage_id == 3u) return boss_active && !boss_grace_ticks && target == BOSS_TARGET_CORE;
    if (campaign_stage_id == 1u) {
        if (target != BOSS_TARGET_CORE && target - ENEMY_MAX >= US_MECHANIC_PARTS) return 0;
        return boss_active && (!boss_grace_ticks || target != BOSS_TARGET_CORE) &&
            us_mechanic_target_open(&mechanic_boss, target == BOSS_TARGET_CORE ? US_MECHANIC_CORE : target - ENEMY_MAX);
    }
    if (campaign_stage_id == 2u) {
        if (target != BOSS_TARGET_CORE && target - ENEMY_MAX >= US_ARCHON_PARTS) return 0;
        return boss_active && (!boss_grace_ticks || target != BOSS_TARGET_CORE) &&
            us_archon_target_open(&archon_boss, target == BOSS_TARGET_CORE ? US_ARCHON_CORE : target - ENEMY_MAX);
    }
    if (target == BOSS_TARGET_CORE) return boss_active && !boss_armor_left;
    unsigned plate = target - ENEMY_MAX, row = plate / 7u, col = plate % 7u;
    if (plate>=BOSS_PLATE_COUNT || !boss_active || boss_reforming_ticks || !armor_alive[row][col]) return 0;
    for (unsigned outer = 0; outer < row; ++outer) if (armor_alive[outer][col]) return 0;
    return 1;
}

static void attack_target_velocity(unsigned target, int32_t *vx, int32_t *vy) {
    if (target < ENEMY_MAX) { *vx = enemies[target].vx; *vy = enemies[target].vy; }
    else if (campaign_stage_id == 4u) {
        const us_twin_actor_t *actor = &twin_boss.actors[target == BOSS_TARGET_CORE ? 1u : 0u];
        *vx = (int32_t)(actor->vx * Q8); *vy = (int32_t)(actor->vy * Q8);
    }
    else if (target == BOSS_TARGET_CORE) { *vx = boss_vx; *vy = boss_vy; }
    else { *vx = 0; *vy = 0; }
}

static int nearest_gunner_target(int32_t x, int32_t y) {
    int target = -1;
    unsigned nearest = 500u * Q8;
    for (unsigned i = 0; i < ATTACK_TARGET_MAX; ++i) {
        int32_t tx, ty;
        if (!attack_target_selectable(i) || !attack_target_position(i, &tx, &ty)) continue;
        /* Strict distance<nearest is floor(distance)<=nearest-1. */
        if(!nearest||us_logic_box_excludes(tx,ty,x,y,nearest-1u))continue;
        unsigned distance = distance_q8(tx, ty, x, y);
        if (distance < nearest) { nearest = distance; target = (int)i; }
    }
    return target;
}

static unsigned gunner_direction(int32_t x, int32_t y, unsigned target) {
    int32_t tx, ty, vx, vy;
    if (!attack_target_position(target, &tx, &ty)) return player_angle;
    attack_target_velocity(target, &vx, &vy);
    int32_t aim_x = tx, aim_y = ty;
    for (unsigned iteration = 0; iteration < 3u; ++iteration) {
        unsigned distance = distance_q8(x, y, aim_x, aim_y);
        aim_x = tx + (int32_t)((int64_t)vx * distance / (420u * Q8));
        aim_y = ty + (int32_t)((int64_t)vy * distance / (420u * Q8));
    }
    return angle_to(aim_x - x, aim_y - y);
}

static int gunner_target_point(int32_t *x, int32_t *y) {
    if (phase == USURPER_PHASE_MENU) {
        int victim = nearest_gunner_target(player_x, player_y);
        if (victim < 0) return -1;
        int32_t tx, ty, vx, vy;
        if (!attack_target_position((unsigned)victim, &tx, &ty)) return -1;
        attack_target_velocity((unsigned)victim, &vx, &vy);
        int dx, dy;
        direction_q8(angle_to(tx - player_x, ty - player_y), &dx, &dy);
        int32_t origin_x = player_x + dx * UNIT_ORBIT_RADIUS;
        int32_t origin_y = player_y + dy * UNIT_ORBIT_RADIUS;
        *x = tx; *y = ty;
        /* GrowthPilot.shot_aim: intercept from the orbit mount at speed 580,
         * then lead another 0.25 s. Turning/flight heading is not attack aim. */
        for (unsigned iteration = 0; iteration < 3u; ++iteration) {
            unsigned travel_q8 = min_u(3u * Q8 / 4u, distance_q8(origin_x, origin_y, *x, *y) / 580u);
            *x = tx + (int32_t)((int64_t)vx * travel_q8 / Q8);
            *y = ty + (int32_t)((int64_t)vy * travel_q8 / Q8);
        }
        *x += vx / 4; *y += vy / 4;
    } else if (pointer_visible) {
        *x = (0?pointer_x*25*Q8/32:(render_camera_x + (pointer_x * GAME_W / (int)USURPER_GPU_WIDTH - GAME_W / 2) * Q8));
        *y = (0?pointer_y*5*Q8/6:(render_camera_y + (pointer_y * GAME_H / (int)USURPER_GPU_HEIGHT - GAME_H / 2) * Q8));
    } else {
        int dx, dy;
        direction_q8(player_angle, &dx, &dy);
        *x = player_x + dx * 180;
        *y = player_y + dy * 180;
    }
    unsigned reach = distance_q8(player_x, player_y, *x, *y);
    if (reach > 480u * Q8) {
        *x = player_x + (int32_t)((int64_t)(*x - player_x) * 480u * Q8 / reach);
        *y = player_y + (int32_t)((int64_t)(*y - player_y) * 480u * Q8 / reach);
    }
    *x = clamp_i(*x, 20 * Q8, (FIELD_W - 20) * Q8);
    *y = clamp_i(*y, 20 * Q8, (FIELD_H - 20) * Q8);
    return 0;
}

static int schedule_gunner_release(void) {
    for (unsigned i = 0; i < GUNNER_VOLLEY_MAX; ++i) {
        GunnerVolley *volley = &gunner_volleys[i];
        if (volley->rounds || volley->reticle_q8) continue;
        if (gunner_target_point(&volley->target_x, &volley->target_y)) return -1;
        volley->rounds = 5u + 2u * tech_effect("G3") + 2u * tech_effect("G7");
        volley->cadence_q8 = 15u * Q8 - min_u(tech_effect("G5"), 4u) * 3u * Q8 / 2u;
        volley->delay_q8 = 0;
        volley->reticle_q8 = volley->cadence_q8 * (volley->rounds - 1u) + 12u * Q8;
        return 0;
    }
    return -1;
}

static void gunner_release_step(void) {
    for (unsigned i = 0; i < GUNNER_VOLLEY_MAX; ++i) {
        GunnerVolley *volley = &gunner_volleys[i];
        if (volley->reticle_q8) volley->reticle_q8 -= min_u(Q8, volley->reticle_q8);
        if (!volley->rounds) continue;
        volley->delay_q8 -= Q8;
        if (volley->delay_q8 > 0) continue;
        if (volley->target_x != player_x || volley->target_y != player_y) {
            unsigned angle = angle_to(volley->target_x - player_x, volley->target_y - player_y);
            if (spawn_ally_projectile(player_x, player_y, angle, gunner_damage(1800u, 1u), 580u, 1u) < 0) {
                volley->delay_q8 = 0;
                volley->reticle_q8 += Q8;
                continue;
            }
        }
        --volley->rounds;
        volley->delay_q8 += (int32_t)volley->cadence_q8;
    }
}

typedef struct {
    uint8_t species, join, burst_left, burst_delay;
    uint16_t attack;
    int32_t join_x, join_y, prev_x, prev_y;
    uint32_t bearing, prev_bearing, facing, prev_facing;
    uint32_t instance_id;
    double support_clock;
    float hull_scale;
} UnitSnapshot;

static UnitSnapshot save_unit(unsigned i) {
    return (UnitSnapshot){unit_species[i], unit_join_ticks[i], unit_burst_left[i], unit_burst_delay[i],
        unit_attack_tick[i], unit_join_dx[i], unit_join_dy[i], unit_prev_x[i], unit_prev_y[i],
        unit_bearing_q8[i], unit_prev_bearing_q8[i], unit_facing_q8[i], unit_prev_facing_q8[i], unit_instance_id[i],unit_support_clock[i],unit_hull_scale[i]};
}

static void restore_unit(unsigned i, UnitSnapshot unit) {
    unit_species[i] = unit.species; unit_join_ticks[i] = unit.join;
    unit_burst_left[i] = unit.burst_left; unit_burst_delay[i] = unit.burst_delay;
    unit_attack_tick[i] = unit.attack; unit_join_dx[i] = unit.join_x; unit_join_dy[i] = unit.join_y;
    unit_prev_x[i] = unit.prev_x; unit_prev_y[i] = unit.prev_y;
    unit_bearing_q8[i] = unit.bearing; unit_prev_bearing_q8[i] = unit.prev_bearing;
    unit_facing_q8[i] = unit.facing; unit_prev_facing_q8[i] = unit.prev_facing;
    unit_instance_id[i] = unit.instance_id;
    unit_support_clock[i] = unit.support_clock;
    unit_hull_scale[i] = unit.hull_scale;
}

static void boundary_bind_units(void) {
    us_boundary_flush_deferred(&boundary_boss);
    unsigned was_active[US_BOUNDARY_UNITS];
    for (unsigned i = 0; i < US_BOUNDARY_UNITS; ++i) {
        was_active[i] = boundary_boss.units[i].active; boundary_boss.units[i].active = 0;
    }
    for (unsigned index = 0; index < unit_count; ++index) {
        if (!unit_instance_id[index]) unit_instance_id[index] = ++next_unit_instance_id;
        unsigned slot = US_BOUNDARY_UNITS;
        for (unsigned i = 0; i < US_BOUNDARY_UNITS; ++i)
            if (boundary_boss.units[i].valid && boundary_boss.units[i].id == unit_instance_id[index]) { slot = i; break; }
        if (slot == US_BOUNDARY_UNITS)
            for (unsigned i = 0; i < US_BOUNDARY_UNITS; ++i) if (!boundary_boss.units[i].valid) { slot = i; break; }
        if (slot == US_BOUNDARY_UNITS) continue;
        us_boundary_unit_t *unit = &boundary_boss.units[slot];
        if (unit->id != unit_instance_id[index])
            for (unsigned side = 0; side < 4u; ++side)
                if (boundary_boss.parts[side * 3u].tracked_unit == (int)slot) boundary_boss.parts[side * 3u].tracked_unit = -1;
        int32_t x, y; unit_world_position(index, &x, &y);
        unit->x = x / 256.0f; unit->y = y / 256.0f; unit->id = unit_instance_id[index];
        unit->order = (uint8_t)index; unit->active = unit->valid = 1; unit->pending_free = 0;
        boundary_unit_species[slot] = unit_species[index]; boundary_unit_facing[slot] = unit_facing_q8[index];
    }
    for (unsigned i = 0; i < US_BOUNDARY_UNITS; ++i) {
        us_boundary_unit_t *unit = &boundary_boss.units[i];
        unsigned captive = 0;
        for (unsigned side = 0; side < 4u; ++side) if (boundary_boss.parts[side * 3u].captive == (int)i) captive = 1;
        if (unit->valid && !unit->active && unit->id == rammer_body_instance_id && rammer_charge.remaining_q16) {
            unit->x = player_x / 256.0f; unit->y = player_y / 256.0f;
        } else if (unit->valid && !unit->active && !captive && (was_active[i] || unit->id == rammer_body_instance_id)) unit->pending_free = 1;
    }
}
static void boundary_remove_grabbed_units(void) {
    for (unsigned slot = 0; slot < US_BOUNDARY_UNITS; ++slot) {
        const us_boundary_unit_t *unit = &boundary_boss.units[slot];
        if (unit->active || !unit->id) continue;
        for (unsigned index = 0; index < unit_count; ++index) if (unit_instance_id[index] == unit->id) {
            if (!index) cancel_combat_aim();
            for (unsigned i = index; i + 1u < unit_count; ++i) restore_unit(i, save_unit(i + 1u));
            --unit_count; break;
        }
    }
}
static void boundary_confine_enemies(void) {
    float rect[4]; us_boundary_safe_rect(&boundary_boss, rect);
    for (unsigned i = 0; i < ENEMY_MAX; ++i) if (enemies[i].active) {
        enemies[i].x = clamp_i(enemies[i].x, (int32_t)((rect[0] + 14) * Q8), (int32_t)((rect[2] - 14) * Q8));
        enemies[i].y = clamp_i(enemies[i].y, (int32_t)((rect[1] + 14) * Q8), (int32_t)((rect[3] - 14) * Q8));
    }
}

static int spawn_dart_projectile(unsigned angle, unsigned damage, unsigned pierce, unsigned rail) {
    int index = alloc_projectile();
    if (index < 0) return -1;
    if(phase!=USURPER_PHASE_MENU)usurper_audio_event(rail?US_AUDIO_RAIL:US_AUDIO_SHOT);
    int dx, dy;
    direction_q8(angle, &dx, &dy);
    unsigned speed = 500u;
    int d2 = tech_index("D2");
    if (d2 >= 0) {
        static const unsigned speed_by_rank[3] = {500u, 650u, 800u};
        unsigned rank = tech_effective_rank((unsigned)d2);
        speed = speed_by_rank[rank > 2u ? 2u : rank];
    }
    projectiles[index] = (Projectile){.x = player_x,
        .y = player_y, .prev_x = player_x, .prev_y = player_y, .vx = dx * (int)speed,
        .vy = dy * (int)speed, .lifetime = 360u,
        .active = 1, .kind = SHOT_DART, .damage = (uint8_t)damage,
        .pierce = (uint8_t)pierce, .rail = (uint8_t)rail, .hull = 1u,
        .remaining_q8 = (tech_effect("ZD") ? speed * 6u : 400u) * Q8};
    return index;
}

static unsigned skimmer_active_sweeps(void) {
    unsigned count = 0u;
    for (unsigned i = 0; i < SKIMMER_SWEEP_MAX; ++i) count += skimmer_sweeps[i].ticks_left != 0u;
    return count;
}

static unsigned skimmer_damage_q8(void) {
    return tech_damage_q8((10u + 2u * tech_effect("S4")) * 100u, SPECIES_SKIMMER, 1u);
}

static int schedule_skimmer_release(void) {
    unsigned needed = tech_effect("ZS") ? 2u : 1u;
    unsigned free_slots[2], count = 0u;
    for (unsigned i = 0; i < SKIMMER_SWEEP_MAX && count < needed; ++i)
        if (!skimmer_sweeps[i].ticks_left) free_slots[count++] = i;
    if (count != needed) return -1;
    unsigned omega_q8 = 1152u + tech_effect("S3") * Q8;
    unsigned step_q16 = (unsigned)((uint64_t)omega_q8 * 3754936u / (60u * Q8));
    unsigned duration = (4u + tech_effect("S2")) * 60u;
    unsigned damage = skimmer_damage_q8();
    for (unsigned i = 0; i < needed; ++i) {
        SkimmerSweep *scan = &skimmer_sweeps[free_slots[i]];
        memset(scan, 0, sizeof(*scan));
        scan->start_q16 = ((player_angle + i * 180u) % 360u) * 65536u;
        scan->angle_q16 = scan->start_q16;
        scan->step_q16 = step_q16;
        scan->radius = (uint16_t)tech_capture_radius();
        scan->ticks_left = (uint16_t)duration;
        scan->damage_q8 = (uint16_t)damage;
        scan->echo = tech_effect("S8") != 0u;
        /* Original repeat time is TAU / angular_speed - 0.001 seconds. */
        scan->repeat_ticks = (uint16_t)((360u * 65536u + step_q16 - 1u - step_q16 * 6u / 100u) / step_q16);
    }
    return 0;
}

static void release_selected(void) {
    if (!unit_count || release_cooldown_ticks || rammer_charge.remaining_q16 ||
        (phase != USURPER_PHASE_MENU && !gameplay_input_allowed())) return;
    unsigned species = unit_species[0];
    /* Only these five active abilities are implemented in this slice.
     * Placeholder hulls must never be consumed without starting an ability. */
    if (species != SPECIES_DART && species != SPECIES_SKIMMER && species != SPECIES_GUNNER && species != SPECIES_RAMMER && species!=SPECIES_WARDEN) return;
    int dx, dy;
    direction_q8(player_angle, &dx, &dy);
    if(species==SPECIES_WARDEN){
        us_warden_ranks_t ranks={.w2=tech_effect("W2"),.w4=tech_effect("W4"),.w6=tech_effect("W6"),.w7=tech_effect("W7"),.w8=tech_effect("W8"),.zw=tech_effect("ZW")};
        us_warden_deploy(&warden_field,player_x/256.0f,player_y/256.0f,&ranks);
    }else if (species == SPECIES_DART) {
        int d4 = tech_index("D4"), d6 = tech_index("D6");
        unsigned pierce = d4 >= 0 && tech_rank((unsigned)d4) ? 2u : 1u;
        unsigned rail = d6 >= 0 && tech_rank((unsigned)d6);
        unsigned damage = (tech_damage_q8(3600u, SPECIES_DART, 1u) + Q8 / 2u) / Q8;
        if (spawn_dart_projectile(player_angle, damage, rail ? 255u : pierce, rail) < 0) return;
        release_flash_ticks = 12u;
    } else if (species == SPECIES_SKIMMER) {
        if (schedule_skimmer_release()) return;
        release_flash_ticks = 12u;
    } else if (species == SPECIES_GUNNER) {
        if (schedule_gunner_release()) return;
    } else if (species == SPECIES_RAMMER) {
        rammer_body_instance_id = unit_instance_id[0];
        memset(&rammer_charge, 0, sizeof(rammer_charge));
        /* Research.dash_duration: 0.3 + 0.08*A2 + 0.12*charge seconds.
         * Keep fractional ticks so a charged dash does not round its travel. */
        rammer_charge.duration_q16 = 18u * 65536u + 24u * 65536u * tech_effect("A2") / 5u +
            (uint32_t)((uint64_t)36u * release_charge_q16 / 5u);
        if (tech_effect("ZA")) rammer_charge.duration_q16 *= 2u;
        rammer_charge.remaining_q16 = rammer_charge.duration_q16;
        rammer_charge.direction_x = dx; rammer_charge.direction_y = dy;
        rammer_charge.angle = player_angle;
        player_vx = player_vy = 0;
        if (tech_effect("A7")) player_invincible_ticks = max_u(player_invincible_ticks,
            (rammer_charge.duration_q16 + 65535u) / 65536u);
    }
    for (unsigned i = 0; i + 1u < unit_count; ++i) restore_unit(i, save_unit(i + 1u));
    --unit_count;
    release_cooldown_ticks = (192u - min_u(tech_effect("F5"), 6u) * 30u + 9u) / 10u;
    ++released_total;
    if(phase!=USURPER_PHASE_MENU)usurper_audio_event(US_AUDIO_RELEASE);
    if (phase == USURPER_PHASE_MENU) menu_demo_shown_species_mask |= 1u << species;
    if (phase == USURPER_PHASE_TUTORIAL) ++tutorial_releases;
    message("CONTROLLED UNIT ACTIVATED");
}

static void cycle_unit(int direction) {
    if (unit_count < 2u || aim_is_held || rammer_charge.remaining_q16) return;
    if (direction > 0) {
        UnitSnapshot first = save_unit(0u);
        for (unsigned i = 0; i + 1u < unit_count; ++i) restore_unit(i, save_unit(i + 1u));
        restore_unit(unit_count - 1u, first);
    } else {
        UnitSnapshot last = save_unit(unit_count - 1u);
        for (unsigned i = unit_count - 1u; i > 0u; --i) restore_unit(i, save_unit(i - 1u));
        restore_unit(0u, last);
    }
}

static void damage_boss_plate(unsigned column, unsigned row, unsigned rail) {
    if (column >= 7u || row >= 3u || !armor_alive[row][column]) return;
    if (!rail && armor_column_cooldown[column]) return;
    for (unsigned outer = 0; outer < row; ++outer)
        if (armor_alive[outer][column]) return;
    if (rail) {
        for (unsigned r = 0; r < 3u; ++r) {
            if (armor_alive[r][column]) { armor_alive[r][column] = 0; --boss_armor_left; }
        }
    } else {
        armor_alive[row][column] = 0;
        --boss_armor_left;
        armor_column_cooldown[column] = 9u;
    }
    ++boss_hits;
    if (!boss_armor_left) {
        message("ARMOR COLLAPSED / CORE EXPOSED");
    }
}

static void finish_run(unsigned victory) {
    if (phase == USURPER_PHASE_RESULT) return;
    if(phase!=USURPER_PHASE_MENU)usurper_audio_event(victory?US_AUDIO_WIN:US_AUDIO_LOSS);
    us_supplies_stop(&supplies,&supply_callbacks);
    warden_field.active=0;warden_guard_left=0;memset(interceptors,0,sizeof(interceptors));memset(warden_claims,0,sizeof(warden_claims));
    collect_remaining_pickups();
    result_victory = victory;
    last_run_alloy = alloy_run;
    alloy_wallet += alloy_run;
    if (victory) {
        /* Preserve a legacy prefix-only profile while tracking independent
         * first clears now that all eight stages can be chosen directly. */
        if (cleared_sectors) completed_stage_mask |= (1u << min_u(cleared_sectors, 8u)) - 1u;
        alloy_wallet += us_campaign_first_reward(campaign_stage_id, completed_stage_mask);
        if(!(completed_stage_mask&(1u<<campaign_stage_id))&&campaign_stage_id<3u) {
            selected_loadout.form_index=(int32_t)campaign_stage_id;
            if(!selected_loadout.starting_species)selected_loadout.starting_species=us_loadout_form_species(selected_loadout.form_index);
        }
        completed_stage_mask |= 1u << campaign_stage_id;
        cleared_sectors = us_campaign_completed_prefix(completed_stage_mask);
        us_loadout_reward_defaults(&selected_loadout,completed_stage_mask);
    }
    alloy_run = 0;
    boss_active = 0;
    result_ticks = 0;
    result_selection = result_action = 0;
    damage_flash_ticks = 0;
    defeat_pending = defeat_tick = 0;
    memset(gunner_volleys, 0, sizeof(gunner_volleys));
    memset(&rammer_charge, 0, sizeof(rammer_charge));
    phase = USURPER_PHASE_RESULT;
    message(victory ? "SECTOR 1-1 CLEAR / ENTER FOR RESEARCH" : "SORTIE FAILED / ENTER FOR RESEARCH");
}

static void settle_abandoned_run(void) {
    if (phase == USURPER_PHASE_BATTLE || phase == USURPER_PHASE_TUTORIAL ||
        (phase == USURPER_PHASE_PAUSED &&
         (paused_phase == USURPER_PHASE_BATTLE || paused_phase == USURPER_PHASE_TUTORIAL)))
        finish_run(0u);
}

static void boss_expose_damage(unsigned damage) {
    if (boss_armor_left) return;
    unsigned scaled = damage * 5u / 2u;
    if (boss_phase == 1u) {
        unsigned floor_hp = BOSS_BASE_HP / 2u;
        if (boss_hp > floor_hp) boss_hp -= min_u(scaled, boss_hp - floor_hp);
        if (boss_hp <= floor_hp) {
            boss_phase = 2u;
            boss_armor_left = BOSS_PLATE_COUNT;
            memset(armor_alive, 1, sizeof(armor_alive));
            memset(armor_column_cooldown, 0, sizeof(armor_column_cooldown));
            boss_reforming_ticks = 1u;
            for (unsigned i = 0; i <= SKIMMER_SWEEP_MAX; ++i)
                for (unsigned j = ENEMY_MAX; j < BOSS_TARGET_CORE; ++j)
                    skimmer_sweeps[i].hit_tick[j] = skimmer_sweeps[i].echo_hit_tick[j] = 0u;
            for (unsigned i = 0; i < PROJECTILE_MAX; ++i) projectiles[i].boss_hit_mask &= 1u << BOSS_TARGET_SLOTS;
            boss_cycle_ticks = 192u;
            message("PHASE 2 / ARMOR REFORMING");
        }
    } else {
        boss_hp = boss_hp > scaled ? boss_hp - scaled : 0u;
        if (!boss_hp) {
            unsigned reward = 24u * 12000u + alloy_remainder;
            alloy_run += reward / 10000u;
            alloy_remainder = reward % 10000u;
            finish_run(1u);
        }
    }
}

static void sync_mechanic_boss(void) {
    boss_x = mechanic_boss.x / 256; boss_y = mechanic_boss.y / 256;
    boss_vx = mechanic_boss.vx / 256; boss_vy = mechanic_boss.vy / 256;
    boss_angle = (unsigned)mechanic_boss.angle / 65536u;
    boss_hp = (mechanic_boss.hp_q16 + 65535u) / 65536u;
    boss_phase = mechanic_boss.phase + 1u; boss_armor_left = mechanic_boss.remaining;
    boss_hits = mechanic_boss.breaks;
    if (!mechanic_boss.active && boss_active) {
        unsigned value = us_campaign_stage(campaign_stage_id)->reward * 12000u + alloy_remainder;
        alloy_run += value / 10000u; alloy_remainder = value % 10000u;
        finish_run(1u);
    }
}

static void sync_archon_boss(void) {
    boss_x = archon_boss.x / 256; boss_y = archon_boss.y / 256;
    boss_vx = archon_boss.vx / 256; boss_vy = archon_boss.vy / 256;
    boss_angle = (unsigned)archon_boss.angle / 65536u;
    boss_hp = (archon_boss.hp_q16 + 65535u) / 65536u;
    boss_phase = archon_boss.phase + 1u; boss_armor_left = archon_boss.remaining;
    boss_hits = archon_boss.breaks;
    if (!archon_boss.active && boss_active) {
        unsigned value = us_campaign_stage(campaign_stage_id)->reward * 12000u + alloy_remainder;
        alloy_run += value / 10000u; alloy_remainder = value % 10000u;
        finish_run(1u);
    }
}

static void sync_flame_boss(void) {
    boss_x = flame_boss.x / 256; boss_y = flame_boss.y / 256;
    boss_vx = boss_vy = 0; boss_angle = (unsigned)flame_boss.angle / 65536u;
    boss_hp = (flame_boss.hp_q16 + 65535u) / 65536u;
    boss_phase = flame_boss.phase + 1u; boss_armor_left = 0;
    if (!flame_boss.active && boss_active) {
        unsigned value = us_campaign_stage(campaign_stage_id)->reward * 12000u + alloy_remainder;
        alloy_run += value / 10000u; alloy_remainder = value % 10000u; finish_run(1u);
    }
}

static void damage_flame_target(unsigned target, unsigned damage_q8, float source_x, float source_y) {
    if (!boss_active || boss_grace_ticks || target != BOSS_TARGET_CORE) return;
    us_flame_hit(&flame_boss, damage_q8 * 256u, source_x, source_y); sync_flame_boss();
}

static void sync_twin_boss(void) {
    boss_x = (int32_t)(twin_boss.x * Q8); boss_y = (int32_t)(twin_boss.y * Q8);
    boss_vx = boss_vy = 0; boss_hp = (unsigned)(twin_boss.hp + 0.999999);
    boss_phase = twin_boss.phase + 1u; boss_armor_left = twin_boss.remaining; boss_hits = twin_boss.breaks;
    if (!twin_boss.active && boss_active) {
        for (unsigned i = 0; i < US_TWIN_BOMBS; ++i) twin_boss.bombs[i].active = 0;
        unsigned value = us_campaign_stage(campaign_stage_id)->reward * 12000u + alloy_remainder;
        alloy_run += value / 10000u; alloy_remainder = value % 10000u; finish_run(1u);
    }
}

static void sync_boundary_boss(void) {
    boss_x = 400 * Q8; boss_y = 240 * Q8; boss_vx = boss_vy = 0;
    boss_hp = (unsigned)(boundary_boss.hp + 0.999999); boss_phase = boundary_boss.phase + 1u;
    boss_armor_left = boundary_boss.remaining; boss_hits = boundary_boss.breaks;
    if (!boundary_boss.active && boss_active) {
        us_boundary_finish_crush(&boundary_boss); us_boundary_flush_deferred(&boundary_boss);
        unsigned value = us_campaign_stage(campaign_stage_id)->reward * 12000u + alloy_remainder;
        alloy_run += value / 10000u; alloy_remainder = value % 10000u; finish_run(1u);
    }
}

static void sync_hangar_boss(void) {
    boss_x=(int32_t)(hangar_boss.x*Q8);boss_y=(int32_t)(hangar_boss.y*Q8);
    boss_vx=(int32_t)(hangar_boss.vx*Q8);boss_vy=(int32_t)(hangar_boss.vy*Q8);
    boss_hp=(unsigned)(hangar_boss.hp+0.999999);boss_phase=hangar_boss.phase+1u;
    boss_armor_left=hangar_boss.remaining;boss_hits=hangar_boss.breaks;
    if(!hangar_boss.active&&boss_active) {
        unsigned value=us_campaign_stage(campaign_stage_id)->reward*12000u+alloy_remainder;
        alloy_run+=value/10000u;alloy_remainder=value%10000u;finish_run(1u);
    }
}
static void sync_controller_boss(void){
    boss_x=(int32_t)(controller_boss.x*Q8);boss_y=(int32_t)(controller_boss.y*Q8);boss_vx=(int32_t)(controller_boss.vx*Q8);boss_vy=(int32_t)(controller_boss.vy*Q8);
    boss_hp=(unsigned)(controller_boss.hp+0.999999);boss_phase=controller_boss.phase+1u;boss_armor_left=us_controller_living_nests(&controller_boss);
    if(!controller_boss.active&&boss_active){unsigned value=us_campaign_stage(7)->reward*12000u+alloy_remainder;alloy_run+=value/10000u;alloy_remainder=value%10000u;finish_run(1);}
}
static void controller_wave_counts(unsigned *external,unsigned *scheduled,unsigned *carriers){
    *external=*scheduled=*carriers=0;
    for(unsigned i=0;i<ENEMY_MAX;++i)if(enemies[i].active&&!enemies[i].supply&&!enemies[i].expiry_queued){++*external;*carriers+=enemies[i].species==SPECIES_CARRIER;}
    for(unsigned i=0;i<ARRIVAL_MAX;++i)if(arrivals[i].active){*scheduled+=arrivals[i].count;for(unsigned j=0;j<arrivals[i].count;++j)*carriers+=arrivals[i].species[j]==SPECIES_CARRIER;}
}
static void flush_controller_events(void){
    if(campaign_stage_id!=7u||!boss_spawned)return;
    us_controller_finish(&controller_boss,player_x*256,player_y*256,&controller_events);
    if(controller_events.flags&US_CONTROLLER_CLEAR_ARRIVALS)memset(arrivals,0,sizeof(arrivals));
    if(controller_events.flags&US_CONTROLLER_CLEAR_ENEMIES){for(unsigned i=0;i<ENEMY_MAX;++i)if(!enemies[i].supply)enemies[i].active=0;if(aim_is_held&&aim_capture_target_index<ENEMY_MAX&&!enemies[aim_capture_target_index].supply)cancel_combat_aim();}
    if(controller_events.flags&US_CONTROLLER_SUPPLY_NEXT)supplies.next_mechanic=battle_elapsed+1;
    if(controller_events.flags&US_CONTROLLER_CLEAR_SHOTS){for(unsigned i=0;i<PROJECTILE_MAX;++i)if(projectiles[i].kind==SHOT_ENEMY)projectiles[i].active=0;memset(controller_bombs,0,sizeof(controller_bombs));campaign_next_birth=level_tick/60.0+0.25;}
    for(unsigned i=0;i<controller_events.bubble_count;++i)for(int side=-1;side<=1;side+=2){
        us_supplies_spawn(&supply_callbacks,controller_events.bubbles[i].species,controller_events.bubbles[i].x,controller_events.bubbles[i].y+side*14,1);
    }
    if(controller_boss.phase==2){
        for(unsigned i=0;i<ENEMY_MAX;++i)if(enemies[i].active&&enemy_sovereign_wave[i]&&enemy_wave_index[i]<controller_boss.waves.index&&controller_boss.waves.retiring)enemies[i].active=0;
    }sync_controller_boss();controller_events.bubble_count=controller_events.flags=0;
}

static void hangar_spawn_fighters(float x,float y,unsigned count) {
    if(phase!=USURPER_PHASE_BATTLE||!boss_active||!hangar_boss.active)return;
    unsigned total=0,owned=0;
    for(unsigned i=0;i<ENEMY_MAX;++i)if(enemies[i].active&&enemies[i].carrier_offspring&&!enemies[i].expiry_queued) {
        ++total;if(enemies[i].offspring_owner==UINT64_MAX)++owned;
    }
    unsigned slots=us_hangar_fighter_slots(&hangar_boss,total,owned,count);
    for(unsigned i=0;i<slots;++i) {
        int index=spawn_enemy(SPECIES_DART,(int32_t)((x+(i? -14.0f:14.0f))*Q8),(int32_t)(y*Q8),0u);
        if(index<0)break;
        /* Boss offspring are born directly, without a scheduled warp. The
         * source battle clamps them at the next parent physics step. */
        enemies[index].x=enemies[index].prev_x=(int32_t)((x+(i? -14.0f:14.0f))*Q8);
        enemies[index].y=enemies[index].prev_y=(int32_t)(y*Q8);
        us_dart_init(&offspring_motion[index],x+(i? -14.0f:14.0f),y);
        enemies[index].carrier_offspring=1;enemies[index].offspring_owner=UINT64_MAX;
        enemies[index].offspring_expires=level_tick+1200u;
    }
}
static void flush_hangar_fighters(void) {
    for(unsigned i=0;i<hangar_spawn_pending.count;++i)
        hangar_spawn_fighters(hangar_spawn_pending.fighters[i].x,hangar_spawn_pending.fighters[i].y,hangar_spawn_pending.fighters[i].count);
    hangar_spawn_pending.count=0;
}

static void queue_offspring_expiry(void) {
    for(unsigned i=0;i<ENEMY_MAX;++i)if(enemies[i].active&&enemies[i].carrier_offspring&&level_tick>=enemies[i].offspring_expires)enemies[i].expiry_queued=1;
}
static void flush_offspring_expiry(void) {
    for(unsigned i=0;i<ENEMY_MAX;++i)if(enemies[i].active&&enemies[i].expiry_queued) {
        if(aim_is_held&&aim_capture_target_index==i)cancel_combat_aim();
        enemies[i].active=0;
    }
}

static void damage_boss_target(unsigned target, unsigned damage_q8, unsigned rail) {
    if (!boss_active || (target == BOSS_TARGET_CORE && boss_grace_ticks)) return;
    if (campaign_stage_id == 3u) { damage_flame_target(target, damage_q8, player_x / 256.0f, player_y / 256.0f); return; }
    if(campaign_stage_id==7u){
        if(target==BOSS_TARGET_CORE)us_controller_hit(&controller_boss,damage_q8/256.0,0);
        else{unsigned local=target-ENEMY_MAX;if(local<6)us_controller_hit_nest(&controller_boss,local,damage_q8/256.0,rail);else if(local<18)us_controller_hit_fighter(&controller_boss,local-6,damage_q8/256.0);else if(local<24)(void)us_heavy_bomb_intercept(&controller_bombs[local-18],damage_q8/256.0);}
        sync_controller_boss();return;
    }
    if(campaign_stage_id==6u) {
        unsigned local=target==BOSS_TARGET_CORE?US_HANGAR_CORE:target-ENEMY_MAX;
        if(target!=BOSS_TARGET_CORE&&local>=US_HANGAR_PARTS)return;
        us_hangar_hit(&hangar_boss,local,damage_q8*256u);sync_hangar_boss();return;
    }
    if (campaign_stage_id == 5u) {
        if (boss_grace_ticks) return;
        unsigned local = target == BOSS_TARGET_CORE ? US_BOUNDARY_CORE : target - ENEMY_MAX;
        if (target != BOSS_TARGET_CORE && local >= US_BOUNDARY_PARTS) return;
        us_boundary_hit(&boundary_boss, local, damage_q8 * 256u, 0u); sync_boundary_boss(); return;
    }
    if (campaign_stage_id == 4u) {
        if (boss_grace_ticks) return;
        unsigned local = target == BOSS_TARGET_CORE ? US_TWIN_BOMBER : target - ENEMY_MAX;
        if (target != BOSS_TARGET_CORE && local >= 7u) return;
        us_twin_hit(&twin_boss, local, damage_q8 * 256u, 0u); sync_twin_boss(); return;
    }
    if (campaign_stage_id == 1u) {
        if (target != BOSS_TARGET_CORE && target - ENEMY_MAX >= US_MECHANIC_PARTS) return;
        us_mechanic_hit(&mechanic_boss, target == BOSS_TARGET_CORE ? US_MECHANIC_CORE : target - ENEMY_MAX,
                        damage_q8 * 256u, 0u);
        sync_mechanic_boss();
    } else if (campaign_stage_id == 2u) {
        if (target != BOSS_TARGET_CORE && target - ENEMY_MAX >= US_ARCHON_PARTS) return;
        int32_t tx = 0, ty = 0;
        unsigned local = target == BOSS_TARGET_CORE ? US_ARCHON_CORE : target - ENEMY_MAX;
        us_archon_target_position(&archon_boss, local, &tx, &ty);
        unsigned reward = us_archon_hit(&archon_boss, local, damage_q8 * 256u);
        if (reward) spawn_pickup(tx / 256, ty / 256, reward, 0u);
        sync_archon_boss();
    } else if (target == BOSS_TARGET_CORE) boss_expose_damage((damage_q8 + Q8 / 2u) / Q8);
    else damage_boss_plate((target - ENEMY_MAX) % 7u, (target - ENEMY_MAX) / 7u, rail);
}

typedef struct {
    int32_t x, y, axis_x, axis_y;
    unsigned axis_length, length, half_long, half_width;
} ProjectileSweep;

static ProjectileSweep projectile_sweep(const Projectile *shot, int32_t old_x, int32_t old_y) {
    ProjectileSweep sweep;
    unsigned hull = shot->kind == SHOT_DART && shot->hull;
    sweep.x = hull ? old_x : shot->x; sweep.y = hull ? old_y : shot->y;
    sweep.axis_x = hull ? shot->x - old_x : shot->vx;
    sweep.axis_y = hull ? shot->y - old_y : shot->vy;
    sweep.axis_length = distance_q8(0, 0, sweep.axis_x, sweep.axis_y);
    sweep.length = hull ? sweep.axis_length : 0u;
    if (!sweep.axis_length) { sweep.axis_x = Q8; sweep.axis_y = 0; sweep.axis_length = Q8; }
    sweep.half_long = hull ? 9u * Q8 : 7u * Q8 / 2u;
    sweep.half_width = hull ? 7u * Q8 : 3u * Q8 / 2u;
    return sweep;
}

static int projectile_touches(const ProjectileSweep *sweep, unsigned radius_q8,
                              int32_t tx, int32_t ty, int32_t *along_out) {
    int32_t dx = tx - sweep->x, dy = ty - sweep->y;
    unsigned bound = sweep->length + sweep->half_long + sweep->half_width + radius_q8;
    if ((unsigned)abs_i(dx) > bound || (unsigned)abs_i(dy) > bound) return 0;
    int32_t along = (int32_t)(((int64_t)dx * sweep->axis_x + (int64_t)dy * sweep->axis_y) / sweep->axis_length);
    int32_t across = (int32_t)(((int64_t)dy * sweep->axis_x - (int64_t)dx * sweep->axis_y) / sweep->axis_length);
    int32_t excess_x = along < -(int)sweep->half_long ? -(int)sweep->half_long - along :
        along > (int)(sweep->length + sweep->half_long) ? along - (int)(sweep->length + sweep->half_long) : 0;
    int32_t excess_y = abs_i(across) > (int)sweep->half_width ? abs_i(across) - (int)sweep->half_width : 0;
    *along_out = along;
    return (uint64_t)((int64_t)excess_x * excess_x + (int64_t)excess_y * excess_y) <= (uint64_t)radius_q8 * radius_q8;
}

static unsigned warden_controller_sweep(void *context,float sx,float sy,float tx,float ty,float *x,float *y){
    (void)context;us_warden_sweep_t result=us_warden_sweep(&warden_field,sx,sy,tx,ty,38,1,1,controller_boss.max_hp,tech_effect("W9")!=0);
    if(!result.hit)return 0;
    *x=result.x;*y=result.y;controller_boss.vx=controller_boss.vy=0;
    controller_boss.form_clock=0.6+((int)((controller_boss.form_clock-0.6)/2.05)+1)*2.05;controller_boss.recovery=0.85;
    us_controller_hit(&controller_boss,result.damage,0);return 1;
}
static unsigned warden_mechanic_sweep(void *context,float sx,float sy,float tx,float ty,float *x,float *y){
    (void)context;us_warden_sweep_t result=us_warden_sweep(&warden_field,sx,sy,tx,ty,32,1,1,mechanic_boss.max_hp_q16/65536.0,tech_effect("W9")!=0);
    if(!result.hit)return 0;
    *x=result.x;*y=result.y;us_mechanic_guard_blocked(&mechanic_boss);
    us_mechanic_hit(&mechanic_boss,US_MECHANIC_CORE,(uint32_t)(result.damage*65536),US_MECHANIC_HIT_GUARD);return 1;
}
static void warden_shield_restore(void *context,double amount,unsigned direct){
    (void)context;
    if(!direct){if(amount>warden_shield_budget)amount=warden_shield_budget;warden_shield_budget-=amount;}
    if(amount<=0)return;
    warden_shield_fraction+=amount;unsigned whole=(unsigned)warden_shield_fraction;warden_shield_fraction-=whole;
    player_shield=min_u(player_shield_max,player_shield+whole);if(player_shield==player_shield_max)warden_shield_fraction=0;
}
static void warden_recycle(void *context,unsigned index){
    (void)context;
    if(index<PROJECTILE_MAX)projectiles[index].active=0;
    else if(index<PROJECTILE_MAX+US_TWIN_BOMBS)twin_boss.bombs[index-PROJECTILE_MAX].active=0;
    else if(index<PROJECTILE_MAX+US_TWIN_BOMBS+6)controller_bombs[index-PROJECTILE_MAX-US_TWIN_BOMBS].active=0;
}
static void warden_retaliate(void *context,float x,float y,unsigned damage,unsigned pierce){
    (void)context;int target=nearest_gunner_target((int32_t)(x*Q8),(int32_t)(y*Q8));if(target<0)return;
    int index=spawn_ally_projectile((int32_t)(x*Q8),(int32_t)(y*Q8),gunner_direction((int32_t)(x*Q8),(int32_t)(y*Q8),(unsigned)target),
        (tech_damage_q8(damage*100,SPECIES_WARDEN,1)+Q8/2)/Q8,420,0);
    if(index>=0)projectiles[index].pierce=(uint8_t)pierce;
}
typedef struct {float x,y;uint32_t generation;uint8_t allocated,active,eligible,queued;} WardenThreat;
static WardenThreat warden_threat(unsigned index){
    WardenThreat target={0};
    if(index<PROJECTILE_MAX){
        const Projectile *shot=&projectiles[index];target=(WardenThreat){shot->x/256.0f,shot->y/256.0f,projectile_generation[index],
            (uint8_t)(projectile_generation[index]&&shot->kind==SHOT_ENEMY),shot->active,(uint8_t)(shot->kind==SHOT_ENEMY&&shot->special!=1&&shot->special!=3),0};
    }else if(index<PROJECTILE_MAX+US_TWIN_BOMBS){
        const us_twin_bomb_t *bomb=&twin_boss.bombs[index-PROJECTILE_MAX];target=(WardenThreat){bomb->x,bomb->y,bomb->serial,(uint8_t)(campaign_stage_id==4u&&bomb->lifetime>0),bomb->active,1,0};
    }else if(index<PROJECTILE_MAX+US_TWIN_BOMBS+6u){
        const us_heavy_bomb_t *bomb=&controller_bombs[index-PROJECTILE_MAX-US_TWIN_BOMBS];target=(WardenThreat){bomb->x,bomb->y,bomb->serial,(uint8_t)(campaign_stage_id==7u&&bomb->initial_lifetime>0),bomb->active,1,0};
    }else{
        unsigned slot=index-PROJECTILE_MAX-US_TWIN_BOMBS-6u;const Enemy *enemy=&enemies[slot];
        target=(WardenThreat){enemy->x/256.0f,enemy->y/256.0f,enemy_generation[slot],
            (uint8_t)(enemy->active&&enemy->species==SPECIES_FRAGMENT&&!enemy->supply),enemy->active,1,enemy->expiry_queued};
    }
    return target;
}
static void interceptor_clear(SceneInterceptor *interceptor){
    unsigned index=interceptor->target_index;
    if(warden_claims[index].generation==interceptor->motion.target_id)warden_claims[index].active=0;
    interceptor->motion.active=0;
}
static void warden_passive(unsigned slot,int32_t x,int32_t y){
    (void)slot;
    if(warden_pulse_clock<=0){warden_pulse_clock=1.5;warden_shield_restore(NULL,1.5*(1+0.25*tech_effect("W1")),0);}
    us_threat_order_t order[WARDEN_THREAT_COUNT];unsigned count=0;
    float ox=x/256.0f,oy=y/256.0f;
    for(unsigned i=0;i<WARDEN_THREAT_COUNT;++i){WardenThreat target=warden_threat(i);if(!target.allocated)continue;
        float dx=target.x-ox,dy=target.y-oy;order[count++]=(us_threat_order_t){us_sf_add(us_sf_mul(dx,dx),us_sf_mul(dy,dy)),i};}
    us_threat_sort(order,count);unsigned launched=0;
    for(unsigned i=0;i<count;++i){
        unsigned index=order[i].index;WardenThreat target=warden_threat(index);
        if(!target.active||!target.eligible||us_fx_sqrt_float(order[i].distance_squared)>100||
            (warden_claims[index].active&&warden_claims[index].generation==target.generation))continue;
        unsigned free_slot=0;while(free_slot<WARDEN_THREAT_COUNT&&interceptors[free_slot].motion.active)++free_slot;
        if(free_slot==WARDEN_THREAT_COUNT)break;
        interceptors[free_slot].target_index=index;interceptors[free_slot].born=1;us_interceptor_init(&interceptors[free_slot].motion,ox,oy,target.generation);
        warden_claims[index].active=1;warden_claims[index].generation=target.generation;
        if(++launched>=3+tech_effect("W3"))break;
    }
}
#include "usurper_form_scene_impl.h"
static void player_form_step(void) {
    form_combat_step();
    us_form_guard_step(&player_form);
    if(selected_loadout.form_index!=9 || player_form.guard_cooldown>0)return;
    unsigned chosen=PROJECTILE_MAX;
    uint64_t nearest=(uint64_t)(110u*Q8)*(110u*Q8);
    /* specialization._intercept_projectile accepts EnemyProjectile only;
     * fragments are handled by captured Warden providers, not this form. */
    for(unsigned i=0;i<PROJECTILE_MAX;++i) {
        WardenThreat target=warden_threat(i);
        if(!target.allocated||!target.active||!target.eligible||
           (warden_claims[i].active&&warden_claims[i].generation==target.generation))continue;
        int64_t dx=(int64_t)projectiles[i].x-player_x,dy=(int64_t)projectiles[i].y-player_y;
        uint64_t squared=(uint64_t)(dx*dx+dy*dy);
        if(squared<nearest){nearest=squared;chosen=i;}
    }
    if(chosen==PROJECTILE_MAX)return;
    unsigned slot=0;while(slot<WARDEN_THREAT_COUNT&&interceptors[slot].motion.active)++slot;
    if(slot==WARDEN_THREAT_COUNT)return;
    WardenThreat target=warden_threat(chosen);
    interceptors[slot].target_index=chosen;interceptors[slot].born=1;
    us_interceptor_init(&interceptors[slot].motion,player_x/256.0f,player_y/256.0f,target.generation);
    warden_claims[chosen].active=1;warden_claims[chosen].generation=target.generation;
    us_form_guard_started(&player_form);
}
static void interceptor_step(void){
    for(unsigned i=0;i<WARDEN_THREAT_COUNT;++i){
        SceneInterceptor *interceptor=&interceptors[i];if(!interceptor->motion.active)continue;
        /* Nodes added during Godot's physics pass begin on the next pass. */
        if(interceptor->born){interceptor->born=0;continue;}
        unsigned index=interceptor->target_index;WardenThreat target=warden_threat(index);
        unsigned result=us_interceptor_step(&interceptor->motion,target.x,target.y,target.generation,target.allocated,target.active,target.queued);
        if(result==US_INTERCEPTOR_HIT){
            ++warden_interceptor_hits;
            if(index<PROJECTILE_MAX+US_TWIN_BOMBS+6u)warden_recycle(NULL,index);
            else {unsigned enemy=index-PROJECTILE_MAX-US_TWIN_BOMBS-6u;
                /* Original interceptor calls FragmentEnemy._on_hurt directly,
                 * bypassing Hurtbox's split invincibility, with twice max HP. */
                damage_enemy_received(enemy,enemies[enemy].max_hp*2.0);}
        }
        if(result!=US_INTERCEPTOR_NONE)interceptor_clear(interceptor);
    }
}
static void warden_field_step(void){
    if(!warden_field.active)return;
    us_warden_ranks_t ranks={.w2=tech_effect("W2"),.w4=tech_effect("W4"),.w6=tech_effect("W6"),.w7=tech_effect("W7"),.w8=tech_effect("W8"),.zw=tech_effect("ZW")};
    us_warden_enemy_t actors[ENEMY_MAX+1];memset(actors,0,sizeof(actors));
    /* us_warden_project returns before reading positions when alive is zero. */
    for(unsigned i=0;i<ENEMY_MAX;++i)if(enemies[i].active&&!enemies[i].supply)
        actors[i]=(us_warden_enemy_t){enemies[i].x/256.0f,enemies[i].y/256.0f,enemies[i].vx/256.0f,enemies[i].vy/256.0f,1,0,0};
    actors[ENEMY_MAX]=(us_warden_enemy_t){boss_x/256.0f,boss_y/256.0f,boss_vx/256.0f,boss_vy/256.0f,(uint8_t)(boss_active&&campaign_stage_id!=4u&&campaign_stage_id!=5u),1,0};
    us_warden_shot_t shots[PROJECTILE_MAX+US_TWIN_BOMBS+6];memset(shots,0,sizeof(shots));
    /* Keep array indices/callback order. Inactive or non-interceptible
     * entries are never queried for coordinates by us_warden_step. */
    for(unsigned i=0;i<PROJECTILE_MAX;++i)if(projectiles[i].active&&projectiles[i].kind==SHOT_ENEMY&&projectiles[i].special!=1&&projectiles[i].special!=3)
        shots[i]=(us_warden_shot_t){projectiles[i].x/256.0f,projectiles[i].y/256.0f,1,1};
    if(campaign_stage_id==4u)for(unsigned i=0;i<US_TWIN_BOMBS;++i)shots[PROJECTILE_MAX+i]=(us_warden_shot_t){twin_boss.bombs[i].x,twin_boss.bombs[i].y,1,twin_boss.bombs[i].active};
    if(campaign_stage_id==7u)for(unsigned i=0;i<6;++i)shots[PROJECTILE_MAX+US_TWIN_BOMBS+i]=(us_warden_shot_t){controller_bombs[i].x,controller_bombs[i].y,1,controller_bombs[i].active};
    us_warden_callbacks_t callbacks={NULL,warden_shield_restore,warden_recycle,warden_retaliate};
    unsigned contains=us_warden_step(&warden_field,player_x/256.0f,player_y/256.0f,defeat_pending||phase==USURPER_PHASE_RESULT,&ranks,actors,ENEMY_MAX+1,shots,PROJECTILE_MAX+US_TWIN_BOMBS+6,&callbacks);
    if(contains)warden_guard_left=0.1;
    for(unsigned i=0;i<ENEMY_MAX;++i)if(actors[i].alive){
        enemies[i].x=(int32_t)(actors[i].x*Q8);enemies[i].y=(int32_t)(actors[i].y*Q8);enemies[i].vx=(int32_t)(actors[i].vx*Q8);enemies[i].vy=(int32_t)(actors[i].vy*Q8);
        if(enemies[i].carrier_offspring){offspring_motion[i].x=actors[i].x;offspring_motion[i].y=actors[i].y;offspring_motion[i].vx=actors[i].vx;offspring_motion[i].vy=actors[i].vy;}
    }
    if(actors[ENEMY_MAX].alive){
        const us_warden_enemy_t *actor=&actors[ENEMY_MAX];boss_x=(int32_t)(actor->x*Q8);boss_y=(int32_t)(actor->y*Q8);boss_vx=(int32_t)(actor->vx*Q8);boss_vy=(int32_t)(actor->vy*Q8);
        if(campaign_stage_id==1u){mechanic_boss.x_float=actor->x;mechanic_boss.y_float=actor->y;mechanic_boss.vx_float=actor->vx;mechanic_boss.vy_float=actor->vy;mechanic_boss.x=boss_x*256;mechanic_boss.y=boss_y*256;}
        else if(campaign_stage_id==2u){archon_boss.x_float=actor->x;archon_boss.y_float=actor->y;archon_boss.vx_float=actor->vx;archon_boss.vy_float=actor->vy;archon_boss.x=boss_x*256;archon_boss.y=boss_y*256;}
        else if(campaign_stage_id==3u){flame_boss.x_float=actor->x;flame_boss.y_float=actor->y;flame_boss.x=boss_x*256;flame_boss.y=boss_y*256;}
        else if(campaign_stage_id==6u){hangar_boss.x=actor->x;hangar_boss.y=actor->y;hangar_boss.vx=actor->vx;hangar_boss.vy=actor->vy;}
        else if(campaign_stage_id==7u){controller_boss.x=actor->x;controller_boss.y=actor->y;controller_boss.vx=actor->vx;controller_boss.vy=actor->vy;}
    }
    if(!warden_field.active||!boss_active)return;
    if(campaign_stage_id==4u)for(unsigned i=0;i<2;++i){
        us_twin_actor_t *twin=&twin_boss.actors[i];us_warden_enemy_t actor={twin->x,twin->y,twin->vx,twin->vy,twin->active,1,0};us_warden_project(&warden_field,&actor);
        twin->x=actor.x;twin->y=actor.y;twin->vx=actor.vx;twin->vy=actor.vy;
    }
    if(campaign_stage_id==7u){
        for(unsigned i=0;i<6;++i){us_controller_nest_t *nest=&controller_boss.nests[i];us_warden_enemy_t actor={nest->x,nest->y,0,0,nest->active,0,0};us_warden_project(&warden_field,&actor);nest->x=actor.x;nest->y=actor.y;}
        for(unsigned i=0;i<12;++i){us_controller_fighter_t *fighter=&controller_boss.fighters[i];us_warden_enemy_t actor={fighter->x,fighter->y,fighter->vx,fighter->vy,(uint8_t)(fighter->present&&!fighter->queued),0,0};us_warden_project(&warden_field,&actor);fighter->x=actor.x;fighter->y=actor.y;fighter->vx=actor.vx;fighter->vy=actor.vy;}
    }
}

static void spend_projectile_hit(Projectile *shot) {
    if (shot->pierce > 1u) {
        --shot->pierce;
        if (shot->kind == SHOT_GUNNER) {
            if(shot->form_damage>0)shot->form_damage*=0.75;
            else shot->damage = (uint8_t)((shot->damage * 75u + 50u) / 100u);
        }
    } else shot->active = 0u;
}

static void complete_projectile_hit(unsigned shot_index, unsigned enemy_index) {
    Projectile *shot = &projectiles[shot_index];
    Enemy *enemy = &enemies[enemy_index];
    if (enemy->capture_reserved) return;
    if (shot->kind != SHOT_ENEMY) {
        uint32_t mask = 1u << (enemy_index % 32u);
        if (shot->hit_mask[enemy_index / 32u] & mask) return;
        shot->hit_mask[enemy_index / 32u] |= mask;
    }
    damage_enemy(enemy_index,shot->form_damage>0?shot->form_damage:shot->damage);
    if(!enemy->invincible)hostile_warden_counter(enemy_index,shot->rail,0,0,0);
    spend_projectile_hit(shot);
}

#include "usurper_projectile_motion_fast.h"

static void projectile_step(void) {
    for (unsigned index = 0; index < PROJECTILE_MAX; ++index) {
        Projectile *shot = &projectiles[index];
        if (!shot->active) continue;
        int32_t old_x = shot->x, old_y = shot->y;
        int32_t dx = us_projectile_motion_step(shot->vx,shot->age);
        int32_t dy = us_projectile_motion_step(shot->vy,shot->age);
        if (shot->kind == SHOT_DART) {
            unsigned length = distance_q8(0, 0, dx, dy);
            if (length > shot->remaining_q8 && length) {
                dx = (int32_t)((int64_t)dx * shot->remaining_q8 / length);
                dy = (int32_t)((int64_t)dy * shot->remaining_q8 / length);
                length = shot->remaining_q8;
            }
            shot->remaining_q8 -= length;
        }
        shot->x += dx; shot->y += dy;
        if (shot->kind == SHOT_ENEMY) {
            if (!us_logic_box_excludes(shot->x,shot->y,player_x,player_y,10u*Q8-1u)&&
                distance_q8(shot->x, shot->y, player_x, player_y) < 10u * Q8) {
                unsigned emitter = shot->emitter_index;
                int blocked = phase == USURPER_PHASE_TUTORIAL || player_invincible_ticks ||
                    (emitter < ENEMY_MAX && shot->emitter_generation == enemy_generation[emitter] &&
                     enemies[emitter].capture_reserved);
                if (!blocked) {
                    damage_player(shot->damage);
                    shot->active = 0u;
                    if (defeat_pending) return;
                    continue;
                }
            }
        } else {
            ProjectileSweep sweep = projectile_sweep(shot, old_x, old_y);
            for (unsigned visit = 0; visit < ATTACK_TARGET_MAX && shot->active; ++visit) {
                unsigned nearest = ATTACK_TARGET_MAX;
                int32_t nearest_along = 0x7fffffff;
                for (unsigned target = 0; target < ATTACK_TARGET_MAX; ++target) {
                    int32_t tx, ty, along;
                    if (!attack_target_position(target, &tx, &ty)) continue;
                    unsigned radius = target == BOSS_TARGET_CORE ? 23u * Q8 : 6u * Q8;
                    if (campaign_stage_id == 1u && target >= ENEMY_MAX && target != BOSS_TARGET_CORE) radius = 10u * Q8;
                    if (campaign_stage_id == 2u && target >= ENEMY_MAX && target != BOSS_TARGET_CORE) radius = 15u * Q8;
                    if(campaign_stage_id==6u&&target>=ENEMY_MAX&&target!=BOSS_TARGET_CORE)radius=10u*Q8;
                    if (campaign_stage_id == 5u && target >= ENEMY_MAX)
                        radius = (target == BOSS_TARGET_CORE ? 17u : (target - ENEMY_MAX) % 3u ? 15u : 16u) * Q8;
                    if (campaign_stage_id == 4u && target >= ENEMY_MAX) {
                        unsigned local = target == BOSS_TARGET_CORE ? US_TWIN_BOMBER : target - ENEMY_MAX;
                        radius = (local == US_TWIN_BOMBER && target == BOSS_TARGET_CORE ? 26u : local == US_TWIN_SPLITTER ? 17u : local >= 7u ? 2u : 10u) * Q8;
                    }
                    if (target < ENEMY_MAX) {
                        if (enemies[target].invincible || enemies[target].capture_reserved ||
                            (shot->hit_mask[target / 32u] & (1u << (target % 32u)))) continue;
                        radius = enemies[target].species == SPECIES_GUNNER ? 6u * Q8 : 11u * Q8 / 2u;
                    } else if (shot->boss_hit_mask & (1u << (target - ENEMY_MAX))){
                        if(campaign_stage_id!=7u||shot->boss_hit_generation[target-ENEMY_MAX]==controller_target_serial(target))continue;
                    }
                    if(campaign_stage_id==7u&&target>=ENEMY_MAX)radius=controller_target_radius(target);
                    if (projectile_touches(&sweep, radius, tx, ty, &along) && along < nearest_along) {
                        nearest = target; nearest_along = along;
                    }
                }
                if (nearest == ATTACK_TARGET_MAX) break;
                if (nearest < ENEMY_MAX) complete_projectile_hit(index, nearest);
                else {
                    shot->boss_hit_mask |= 1u << (nearest - ENEMY_MAX);
                    if(campaign_stage_id==7u)shot->boss_hit_generation[nearest-ENEMY_MAX]=controller_target_serial(nearest);
                    if(shot->form_damage>0)damage_boss_form(nearest,shot->form_damage,shot->rail);
                    else if (campaign_stage_id == 4u && nearest != BOSS_TARGET_CORE && nearest - ENEMY_MAX >= 7u) {
                        (void)us_twin_intercept(&twin_boss, nearest - ENEMY_MAX - 7u, shot->damage);
                    } else if (campaign_stage_id == 3u) damage_flame_target(nearest, shot->damage * Q8, shot->x / 256.0f, shot->y / 256.0f);
                    else if (campaign_stage_id == 4u || campaign_stage_id == 5u || campaign_stage_id==6u || campaign_stage_id==7u) damage_boss_target(nearest, shot->damage * Q8, shot->rail);
                    else if (campaign_stage_id == 1u || campaign_stage_id == 2u) damage_boss_target(nearest, shot->damage * Q8, shot->rail);
                    else if (nearest == BOSS_TARGET_CORE) boss_expose_damage(shot->damage);
                    else {
                        unsigned plate = nearest - ENEMY_MAX;
                        if (!shot->rail || !shot->boss_armor_hit) {
                            damage_boss_plate(plate % 7u, plate / 7u, shot->rail);
                            if (shot->rail) shot->boss_armor_hit = 1u;
                        }
                    }
                    spend_projectile_hit(shot);
                    if (phase == USURPER_PHASE_RESULT) return;
                }
            }
        }
        ++shot->age;
        if (shot->age >= shot->lifetime || (shot->kind == SHOT_DART && !shot->remaining_q8) ||
            shot->x < -20 * Q8 || shot->x > (FIELD_W + 20) * Q8 ||
            shot->y < -20 * Q8 || shot->y > (FIELD_H + 20) * Q8) shot->active = 0u;
    }
}

static void spawn_arrival_event(unsigned event_index, const usurper_spawn_event_t *event) {
    unsigned pending = 0;
    for (unsigned i = 0; i < ARRIVAL_MAX; ++i) if (arrivals[i].active) pending += arrivals[i].count;
    if (enemy_count() + pending >= 90u) return;
    int slot = -1;
    for (unsigned i = 0; i < ARRIVAL_MAX; ++i) if (!arrivals[i].active) { slot = (int)i; break; }
    if (slot < 0) return;
    Arrival *arrival = &arrivals[slot];
    memset(arrival, 0, sizeof(*arrival));
    arrival->active = 1;
    arrival->ticks_left = ARRIVAL_TICKS;
    arrival->count = event->count;
    if(campaign_stage_id==7u&&boss_spawned&&controller_boss.phase==2){arrival->sovereign_wave=1;arrival->wave_index=controller_boss.waves.index;}
    for (unsigned member = 0; member < event->count && member < 8u; ++member) {
        arrival->species[member] = event->species[member];
        int32_t cx, cy;
        if (event_index == 0u) {
            cx = player_x + 112 * Q8;
            cy = player_y;
        } else {
            unsigned birth_time = event->tick;
            unsigned angle = (event_index * 90u + (birth_time / 1200u) * 45u) % 360u;
            int dx, dy;
            direction_q8(angle, &dx, &dy);
            cx = player_x + dx * 190;
            cy = player_y + dy * 190;
        }
        int offset = ((int)member * 2 - ((int)event->count - 1)) * 16;
        arrival->x[member] = clamp_i((int)cx + offset * Q8, 24 * Q8, (FIELD_W - 24) * Q8);
        arrival->y[member] = clamp_i((int)cy, 24 * Q8, (FIELD_H - 24) * Q8);
        if (distance_q8(arrival->x[member], arrival->y[member], player_x, player_y) < 100u * Q8)
            arrival->x[member] = clamp_i((int)player_x + 190 * Q8, 24 * Q8, (FIELD_W - 24) * Q8);
        if(arrival->sovereign_wave&&controller_boss.waves.index%5==2){
            float dx=(cx-player_x)/256.0f,dy=(cy-player_y)/256.0f,length=us_fx_sqrt_float(dx*dx+dy*dy),ax=length?dx/length:0,ay=length?dy/length:0;
            float offset=(float)((member-(event->count-1)*0.5)*30);
            arrival->x[member]=clamp_i((int32_t)((cx/256.0f+ax*offset)*Q8),24*Q8,(FIELD_W-24)*Q8);
            arrival->y[member]=clamp_i((int32_t)((cy/256.0f+ay*offset)*Q8),24*Q8,(FIELD_H-24)*Q8);
        }
    }
}

static void arrival_step(void) {
    for (unsigned i = 0; i < ARRIVAL_MAX; ++i) {
        Arrival *arrival = &arrivals[i];
        if (!arrival->active) continue;
        if (arrival->ticks_left) --arrival->ticks_left;
        if (arrival->ticks_left) continue;
        for (unsigned member = 0; member < arrival->count && member < 8u; ++member){
            int index=spawn_enemy(arrival->species[member],arrival->x[member],arrival->y[member],0);
            if(index>=0&&arrival->sovereign_wave){enemy_sovereign_wave[index]=1;enemy_wave_index[index]=arrival->wave_index;if(enemies[index].species==SPECIES_GUNNER)enemies[index].orbit_sign=0;}
        }
        arrival->active = 0;
    }
}

static void spawn_campaign_event(void) {
    if (campaign_stage_id != 0u) {
        if (level_tick / 60.0 < campaign_next_birth) return;
        usurper_spawn_event_t event = {.tick = (uint16_t)(campaign_next_birth * 60.0 + 0.5)};
        event.count = (uint8_t)us_campaign_roster(campaign_stage_id, spawn_index, boss_spawned,
                                               (uint32_t)(campaign_next_birth * 60.0 + 0.5), event.species);
        if(campaign_stage_id==7u&&boss_spawned){
            if(controller_boss.phase==1)event.count=0;
            else if(controller_boss.phase==2){unsigned external,scheduled,carriers;controller_wave_counts(&external,&scheduled,&carriers);event.count=(uint8_t)us_controller_wave_roster(&controller_boss,external,scheduled,carriers);
                static const uint8_t species[5]={SPECIES_SKIMMER,SPECIES_DART,SPECIES_GUNNER,SPECIES_BOMBER,SPECIES_CARRIER};for(unsigned i=0;i<event.count;++i)event.species[i]=species[controller_boss.waves.index%5];}
        }
        if(campaign_stage_id==6u) {
            unsigned carriers=0,splitters=0;
            for(unsigned i=0;i<ENEMY_MAX;++i)if(enemies[i].active&&!enemies[i].expiry_queued) {
                carriers+=enemies[i].species==SPECIES_CARRIER;splitters+=enemies[i].species==SPECIES_SPLITTER;
            }
            for(unsigned i=0;i<ARRIVAL_MAX;++i)if(arrivals[i].active)for(unsigned j=0;j<arrivals[i].count;++j) {
                carriers+=arrivals[i].species[j]==SPECIES_CARRIER;splitters+=arrivals[i].species[j]==SPECIES_SPLITTER;
            }
            unsigned kept=0;
            for(unsigned i=0;i<event.count;++i) {
                unsigned species=event.species[i];
                if((species==SPECIES_CARRIER&&carriers>=5u)||(species==SPECIES_SPLITTER&&splitters>=26u))continue;
                event.species[kept++]=(uint8_t)species;carriers+=species==SPECIES_CARRIER;splitters+=species==SPECIES_SPLITTER;
            }
            event.count=(uint8_t)kept;
        }
        if (event.count) spawn_arrival_event(spawn_index, &event);
        ++spawn_index;
        campaign_next_birth += campaign_stage_id==7u&&boss_spawned&&controller_boss.phase==2?2.4:us_campaign_birth_seconds(campaign_stage_id, boss_spawned);
        return;
    }
    if (spawn_index < USURPER_SPAWN_EVENT_COUNT) {
        usurper_spawn_event_t event = usurper_spawn_timeline[spawn_index];
        if (level_tick < event.tick) return;
        event.count = (uint8_t)us_campaign_roster(campaign_stage_id, spawn_index, 0u, event.tick, event.species);
        spawn_arrival_event(spawn_index, &event);
        ++spawn_index;
        return;
    }
    unsigned first_boss_wave = usurper_spawn_timeline[USURPER_SPAWN_EVENT_COUNT - 1u].tick + 84u;
    unsigned next_tick = first_boss_wave + (spawn_index - USURPER_SPAWN_EVENT_COUNT) * 108u;
    if (level_tick < next_tick) return;
    usurper_spawn_event_t event = {.tick = (uint16_t)next_tick};
    event.count = (uint8_t)us_campaign_roster(campaign_stage_id, spawn_index, 1u, next_tick, event.species);
    spawn_arrival_event(spawn_index, &event);
    ++spawn_index;
}

static void boss_spawn(void) {
    boss_vx = boss_vy = 0; boss_reforming_ticks = 0u;
    for (unsigned i = 0; i <= SKIMMER_SWEEP_MAX; ++i)
        for (unsigned j = ENEMY_MAX; j < ATTACK_TARGET_MAX; ++j)
            skimmer_sweeps[i].hit_tick[j] = skimmer_sweeps[i].echo_hit_tick[j] = 0u;
    boss_active = boss_spawned = 1u;
    if (campaign_stage_id > 7u) {
        /* The remaining encounters have independent state machines and must
         * never silently use the first-stage armor boss. */
        boss_active = 0u;
        message(us_campaign_stage(campaign_stage_id)->name);
        return;
    }
    if(campaign_stage_id==7u){
        memset(arrivals,0,sizeof(arrivals));us_controller_birth(player_x*256,player_y*256,camera_x*256,camera_y*256,&controller_warning_x,&controller_warning_y,controller_warning_valid);
        boss_x=controller_warning_x/256;boss_y=controller_warning_y/256;us_controller_init(&controller_boss,boss_x*256,boss_y*256,player_x*256,player_y*256);
        controller_boss.guard_sweep=warden_controller_sweep;
        sync_controller_boss();boss_prev_x=boss_x;boss_prev_y=boss_y;boss_grace_ticks=36;message(us_campaign_stage(7)->name);return;
    }
    if(campaign_stage_id==6u) {
        us_hangar_birth(player_x*256,player_y*256,camera_x*256,camera_y*256,&hangar_warning_x,&hangar_warning_y,hangar_warning_valid);
        boss_x=hangar_warning_x/256;boss_y=hangar_warning_y/256;
        us_hangar_init(&hangar_boss,us_campaign_stage(6u)->hp_q16,boss_x*256,boss_y*256,player_x*256,player_y*256);
        sync_hangar_boss();boss_prev_x=boss_x;boss_prev_y=boss_y;boss_grace_ticks=36;
        message(us_campaign_stage(6u)->name);return;
    }
    if (campaign_stage_id == 5u) {
        us_boundary_init(&boundary_boss, us_campaign_stage(5u)->hp_q16, player_x * 256, player_y * 256, 0);
        boundary_boss.presentation_enabled = 1;
        player_x = (int32_t)(boundary_boss.px * Q8); player_y = (int32_t)(boundary_boss.py * Q8);
        boundary_bind_units(); boundary_confine_enemies(); sync_boundary_boss();
        boss_prev_x = boss_x; boss_prev_y = boss_y; boss_grace_ticks = 36u;
        message(us_campaign_stage(5u)->name); return;
    }
    if (campaign_stage_id == 4u) {
        boss_x = clamp_i(player_x + 110 * Q8, 148 * Q8, (FIELD_W - 148) * Q8);
        boss_y = clamp_i(player_y, 148 * Q8, (FIELD_H - 148) * Q8);
        us_twin_init(&twin_boss, us_campaign_stage(4u)->hp_q16,
            boss_x * 256, boss_y * 256, player_x * 256, player_y * 256);
        boss_prev_x = boss_x; boss_prev_y = boss_y; boss_grace_ticks = 36u;
        sync_twin_boss(); message(us_campaign_stage(4u)->name); return;
    }
    if (campaign_stage_id == 3u) {
        boss_x = clamp_i(player_x + 110 * Q8, 45 * Q8, (FIELD_W - 45) * Q8);
        boss_y = clamp_i(player_y, 45 * Q8, (FIELD_H - 45) * Q8);
        us_flame_init(&flame_boss, us_campaign_stage(3u)->hp_q16,
            boss_x * 256, boss_y * 256, player_x * 256, player_y * 256);
        boss_prev_x = boss_x; boss_prev_y = boss_y; boss_grace_ticks = 36u;
        sync_flame_boss(); message(us_campaign_stage(3u)->name); return;
    }
    if (campaign_stage_id == 2u) {
        boss_x = clamp_i(player_x + 110 * Q8, 145 * Q8, (FIELD_W - 145) * Q8);
        boss_y = clamp_i(player_y, 145 * Q8, (FIELD_H - 145) * Q8);
        us_archon_init(&archon_boss, us_campaign_stage(2u)->hp_q16,
                       boss_x * 256, boss_y * 256, player_x * 256, player_y * 256);
        boss_prev_x = boss_x; boss_prev_y = boss_y; boss_grace_ticks = 36u;
        sync_archon_boss(); message("执政官"); return;
    }
    if (campaign_stage_id == 1u) {
        boss_x = clamp_i(player_x + 110 * Q8, 85 * Q8, (FIELD_W - 85) * Q8);
        boss_y = clamp_i(player_y, 85 * Q8, (FIELD_H - 85) * Q8);
        us_mechanic_init(&mechanic_boss, us_campaign_stage(1u)->hp_q16,
                        boss_x * 256, boss_y * 256, player_x * 256, player_y * 256);
        mechanic_boss.guard_sweep=warden_mechanic_sweep;
        boss_prev_x = boss_x; boss_prev_y = boss_y;
        boss_grace_ticks = 36u;
        sync_mechanic_boss(); message("破阵者"); return;
    }
    boss_phase = 1u;
    boss_hp = BOSS_BASE_HP;
    boss_armor_left = BOSS_PLATE_COUNT;
    memset(armor_alive, 1, sizeof(armor_alive));
    memset(armor_column_cooldown, 0, sizeof(armor_column_cooldown));
    boss_x = clamp_i((int)player_x + 110 * Q8, 75 * Q8, (FIELD_W - 75) * Q8);
    boss_y = player_y;
    boss_prev_x = boss_x;
    boss_prev_y = boss_y;
    boss_angle = angle_to(player_x - boss_x, player_y - boss_y);
    boss_locked_angle = boss_angle;
    boss_cycle_ticks = 120u;
    boss_shot_tick = 0u;
    message("披甲者 / ARMOR 21");
}

static void boss_step(void) {
    if (!boss_active) return;
    if(campaign_stage_id==7u){
        if(boss_grace_ticks){--boss_grace_ticks;return;}
        for(unsigned i=0;i<13;++i)if(controller_contact_ticks[i])--controller_contact_ticks[i];
        unsigned external,scheduled,carriers,heavy=0;controller_wave_counts(&external,&scheduled,&carriers);(void)carriers;
        for(unsigned i=0;i<6;++i)heavy+=controller_bombs[i].active;
        us_controller_step(&controller_boss,player_x*256,player_y*256,player_vx*256,player_vy*256,(float)tech_speed(),external,scheduled,heavy,&controller_events);
        for(unsigned i=0;i<controller_events.count;++i){int index=spawn_enemy_projectile((int32_t)(controller_events.shots[i].x*Q8),(int32_t)(controller_events.shots[i].y*Q8),0,controller_events.shots[i].damage,controller_events.shots[i].speed);
            if(index>=0){projectiles[index].vx=(int32_t)(controller_events.shots[i].dx*controller_events.shots[i].speed*Q8);projectiles[index].vy=(int32_t)(controller_events.shots[i].dy*controller_events.shots[i].speed*Q8);projectiles[index].lifetime=240;}}
        for(unsigned i=0;i<controller_events.ordnance_count;++i){int index=spawn_enemy_projectile((int32_t)(controller_events.ordnance[i].x*Q8),(int32_t)(controller_events.ordnance[i].y*Q8),0,controller_events.ordnance[i].damage,controller_events.ordnance[i].speed);
            if(index>=0){projectiles[index].vx=(int32_t)(controller_events.ordnance[i].dx*controller_events.ordnance[i].speed*Q8);projectiles[index].vy=(int32_t)(controller_events.ordnance[i].dy*controller_events.ordnance[i].speed*Q8);projectiles[index].lifetime=(uint16_t)(controller_events.ordnance[i].lifetime*60+0.5);projectiles[index].special=(uint8_t)controller_events.ordnance[i].mode;}}
        for(unsigned i=0;i<controller_events.bomb_count;++i)for(unsigned slot=0;slot<6;++slot)if(!controller_bombs[slot].active){
            us_heavy_bomb_init(&controller_bombs[slot],controller_events.bombs[i].x,controller_events.bombs[i].y,controller_events.bombs[i].landing_x,controller_events.bombs[i].landing_y,++controller_bomb_serial);break;}
        if(controller_events.charge_damage)damage_player(controller_events.charge_damage);
        if(controller_events.flame_damage)damage_player(controller_events.flame_damage);
        if(controller_events.blade_damage)damage_player(controller_events.blade_damage);
        sync_controller_boss();return;
    }
    if(campaign_stage_id==6u) {
        if(boss_grace_ticks) {
            --boss_grace_ticks;
            for(unsigned i=0;i<US_HANGAR_PARTS;++i)if(hangar_boss.part_flash[i]>0) {
                hangar_boss.part_flash[i]-=1.0/60;if(hangar_boss.part_flash[i]<0)hangar_boss.part_flash[i]=0;
            }
            return;
        }
        if(hangar_contact_tick)--hangar_contact_tick;
        us_hangar_step(&hangar_boss,player_x*256,player_y*256,&hangar_spawn_pending);
        sync_hangar_boss();
        return;
    }
    if (campaign_stage_id == 5u) {
        if (boss_grace_ticks) { --boss_grace_ticks; return; }
        boundary_bind_units(); us_boundary_output_t output;
        boundary_boss.guard_count=warden_field.active?1u:0u;
        if(warden_field.active)boundary_boss.guards[0]=(us_boundary_guard_t){.x=warden_field.x,.y=warden_field.y,.radius=warden_field.radius,.age=warden_field.age,.duration=warden_field.duration,
            .block_x=warden_field.block_dx,.block_y=warden_field.block_dy,.block_flash=warden_field.block_flash,.is_warden=1,.strengthened=(uint8_t)(tech_effect("W9")!=0)};
        us_boundary_step_delta(&boundary_boss, player_x * 256, player_y * 256, player_vx * 256, player_vy * 256, 1.0 / 60, &output);
        if(warden_field.active){warden_field.block_flash=boundary_boss.guards[0].block_flash;warden_field.block_dx=boundary_boss.guards[0].block_x;warden_field.block_dy=boundary_boss.guards[0].block_y;}
        player_x = (int32_t)(boundary_boss.px * Q8); player_y = (int32_t)(boundary_boss.py * Q8);
        player_vx = (int32_t)(boundary_boss.vx * Q8); player_vy = (int32_t)(boundary_boss.vy * Q8);
        boundary_remove_grabbed_units(); boundary_confine_enemies(); sync_boundary_boss();
        for (unsigned i = 0; i < output.charge_hits; ++i) damage_player(16);
        for (unsigned i = 0; i < output.beam_hits; ++i) damage_player(14);
        for (unsigned i = 0; i < output.shot_count; ++i)
            spawn_enemy_projectile(400 * Q8, 240 * Q8, angle_to((int32_t)(output.shots[i].dx * 65536), (int32_t)(output.shots[i].dy * 65536)), 12, 150);
        return;
    }
    if (campaign_stage_id == 4u) {
        if (boss_grace_ticks) { --boss_grace_ticks; return; }
        for (unsigned i = 0; i < 8u; ++i) if (twin_explosions[i].active) {
            twin_explosions[i].age += 1.0 / 60;
            if (twin_explosions[i].age >= 0.55) twin_explosions[i].active = 0;
        }
        unsigned serials[US_TWIN_BOMBS], active[US_TWIN_BOMBS];
        for (unsigned i = 0; i < US_TWIN_BOMBS; ++i) { serials[i] = twin_boss.bombs[i].serial; active[i] = twin_boss.bombs[i].active; }
        us_twin_output_t output;
        us_twin_step(&twin_boss, player_x * 256, player_y * 256, player_vx * 256, player_vy * 256, tech_speed(), &output);
        for (unsigned i = 0; i < US_TWIN_BOMBS; ++i)
            if (twin_boss.bombs[i].active && (!active[i] || serials[i] != twin_boss.bombs[i].serial))
                for (unsigned j = 0; j < PROJECTILE_MAX; ++j) projectiles[j].boss_hit_mask &= ~(1u << (7u + i));
        for (unsigned i = 0; i < 2u; ++i) if (twin_contact_ticks[i]) --twin_contact_ticks[i];
        for (unsigned i = 0; i < output.damage_count; ++i) damage_player(16u);
        for (unsigned i = 0; i < output.exploded; ++i)
            for (unsigned slot = 0; slot < 8u; ++slot) if (!twin_explosions[slot].active) {
                twin_explosions[slot].active = 1; twin_explosions[slot].age = 0;
                twin_explosions[slot].x = output.explosions[i].x; twin_explosions[slot].y = output.explosions[i].y; break;
            }
        sync_twin_boss(); return;
    }
    if (campaign_stage_id == 3u) {
        if (boss_grace_ticks) { --boss_grace_ticks; return; }
        us_flame_output_t output;
        us_flame_step(&flame_boss, player_x * 256, player_y * 256, &output);
        sync_flame_boss();
        if (output.teleported) { boss_prev_x = boss_x; boss_prev_y = boss_y; }
        if (output.flame_damage) damage_player(output.flame_damage);
        return;
    }
    if (campaign_stage_id == 2u) {
        if (boss_grace_ticks) { --boss_grace_ticks; return; }
        us_archon_flush_deferred(&archon_boss, player_x * 256, player_y * 256);
        us_archon_output_t output;
        us_archon_step(&archon_boss, player_x * 256, player_y * 256, &output);
        sync_archon_boss();
        for (unsigned i = 0; i < output.count; ++i) {
            const us_boss_shot_t *event = &output.shots[i];
            int shot = spawn_enemy_projectile(event->x / 256, event->y / 256,
                angle_to(event->direction_x, event->direction_y), event->damage, event->speed);
            if (shot >= 0) {
                projectiles[shot].vx = event->direction_x * (int)event->speed / 256;
                projectiles[shot].vy = event->direction_y * (int)event->speed / 256;
            }
        }
        return;
    }
    if (campaign_stage_id == 1u) {
        if (boss_grace_ticks) { --boss_grace_ticks; return; }
        us_mechanic_output_t output;
        us_mechanic_step(&mechanic_boss, player_x * 256, player_y * 256, &output);
        sync_mechanic_boss();
        for (unsigned i = 0; i < output.count; ++i) {
            const us_boss_shot_t *event = &output.shots[i];
            int shot = spawn_enemy_projectile(event->x / 256, event->y / 256,
                        angle_to(event->direction_x, event->direction_y), event->damage, event->speed);
            if (shot >= 0) {
                projectiles[shot].vx = event->direction_x * (int)event->speed / 256;
                projectiles[shot].vy = event->direction_y * (int)event->speed / 256;
            }
        }
        return;
    }
    if (boss_reforming_ticks) --boss_reforming_ticks;
    int32_t dx = player_x - boss_x, dy = player_y - boss_y;
    unsigned distance = distance_q8(boss_x, boss_y, player_x, player_y);
    unsigned desired_angle = angle_to(dx, dy);
    int turn = angle_delta((int)boss_angle, (int)desired_angle);
    if ((frame_tick & 1u) == 0u) {
        if (turn > 1) boss_angle = (boss_angle + 1u) % 360u;
        else if (turn < -1) boss_angle = (boss_angle + 359u) % 360u;
    }
    int32_t pace = ((int32_t)(distance / Q8) - 140) * 7 / 10;
    pace = pace < -25 ? -25 : pace > 45 ? 45 : pace;
    int ndx, ndy;
    direction_q8(desired_angle, &ndx, &ndy);
    boss_vx = ndx * pace; boss_vy = ndy * pace;
    boss_x = clamp_i((int)(boss_x + ndx * pace / 60), 75 * Q8, (FIELD_W - 75) * Q8);
    boss_y = clamp_i((int)(boss_y + ndy * pace / 60), 75 * Q8, (FIELD_H - 75) * Q8);
    if (boss_cycle_ticks) --boss_cycle_ticks;
    if (!boss_cycle_ticks) {
        boss_cycle_ticks = boss_armor_left ? 192u : 144u;
        boss_locked_angle = angle_to(player_x - boss_x, player_y - boss_y);
        ++boss_pattern;
    }
    unsigned fire_at = boss_armor_left ? 141u : 108u;
    if (boss_cycle_ticks < fire_at && boss_shot_tick >= fire_at) {
        for (int spread = -2; spread <= 2; ++spread)
            spawn_enemy_projectile(boss_x, boss_y, angle_offset(boss_locked_angle, spread * 13), 8u, 115u);
    }
    boss_shot_tick = boss_cycle_ticks;
}

/* ChargeMotion.endpoint clips the whole vector at its first boundary.
 * Clamping each axis separately would make diagonal dashes slide along walls. */
static unsigned charge_endpoint(int32_t *x, int32_t *y, int32_t motion_x, int32_t motion_y,
                                int32_t margin) {
    *x = clamp_i(*x, margin, FIELD_W * Q8 - margin);
    *y = clamp_i(*y, margin, FIELD_H * Q8 - margin);
    uint32_t travel = 16777216u;
    if (motion_x > 0) travel = min_u(travel, (uint32_t)((int64_t)(FIELD_W * Q8 - margin - *x) * 16777216 / motion_x));
    else if (motion_x < 0) travel = min_u(travel, (uint32_t)((int64_t)(margin - *x) * 16777216 / motion_x));
    if (motion_y > 0) travel = min_u(travel, (uint32_t)((int64_t)(FIELD_H * Q8 - margin - *y) * 16777216 / motion_y));
    else if (motion_y < 0) travel = min_u(travel, (uint32_t)((int64_t)(margin - *y) * 16777216 / motion_y));
    *x += (int32_t)((int64_t)motion_x * travel / 16777216);
    *y += (int32_t)((int64_t)motion_y * travel / 16777216);
    return travel < 16777216u;
}

static unsigned charge_segment_distance(int32_t tx, int32_t ty, int32_t x0, int32_t y0,
                                        int32_t x1, int32_t y1) {
    int32_t dx = x1 - x0, dy = y1 - y0;
    int64_t length = (int64_t)dx * dx + (int64_t)dy * dy;
    int64_t projection = (int64_t)(tx - x0) * dx + (int64_t)(ty - y0) * dy;
    if (length && projection > 0) {
        if (projection >= length) { x0 = x1; y0 = y1; }
        else { x0 += (int32_t)((int64_t)dx * projection / length); y0 += (int32_t)((int64_t)dy * projection / length); }
    }
    return distance_q8(tx, ty, x0, y0);
}

static void rammer_charge_step(void) {
    RammerCharge *charge = &rammer_charge;
    if (!charge->remaining_q16 || !player_hp || defeat_pending) return;
    rammer_contact_guard = 1u;
    uint32_t time = min_u(charge->remaining_q16, 65536u);
    int32_t old_x = player_x, old_y = player_y;
    int32_t mx = (int32_t)((int64_t)charge->direction_x * 360 * time / (60 * 65536));
    int32_t my = (int32_t)((int64_t)charge->direction_y * 360 * time / (60 * 65536));
    unsigned boundary = charge_endpoint(&player_x, &player_y, mx, my, 14 * Q8);
    charge->remaining_q16 -= time;
    charge->age_q16 += 65536u;
    player_vx = player_vy = 0;
    player_angle = charge->angle;
    if (tech_effect("A7")) {
        for (unsigned i = 0; i < PROJECTILE_MAX; ++i)
            if (projectiles[i].active && projectiles[i].kind == SHOT_ENEMY &&
                distance_q8(projectiles[i].x, projectiles[i].y, player_x, player_y) < 22u * Q8)
                projectiles[i].active = 0u;
    }
    unsigned wings = tech_effect("A4") ? min_u(14u * Q8,
        (unsigned)((uint64_t)14u * Q8 * charge->age_q16 * 5u / (18u * 65536u))) : 0u;
    for (unsigned i = 0; i < ATTACK_TARGET_MAX; ++i) {
        int32_t tx, ty;
        if (charge->hit[i] || !attack_target_position(i, &tx, &ty)) continue;
        if (i >= ENEMY_MAX && !attack_target_selectable(i)) continue;
        if (distance_q8(tx, ty, player_x, player_y) >= 190u * Q8) continue;
        unsigned distance = charge_segment_distance(tx, ty, old_x, old_y, player_x, player_y);
        unsigned core = (i == BOSS_TARGET_CORE ? 42u : 25u) * Q8;
        if (distance >= core + wings) continue;
        charge->hit[i] = 1u;
        unsigned damage_q8 = tech_damage_q8_roster(6000u, SPECIES_RAMMER, 1u, 0u);
        if (distance > core) damage_q8 /= 2u;
        if (i < ENEMY_MAX && tech_effect("A6") && enemies[i].ward_ticks) damage_q8 *= 2u;
        if (i < ENEMY_MAX) {
            Enemy *enemy = &enemies[i];
            if (!enemy->invincible) {
                damage_enemy(i,damage_q8/(charge->form_owned?1024.0:256.0));
            }
            if (enemy->active && tech_effect("A5") && !enemy->stun_cooldown) {
                enemy->stun_ticks = 48u;
                enemy->stun_cooldown = 180u;
            }
        } else if(charge->form_owned)damage_boss_form(i,damage_q8/1024.0,0u);
        else damage_boss_target(i, damage_q8, 0u);
    }
    if (boundary) charge->remaining_q16 = 0u;
}

static unsigned player_dart_mobility(void) {
    unsigned provider=0;
    for(unsigned i=0;i<unit_count;++i)provider|=unit_species[i]==SPECIES_DART;
    return us_form_dart_quarters(selected_loadout.form_index,provider,tech_effect("D9"));
}
static void move_player(void) {
    rammer_contact_guard = 0u;
    if (rammer_charge.remaining_q16) { rammer_charge_step(); goto follow_camera; }
    unsigned mobility=player_dart_mobility();
    unsigned max_speed_q8=us_form_speed_q8(tech_speed(),mobility);
    unsigned acceleration_step=us_form_accel_step_q8(mobility);
    unsigned coast_q24=us_form_coast_q24(mobility);
    int32_t wanted_x = 0, wanted_y = 0;
    unsigned keyboard_held = controls_timeout && ((controls_held & 15u) != 0u);
    unsigned thrust = (player_move_ticks != 0u || keyboard_held) && (move_x || move_y);
    if (thrust) {
        if (player_move_ticks && !keyboard_held) --player_move_ticks;
        int dx = move_x, dy = move_y;
        /* Scale after multiplication. Scaling +/-1 first truncated both
         * diagonal components to zero, so W+D/S+A produced no movement. */
        int direction_scale = (dx && dy) ? 46340 : 65536;
        wanted_x = (int32_t)((int64_t)dx * max_speed_q8 * direction_scale / 65536);
        wanted_y = (int32_t)((int64_t)dy * max_speed_q8 * direction_scale / 65536);
    }
    if (thrust) {
        /* Godot Vector2.move_toward: one acceleration budget for the vector,
         * including perpendicular turns and reversals, not one per axis. */
        int32_t change_x = wanted_x - player_vx, change_y = wanted_y - player_vy;
        unsigned change_speed = distance_q8(0, 0, change_x, change_y);
        if (change_speed > acceleration_step) {
            change_x = (int32_t)((int64_t)change_x * acceleration_step / (change_speed + 1u));
            change_y = (int32_t)((int64_t)change_y * acceleration_step / (change_speed + 1u));
        }
        player_vx += change_x;
        player_vy += change_y;
    } else {
        move_x = move_y = 0;
        player_vx = (int32_t)((int64_t)player_vx * coast_q24 / 16777216);
        player_vy = (int32_t)((int64_t)player_vy * coast_q24 / 16777216);
    }
    player_x = clamp_i((int)(player_x + player_vx / 60), 14 * Q8, (FIELD_W - 14) * Q8);
    player_y = clamp_i((int)(player_y + player_vy / 60), 14 * Q8, (FIELD_H - 14) * Q8);
follow_camera:
    if (phase != USURPER_PHASE_MENU) {
        /* CombatCamera.follow_center: 40% horizontal / one-third vertical
         * free movement, bounded by FIELD.grow_individual(32,64,32,64). */
        camera_x = clamp_i(camera_x, player_x - 128 * Q8, player_x + 128 * Q8);
        camera_y = clamp_i(camera_y, player_y - 60 * Q8, player_y + 60 * Q8);
        camera_x = clamp_i(camera_x, 288 * Q8, 512 * Q8);
        camera_y = clamp_i(camera_y, 116 * Q8, 364 * Q8);
        camera_x = ((camera_x + Q8 / 2) / Q8) * Q8;
        camera_y = ((camera_y + Q8 / 2) / Q8) * Q8;
    }
}

static void move_warden_pose(void *context,us_warden_ai_t *warden){
    (void)context;warden->x=us_sf_add(warden->x,us_sf_mul(warden->vx,(float)(1.0/60)));warden->y=us_sf_add(warden->y,us_sf_mul(warden->vy,(float)(1.0/60)));
}
static uint16_t enemy_ward_ticks(double ward){
    /* Both signed zeros produce zero in the original expression. Most slots
     * are unwarded; avoid software double arithmetic for those exact values. */
    union {double value;uint64_t bits;} word={.value=ward};
    return !(word.bits<<1)?0:(uint16_t)(ward*60+0.00001);
}
static void hostile_warden_counter(unsigned index,unsigned rail_tag,unsigned source_rail,unsigned source_other_rail,double duration){
    if(enemies[index].species!=SPECIES_WARDEN)return;
    us_warden_ai_t *warden=&warden_motion[index];warden->x=enemies[index].x/256.0f;warden->y=enemies[index].y/256.0f;
    us_ward_actor_t actors[ENEMY_MAX];unsigned count=0;
    for(unsigned i=0;i<ENEMY_MAX;++i){const Enemy *enemy=&enemies[i];if(!enemy_generation[i]||enemy->supply)continue;
        actors[count++]=(us_ward_actor_t){.x=enemy->x/256.0f,.y=enemy->y/256.0f,.ward=enemy_ward_left[i],.disruption=warden_motion[i].disruption,
            .id=i,.generation=enemy_generation[i],.protected_count=warden_motion[i].protected_count,.alive=enemy->active,
            .kind=enemy->species==SPECIES_WARDEN?US_WARD_ACTOR_WARDEN:enemy->species==SPECIES_FRAGMENT?US_WARD_ACTOR_FRAGMENT:US_WARD_ACTOR_NORMAL};}
    us_warden_enemy_counter(warden,index,actors,count,1,rail_tag,source_rail,source_other_rail,duration);
    for(unsigned i=0;i<count;++i){unsigned slot=actors[i].id;enemy_ward_left[slot]=actors[i].ward;enemies[slot].ward_ticks=enemy_ward_ticks(actors[i].ward);
        if(actors[i].kind==US_WARD_ACTOR_WARDEN){warden_motion[slot].disruption=actors[i].disruption;warden_motion[slot].protected_count=actors[i].protected_count;}}
}
static void hostile_warden_step(unsigned index){
    Enemy *enemy=&enemies[index];us_warden_ai_t *warden=&warden_motion[index];
    if((int32_t)(warden->x*Q8)!=enemy->x)warden->x=enemy->x/256.0f;
    if((int32_t)(warden->y*Q8)!=enemy->y)warden->y=enemy->y/256.0f;
    if((int32_t)(warden->vx*Q8)!=enemy->vx)warden->vx=enemy->vx/256.0f;
    if((int32_t)(warden->vy*Q8)!=enemy->vy)warden->vy=enemy->vy/256.0f;
    /* Battle clamps at 14 before child physics; aura runs after the move. */
    if(warden->x<14)warden->x=14;else if(warden->x>FIELD_W-14)warden->x=FIELD_W-14;
    if(warden->y<14)warden->y=14;else if(warden->y>FIELD_H-14)warden->y=FIELD_H-14;
    us_ward_actor_t actors[ENEMY_MAX];unsigned count=0;
    /* The AI ignores non-live actors. Keep live actors in original slot order
     * so nearest-target ties, generations, and aura update order stay exact.
     * A missing dead escort follows the same player-destination fallback. */
    for(unsigned i=0;i<ENEMY_MAX;++i){Enemy *sibling=&enemies[i];if(!sibling->active||sibling->supply)continue;
        actors[count++]=(us_ward_actor_t){.x=sibling->x/256.0f,.y=sibling->y/256.0f,.ward=enemy_ward_left[i],.id=i,.generation=enemy_generation[i],
        .alive=1,.kind=(uint8_t)(sibling->species==SPECIES_WARDEN?US_WARD_ACTOR_WARDEN:sibling->species==SPECIES_FRAGMENT?US_WARD_ACTOR_FRAGMENT:US_WARD_ACTOR_NORMAL)};}
    const double action_rates[8]={1,1.007,1.014,1.021,1.028,1.035,1.042,1.049};
    us_warden_enemy_step(warden,player_x/256.0f,player_y/256.0f,us_campaign_enemy_speed_float(campaign_stage_id,SPECIES_WARDEN)*(enemy->elite?2.0f:1.0f),action_rates[campaign_stage_id]*(enemy->elite?2:1),1,1,actors,count,NULL,move_warden_pose,NULL);
    enemy->x=(int32_t)(warden->x*Q8);enemy->y=(int32_t)(warden->y*Q8);enemy->vx=(int32_t)(warden->vx*Q8);enemy->vy=(int32_t)(warden->vy*Q8);
    for(unsigned i=0;i<count;++i)enemy_ward_left[actors[i].id]=actors[i].ward;
    /* Retain the old normalization of every slot, including inactive state. */
    for(unsigned i=0;i<ENEMY_MAX;++i)enemies[i].ward_ticks=enemy_ward_ticks(enemy_ward_left[i]);
}
static void move_enemies(void) {
    
    for (unsigned index = 0; index < ENEMY_MAX; ++index) {
        Enemy *enemy = &enemies[index];
        if (!enemy->active || enemy->invincible) continue;
        if(enemy->carrier_offspring&&enemy->contact_tick)--enemy->contact_tick;
        if (enemy->stun_cooldown) --enemy->stun_cooldown;
        if (enemy->knock_cooldown) --enemy->knock_cooldown;
        enemy_ward_left[index]=enemy_ward_left[index]>1.0/60?enemy_ward_left[index]-1.0/60:0;
        enemy->ward_ticks=enemy_ward_ticks(enemy_ward_left[index]);
        if (enemy->stun_ticks) { --enemy->stun_ticks; continue; }
        if(enemy->species==SPECIES_WARDEN)us_warden_status_step(&warden_status[index],1.0/60);
        int32_t dx = player_x - enemy->x, dy = player_y - enemy->y;
        /* Strict comparisons of floor(sqrt(d2)) with an integer radius are
         * exactly d2 < radius^2. Only orbital steering needs the actual root. */
        uint64_t distance_squared=(uint64_t)((int64_t)dx*dx)+(uint64_t)((int64_t)dy*dy);
        unsigned distance=0;
        int32_t wanted_x = 0, wanted_y = 0;
        unsigned speed = enemy_speed_q8(enemy->species);
        unsigned response = enemy->elite ? 8u : 16u;
        unsigned rammer_motion = 0u;
        unsigned precise_offspring=enemy->carrier_offspring&&enemy->species==SPECIES_DART;
        unsigned precise_motion=precise_offspring;
        if(precise_offspring) {
            us_dart_t *dart=&offspring_motion[index];
            if((int32_t)(dart->x*Q8)!=enemy->x)dart->x=enemy->x/256.0f;
            if((int32_t)(dart->y*Q8)!=enemy->y)dart->y=enemy->y/256.0f;
            if((int32_t)(dart->vx*Q8)!=enemy->vx)dart->vx=enemy->vx/256.0f;
            if((int32_t)(dart->vy*Q8)!=enemy->vy)dart->vy=enemy->vy/256.0f;
            us_dart_step(dart,player_x*256,player_y*256,player_vx*256,player_vy*256,
                us_campaign_enemy_speed_float(campaign_stage_id,SPECIES_DART));
            enemy->x=(int32_t)(dart->x*Q8);enemy->y=(int32_t)(dart->y*Q8);
            enemy->vx=(int32_t)(dart->vx*Q8);enemy->vy=(int32_t)(dart->vy*Q8);rammer_motion=1;
        } else if (enemy->species == SPECIES_DART) {
            dx += player_vx * 22 / 100;
            dy += player_vy * 22 / 100;
        } else if (enemy->species == SPECIES_SKIMMER || enemy->species == SPECIES_GUNNER) {
            int target_x = enemy->species == SPECIES_SKIMMER ?
                clamp_i(player_x, 76 * Q8, (FIELD_W - 76) * Q8) : player_x;
            int target_y = enemy->species == SPECIES_SKIMMER ?
                clamp_i(player_y, 76 * Q8, (FIELD_H - 76) * Q8) : player_y;
            dx = target_x - enemy->x;
            dy = target_y - enemy->y;
            distance_squared=(uint64_t)((int64_t)dx*dx)+(uint64_t)((int64_t)dy*dy);
            distance = distance_q8(enemy->x, enemy->y, target_x, target_y);
            unsigned facing = angle_to(dx, dy);
            int ox, oy;
            direction_q8(facing, &ox, &oy);
            if (enemy->species == SPECIES_SKIMMER) {
                int radial = clamp_i(((int)(distance / Q8) - 58) * 256 / 42, -205, 256);
                dx = ox * radial / 256 - oy * enemy->orbit_sign;
                dy = oy * radial / 256 + ox * enemy->orbit_sign;
                response = 12u;
            } else {
                int radial = clamp_i(((int)(distance / Q8) - 105) * 256 / 45, -256, 256);
                dx = ox * radial / 256 - oy * enemy->orbit_sign * 141 / 256;
                dy = oy * radial / 256 + ox * enemy->orbit_sign * 141 / 256;
            }
        } else if (enemy->species == SPECIES_RAMMER) {
            /* RammerEnemy uses APPROACH -> TELEGRAPH -> DASH -> RECOVER. */
            if (enemy->ai_state == 0u) {
                ++enemy->ai_tick;
                if (distance_squared < (uint64_t)(135u*Q8)*(135u*Q8) && enemy->ai_tick > 72u) {
                    enemy->ai_state = 1u;
                    enemy->ai_tick = 0u;
                    enemy->vx = enemy->vy = 0;
                }
            } else if (enemy->ai_state == 1u) {
                enemy->vx = enemy->vx * 90 / 100;
                enemy->vy = enemy->vy * 90 / 100;
                unsigned angle = angle_to(dx, dy);
                int dash_x, dash_y;
                direction_q8(angle, &dash_x, &dash_y);
                enemy->dash_vx = dash_x;
                enemy->dash_vy = dash_y;
                ++enemy->ai_tick;
                if (enemy->ai_tick >= 42u) {
                    enemy->ai_state = 2u;
                    enemy->ai_tick = 0u;
                }
                rammer_motion = 1u;
            } else if (enemy->ai_state == 2u) {
                /* dash_v* is a Q8 direction; convert it back after applying
                 * the source dash speed so one tick cannot teleport the hull
                 * to the arena edge. */
                enemy->vx = enemy->dash_vx * (int)(speed * 48u / 10u) / Q8;
                enemy->vy = enemy->dash_vy * (int)(speed * 48u / 10u) / Q8;
                ++enemy->ai_tick;
                if (enemy->ai_tick >= 26u) {
                    enemy->ai_state = 3u;
                    enemy->ai_tick = 0u;
                }
                rammer_motion = 1u;
            } else {
                enemy->vx = enemy->vx * 92 / 100;
                enemy->vy = enemy->vy * 92 / 100;
                ++enemy->ai_tick;
                if (enemy->ai_tick >= 51u) {
                    enemy->ai_state = 0u;
                    enemy->ai_tick = 0u;
                }
                rammer_motion = 1u;
            }
        } else if (enemy->species == SPECIES_BOMBER) {
            unsigned angle = angle_to(dx, dy) + 90u;
            int ox, oy;
            direction_q8(angle, &ox, &oy);
            dx = ox * 180 + dx / 3;
            dy = oy * 180 + dy / 3;
            response = 20u;
        } else if (enemy->species == SPECIES_WARDEN) {
            hostile_warden_step(index);rammer_motion=1;precise_motion=1;
        }
        if (!rammer_motion) {
            unsigned length = distance_q8(0, 0, dx, dy);
            if (!length) length = 1u;
            wanted_x = us_logic_scale_component(dx,speed,length);
            wanted_y = us_logic_scale_component(dy,speed,length);
            if (enemy->species == SPECIES_SKIMMER) {
                int predicted_x = (int)(enemy->x + wanted_x * 2 / 5);
                int predicted_y = (int)(enemy->y + wanted_y * 2 / 5);
                if (predicted_x < 22 * Q8 || predicted_x > (FIELD_W - 22) * Q8 ||
                    predicted_y < 22 * Q8 || predicted_y > (FIELD_H - 22) * Q8) {
                    dx = clamp_i(player_x, 76 * Q8, (FIELD_W - 76) * Q8) - enemy->x;
                    dy = clamp_i(player_y, 76 * Q8, (FIELD_H - 76) * Q8) - enemy->y;
                    length = distance;
                    if (!length) length = 1u;
                    wanted_x = us_logic_scale_component(dx,speed,length);
                    wanted_y = us_logic_scale_component(dy,speed,length);
                }
            }
            enemy->vx += (wanted_x - enemy->vx) / (int32_t)response;
            enemy->vy += (wanted_y - enemy->vy) / (int32_t)response;
        }
        if(!precise_motion) {
            us_warden_sweep_t guard={0};
            if(enemy->species==SPECIES_RAMMER&&enemy->ai_state==2u&&warden_field.active)
                guard=us_warden_sweep(&warden_field,enemy->x/256.0f,enemy->y/256.0f,(enemy->x+enemy->vx/60)/256.0f,(enemy->y+enemy->vy/60)/256.0f,12,1,0,enemy->max_hp,tech_effect("W9")!=0);
            if(guard.hit){
                enemy->x=(int32_t)(guard.x*Q8);enemy->y=(int32_t)(guard.y*Q8);enemy->vx=enemy->vy=0;enemy->ai_state=3;enemy->ai_tick=0;
                if(!enemy->capture_reserved)damage_enemy(index,guard.damage);
            }else{
                enemy->x = clamp_i((int)(enemy->x + enemy->vx / 60), 18 * Q8, (FIELD_W - 18) * Q8);
                enemy->y = clamp_i((int)(enemy->y + enemy->vy / 60), 18 * Q8, (FIELD_H - 18) * Q8);
            }
        }
        if (enemy->flash) --enemy->flash;
        if (enemy->species == SPECIES_GUNNER || enemy->species == SPECIES_BOMBER) {
            if (enemy->attack_tick) --enemy->attack_tick;
            else if (distance_squared < (uint64_t)(155u*Q8)*(155u*Q8)) {
                int32_t aim_x = player_x + player_vx * 32 / 100;
                int32_t aim_y = player_y + player_vy * 32 / 100;
                int shot = spawn_enemy_projectile(enemy->x, enemy->y,
                    angle_to(aim_x - enemy->x, aim_y - enemy->y), 7u, 82u);
                if (shot >= 0) {
                    projectiles[shot].emitter_index = (uint16_t)index;
                    projectiles[shot].emitter_generation = enemy_generation[index];
                }
                enemy->attack_tick = enemy->species == SPECIES_BOMBER ? 120u : 81u;
            }
        }
        unsigned contact_radius=precise_offspring?12u*Q8+Q8/2u:14u*Q8;
        if(precise_motion){
            int64_t contact_dx=(int64_t)enemy->x-player_x,contact_dy=(int64_t)enemy->y-player_y;
            distance_squared=(uint64_t)(contact_dx*contact_dx)+(uint64_t)(contact_dy*contact_dy);
        }
        unsigned inside=distance_squared<(uint64_t)contact_radius*contact_radius;
        unsigned enter=precise_offspring?inside&&!enemy->contact_inside&&!enemy->contact_tick:inside&&!contact_cooldown;
        if(precise_offspring)enemy->contact_inside=(uint8_t)inside;
        if (enter && !enemy->capture_reserved && !player_invincible_ticks && !rammer_contact_guard) {
            unsigned contact_damage = (unsigned)(((uint64_t)(7u + campaign_stage_id) *
                us_campaign_stage(campaign_stage_id)->enemy_damage_q16 + 32768u) / 65536u);
            if (campaign_stage_id == 6u && (enemy->species == SPECIES_DART || enemy->species == SPECIES_FRAGMENT || enemy->species == SPECIES_SPLITTER))
                contact_damage = enemy->species == SPECIES_DART ? 10u : enemy->species == SPECIES_FRAGMENT ? 6u : 14u;
            damage_player(contact_damage);
            if(precise_offspring)enemy->contact_tick=42u;
            else contact_cooldown = 42u;
            if (defeat_pending) return;
        }
    }
}

static void select_capture_target(void) {
    if (unit_count >= tech_capacity()) { candidate_index = 0xffffffffu; return; }
    if (aim_is_held) {
        unsigned target = aim_capture_target_index;
        candidate_index = target < ENEMY_MAX && enemies[target].active &&
            enemy_capture_eligible(&enemies[target]) && enemy_is_in_capture_range(&enemies[target]) ?
            target : 0xffffffffu;
        return;
    }
    if (manual_target_index < ENEMY_MAX && enemies[manual_target_index].active &&
        enemy_capture_eligible(&enemies[manual_target_index]) &&
        enemy_is_in_capture_range(&enemies[manual_target_index])) {
        candidate_index = manual_target_index;
        return;
    }
    unsigned best = 0xffffffffu, nearest = 0xffffffffu, highest_progress = 0u;
    for (unsigned i = 0; i < ENEMY_MAX; ++i) {
        if (!enemies[i].active) continue;
        if (!enemy_capture_eligible(&enemies[i])) continue;
        unsigned radius=tech_capture_radius()*Q8;
        if(us_logic_box_excludes(enemies[i].x,enemies[i].y,player_x,player_y,radius))continue;
        unsigned distance = distance_q8(enemies[i].x, enemies[i].y, player_x, player_y);
        if (distance > radius) continue;
        /* fleet.gd prioritizes progress, then the current candidate, then
         * proximity. Nearby ships crossing must not restart the link. */
        unsigned progress = enemies[i].capture_q8;
        if (best == 0xffffffffu || progress > highest_progress ||
            (progress == highest_progress &&
             (i == candidate_index || (best != candidate_index && distance < nearest)))) {
            highest_progress = progress;
            nearest = distance;
            best = i;
        }
    }
    candidate_index = best;
}

static void capture_step(void) {
    select_capture_target();
    if (candidate_index != capture_focus_index) {
        capture_focus_index = candidate_index;
        capture_focus_ticks = 0u;
    } else if (candidate_index < ENEMY_MAX && capture_focus_ticks < 48u) ++capture_focus_ticks;
    reserve_capture_target(candidate_index);
    if (candidate_index < ENEMY_MAX) {
        Enemy *enemy = &enemies[candidate_index];
        enemy->capture_away = 0;
        unsigned power = 30u * tech_control_q8();
        if (phase == USURPER_PHASE_TUTORIAL && tutorial_step < 2u) power = 0;
        if (phase == USURPER_PHASE_TUTORIAL && tutorial_step == 3u) power = 0;
        if (phase == USURPER_PHASE_TUTORIAL && tutorial_step >= 3u && tutorial_step != 5u) power = 0;
        if (tech_effect("C4") && capture_focus_ticks >= 48u) power = power * 6u / 5u;
        if (power) {
            unsigned work = enemy_work(enemy->species);
            enemy->capture_fraction += power;
            unsigned increment = enemy->capture_fraction / (work * 60u);
            enemy->capture_fraction %= work * 60u;
            if (enemy->capture_q8 + increment >= 256u) {
                enemy->capture_q8 = 256u;
                complete_capture(candidate_index);
                candidate_index = 0xffffffffu;
            } else enemy->capture_q8 = (uint16_t)(enemy->capture_q8 + increment);
        }
    }
    for (unsigned i = 0; i < ENEMY_MAX; ++i) {
        Enemy *enemy = &enemies[i];
        if (!enemy->active || !enemy->capture_q8 || i == candidate_index) continue;
        ++enemy->capture_away;
        unsigned grace = 72u;
        int c3 = tech_index("C3");
        if (c3 >= 0 && tech_effective_rank((unsigned)c3)) grace = tech_effective_rank((unsigned)c3) > 1u ? 180u : 120u;
        if (enemy->capture_away > grace) {
            unsigned decay_rank = 0;
            int c7 = tech_index("C7");
            if (c7 >= 0) decay_rank = tech_effective_rank((unsigned)c7);
            unsigned decay = decay_rank ? (decay_rank > 1u ? 31u : 46u) : 64u;
            enemy->decay_fraction += decay;
            unsigned decrement = enemy->decay_fraction / 60u;
            enemy->decay_fraction %= 60u;
            enemy->capture_q8 = enemy->capture_q8 > decrement ? (uint16_t)(enemy->capture_q8 - decrement) : 0u;
        }
    }
}

static void pickup_step(void) {
    for (unsigned i = 0; i < PICKUP_MAX; ++i) {
        Pickup *pickup = &pickups[i];
        if (!pickup->active) continue;
        ++pickup->age;
        int32_t dx = player_x - pickup->x, dy = player_y - pickup->y;
        unsigned distance = distance_q8(pickup->x, pickup->y, player_x, player_y);
        {
            unsigned speed = 360u * (60u + min_u(pickup->age, 60u)) * Q8 / 60u;
            if (distance) {
                unsigned step = min_u(distance, speed / 60u);
                pickup->x += (int32_t)((int64_t)dx * step / distance);
                pickup->y += (int32_t)((int64_t)dy * step / distance);
            }
        }
        distance = distance_q8(pickup->x, pickup->y, player_x, player_y);
        if (distance < 12u * Q8) {
            alloy_run += pickup->amount;
            if(phase!=USURPER_PHASE_MENU)usurper_audio_event(US_AUDIO_PICKUP);
            receive_pickup_recovery(pickup->recovery_q8);
            pickup->active = 0;
        }
    }
}

static void unit_passive_step(void) {
    warden_pulse_clock=warden_pulse_clock>1.0/60?warden_pulse_clock-1.0/60:0;
    for (unsigned slot = 0; slot < unit_count; ++slot) {
        unsigned species = unit_species[slot];
        if(species==SPECIES_WARDEN){
            unit_support_clock[slot]-=1.0/60;
            if(unit_support_clock[slot]<=0){int32_t x,y;unit_world_position(slot,&x,&y);unit_support_clock[slot]=1.5;warden_passive(slot,x,y);}
            continue;
        }
        if (species == SPECIES_GUNNER && unit_burst_left[slot] &&
            (!unit_burst_delay[slot] || !--unit_burst_delay[slot])) {
            int32_t x, y;
            unit_world_position(slot, &x, &y);
            int victim = nearest_gunner_target(x, y);
            if (victim >= 0) {
                unsigned count = tech_effect("G8") ? 3u : 2u;
                unsigned base = count == 3u ? 880u : 1120u;
                unsigned angle = gunner_direction(x, y, (unsigned)victim);
                unit_facing_q8[slot] = angle * Q8;
                spawn_ally_projectile(x, y, angle, gunner_damage(base, 0u), 420u, 0u);
            }
            --unit_burst_left[slot];
            unit_burst_delay[slot] = 5u;
        }
        if (unit_attack_tick[slot]) {
            --unit_attack_tick[slot];
            if (species != SPECIES_GUNNER || unit_attack_tick[slot]) continue;
        }
        int32_t origin_x, origin_y;
        unit_world_position(slot, &origin_x, &origin_y);
        int target = nearest_gunner_target(origin_x, origin_y);
        if (species == SPECIES_DART) {
            /* GrowthAbilities.passive deliberately returns for a Dart.  Its
             * hull is launched only by release_front(), with the projectile
             * body attached as the Dart's visible casing. */
            unit_attack_tick[slot] = 30u;
        } else if (species == SPECIES_SKIMMER || species == SPECIES_RAMMER) {
            unsigned damage_q8 = tech_damage_q8(species == SPECIES_RAMMER ? 800u : 400u, species, 0u);
            for (unsigned i = 0; i < ATTACK_TARGET_MAX; ++i) {
                int32_t tx, ty;
                if (!attack_target_selectable(i) || !attack_target_position(i, &tx, &ty)) continue;
                if (distance_q8(tx, ty, origin_x, origin_y) < 22u * Q8) {
                    if (i < ENEMY_MAX) {
                        if (enemies[i].invincible) continue;
                        damage_enemy(i,damage_q8/256.0);
                        if (enemies[i].active && species == SPECIES_RAMMER && tech_effect("A3") && !enemies[i].knock_cooldown) {
                            int32_t dx = tx - origin_x, dy = ty - origin_y;
                            unsigned length = distance_q8(0, 0, dx, dy);
                            if (length) {
                                enemies[i].x += (int32_t)((int64_t)dx * 7u * Q8 / length);
                                enemies[i].y += (int32_t)((int64_t)dy * 7u * Q8 / length);
                            }
                            enemies[i].knock_cooldown = 48u;
                        }
                    } else damage_boss_target(i, damage_q8, 0u);
                }
            }
            unit_attack_tick[slot] = species == SPECIES_SKIMMER && tech_effect("S7") ? 18u : 30u;
        } else if (species == SPECIES_GUNNER) {
            unit_attack_tick[slot] = (uint16_t)((9000u + 8u * tech_effect("F4") +
                15u * tech_effect("G2") + 99u) /
                (100u + 8u * tech_effect("F4") + 15u * tech_effect("G2")));
            if (target >= 0) {
                unsigned count = tech_effect("G8") ? 3u : tech_effect("G4") ? 2u : 1u;
                unsigned base = count == 3u ? 880u : count == 2u ? 1120u : 1600u;
                unsigned angle = gunner_direction(origin_x, origin_y, (unsigned)target);
                unit_facing_q8[slot] = angle * Q8;
                spawn_ally_projectile(origin_x, origin_y, angle, gunner_damage(base, 0u), 420u, 0u);
                unit_burst_left[slot] = (uint8_t)(count - 1u);
                unit_burst_delay[slot] = 5u;
            }
        } else unit_attack_tick[slot] = 60u;
    }
}

/* Integer vectoring keeps sub-degree collision precision without pulling
 * floating-point trigonometry into the RV32 firmware. */
static unsigned skimmer_angle_q8(int32_t dx, int32_t dy) {
    static const int32_t atan_degrees_q16[16] = {
        2949120, 1740967, 919879, 466945, 234379, 117304, 58666, 29335,
        14668, 7334, 3667, 1833, 917, 458, 229, 115
    };
    if (!dx && !dy) return 0u;
    unsigned ax = (unsigned)abs_i(dx), ay = (unsigned)abs_i(dy);
    int32_t x = (int32_t)max_u(ax, ay), y = (int32_t)min_u(ax, ay);
    while (x < (1 << 26)) { x *= 2; y *= 2; }
    int32_t angle = 0;
    for (unsigned i = 0; i < 16u && y; ++i) {
        int32_t old_x = x;
        if (y > 0) { x += y >> i; y -= old_x >> i; angle += atan_degrees_q16[i]; }
        else { x -= y >> i; y += old_x >> i; angle -= atan_degrees_q16[i]; }
    }
    unsigned acute = angle > 0 ? (unsigned)(angle + 128) / 256u : 0u;
    unsigned base = ax >= ay ? acute : 90u * Q8 - acute;
    if (dx >= 0 && dy >= 0) return base;
    if (dx < 0 && dy >= 0) return 180u * Q8 - base;
    if (dx < 0 && dy < 0) return 180u * Q8 + base;
    return (360u * Q8 - base) % (360u * Q8);
}

/* Pure integer geometry can be reused by independently timed sweeps. Target
 * presence and hit clocks remain live; all four coordinates form the key, so
 * movement, slot reuse, phase changes and reset cannot retain stale geometry. */
typedef struct {
    int32_t tx,ty,px,py;
    unsigned distance,angle,valid;
} SkimmerGeometry;
static SkimmerGeometry *skimmer_geometry(unsigned target,int32_t tx,int32_t ty) {
    static SkimmerGeometry cache[ATTACK_TARGET_MAX];
    SkimmerGeometry *g=&cache[target];
    if(!g->valid||g->tx!=tx||g->ty!=ty||g->px!=player_x||g->py!=player_y) {
        g->tx=tx;g->ty=ty;g->px=player_x;g->py=player_y;
        g->distance=distance_q8(tx,ty,player_x,player_y);g->valid=1u;
    }
    return g;
}
static unsigned skimmer_thickness(unsigned distance,unsigned margin) {
    typedef struct {unsigned distance,margin,value,valid;} Thickness;
    static Thickness cache[64];
    Thickness *entry=&cache[(distance^(distance>>6)^(margin*17u))&63u];
    if(!entry->valid||entry->distance!=distance||entry->margin!=margin) {
        entry->distance=distance;entry->margin=margin;
        entry->value=skimmer_angle_q8((int32_t)distance,(int32_t)(margin*Q8+14u*Q8*15u/100u));
        entry->valid=1u;
    }
    return entry->value;
}

static int skimmer_touches(unsigned radius, unsigned previous_q8, unsigned current_q8,
                           unsigned target_angle_q8, unsigned distance, unsigned margin,
                           int turn) {
    if (distance > (radius + margin) * Q8) return 0;
    if (distance < (margin + 2u) * Q8) return 1;
    const unsigned full_turn = 360u * Q8;
    unsigned thickness = skimmer_thickness(distance,margin);
    unsigned sweep = turn > 0 ? (current_q8 + full_turn - previous_q8) % full_turn :
        (previous_q8 + full_turn - current_q8) % full_turn;
    unsigned directed = turn > 0 ? (target_angle_q8 + full_turn - previous_q8) % full_turn :
        (previous_q8 + full_turn - target_angle_q8) % full_turn;
    return directed <= sweep + thickness || directed >= full_turn - thickness;
}

static void skimmer_release_step(void) {
    for (unsigned slot = 0; slot <= SKIMMER_SWEEP_MAX; ++slot) {
        SkimmerSweep *scan = &skimmer_sweeps[slot];
        if (!scan->ticks_left) continue;
        ++scan->elapsed_ticks;
        unsigned previous = scan->angle_q16 / 256u;
        scan->angle_q16 = (scan->angle_q16 + scan->step_q16) % (360u * 65536u);
        unsigned current = scan->angle_q16 / 256u;
        unsigned echo_previous = (2u * scan->start_q16 / 256u + 360u * Q8 - previous) % (360u * Q8);
        unsigned echo_current = (2u * scan->start_q16 / 256u + 360u * Q8 - current) % (360u * Q8);
        for (unsigned visit = 0; visit < ATTACK_TARGET_MAX; ++visit) {
            /* Godot inserts the boss in the enemy group before its armor. */
            unsigned i = visit < ENEMY_MAX ? visit : visit == ENEMY_MAX ? BOSS_TARGET_CORE : visit - 1u;
            /* Neither beam can damage this target during its own cooldown.
             * Keep both independent repeat clocks and the original visit order. */
            if (scan->elapsed_ticks < scan->hit_tick[i] &&
                (!scan->echo || scan->elapsed_ticks < scan->echo_hit_tick[i])) continue;
            int32_t tx, ty;
            if (!attack_target_position(i, &tx, &ty)) continue;
            if (i < ENEMY_MAX && (enemies[i].invincible || enemies[i].capture_reserved)) continue;
            unsigned margin = i == BOSS_TARGET_CORE ? 15u : 8u;
            if(us_logic_box_excludes(tx,ty,player_x,player_y,(scan->radius+margin)*Q8))continue;
            SkimmerGeometry *geometry=skimmer_geometry(i,tx,ty);
            unsigned distance=geometry->distance;
            if (distance > (scan->radius + margin) * Q8) continue;
            if(!(geometry->valid&2u)) {
                geometry->angle=skimmer_angle_q8(tx-player_x,ty-player_y);
                geometry->valid|=2u;
            }
            unsigned angle=geometry->angle;
            int main_hit = scan->elapsed_ticks >= scan->hit_tick[i] &&
                skimmer_touches(scan->radius, previous, current, angle, distance, margin, 1);
            int echo_hit = scan->echo && scan->elapsed_ticks >= scan->echo_hit_tick[i] &&
                skimmer_touches(scan->radius, echo_previous, echo_current, angle, distance, margin, -1);
            if (!main_hit && !echo_hit) continue;
            unsigned total_damage_q8 = scan->damage_q8 * (unsigned)main_hit +
                scan->damage_q8 / 2u * (unsigned)echo_hit;
            if(scan->form_owned) {
                double damage=scan->form_damage*(main_hit+0.5*echo_hit);
                if(i>=ENEMY_MAX)damage_boss_form(i,damage,0);
                else damage_enemy(i,damage);
            } else if (i >= ENEMY_MAX) damage_boss_target(i, scan->damage_q8 * (unsigned)main_hit +
                        scan->damage_q8 / 2u * (unsigned)echo_hit, 0u);
            else damage_enemy(i,total_damage_q8/256.0);
            if (main_hit) scan->hit_tick[i] = scan->elapsed_ticks + scan->repeat_ticks;
            if (echo_hit) scan->echo_hit_tick[i] = scan->elapsed_ticks + scan->repeat_ticks;
            if (phase == USURPER_PHASE_RESULT) return;
        }
        if(!scan->form_owned)--scan->ticks_left;
    }
}

static void tutorial_set_step(unsigned step) {
    tutorial_step = step;
    tutorial_timer = 0;
    tutorial_complete_delay = 0;
    tutorial_start_x = (unsigned)player_x;
    tutorial_start_y = (unsigned)player_y;
    tutorial_start_angle = player_angle;
    if (step == 0u) message("TURN THE HULL / I J K L");
    else if (step == 1u) message("MOVE WITH W A S D");
    else if (step == 2u) {
        spawn_practice(SPECIES_DART, 187u, 174u, &practice_dart_index);
        message("ENTER THE RING TO USURP");
    } else if (step == 3u) message("HOLD F, THEN PRESS F TO RELEASE");
    else if (step == 4u) {
        spawn_practice(SPECIES_DART, 97u, 184u, &practice_dart_index);
        spawn_practice(SPECIES_SKIMMER, 194u, 184u, &practice_skimmer_index);
        if (practice_dart_index < ENEMY_MAX) enemies[practice_dart_index].capture_q8 = 102u;
        tutorial_locked = 0;
        message("PRESS T TO MARK THE OTHER CONTACT");
    } else if (step == 5u) message("CAPTURE A DART AND A SKIMMER");
    else if (step == 6u) {
        tutorial_wheel_start = unit_count ? unit_species[0] : 0u;
        message("PRESS V TO CYCLE THE FLEET");
    } else if (step == 7u) message("WATCH HULL IN THE UPPER LEFT");
    else if (step == 8u) message("SALVAGE RESTORES HULL AND ADDS ALLOY");
}

static void clear_tutorial_targets(void) {
    for (unsigned i = 0; i < ENEMY_MAX; ++i)
        if (enemies[i].active && enemies[i].invincible) enemies[i].active = 0;
    memset(projectiles, 0, sizeof(projectiles));
    memset(gunner_volleys, 0, sizeof(gunner_volleys));
    memset(unit_burst_left, 0, sizeof(unit_burst_left));
    memset(unit_species, 0, sizeof(unit_species));
    memset(unit_attack_tick, 0, sizeof(unit_attack_tick));
    memset(unit_support_clock,0,sizeof(unit_support_clock));
    memset(unit_join_ticks, 0, sizeof(unit_join_ticks));
    memset(unit_join_dx, 0, sizeof(unit_join_dx));
    memset(unit_join_dy, 0, sizeof(unit_join_dy));
    memset(unit_bearing_q8, 0, sizeof(unit_bearing_q8));
    memset(skimmer_sweeps, 0, sizeof(skimmer_sweeps));
          unit_count = 0;
          memset(gunner_volleys, 0, sizeof(gunner_volleys));
    captured_total = released_total = kills_total = 0;
    alloy_run = alloy_remainder = 0;
    memset(pickups, 0, sizeof(pickups));
    memset(arrivals, 0, sizeof(arrivals));
    candidate_index = manual_target_index = aim_capture_target_index = 0xffffffffu;
    practice_dart_index = practice_skimmer_index = 0xffffffffu;
    player_vx = player_vy = 0;
    player_move_ticks = 0;
    aim_is_held = aim_charge_ticks = 0;
    aim_slow_phase = aim_wall_ticks = 0;
    aim_capture_target_index = 0xffffffffu;
    release_flash_ticks = 0;
}

static void begin_tutorial(void) {
    memset(enemies, 0, sizeof(enemies));
    memset(projectiles, 0, sizeof(projectiles));
    memset(arrivals, 0, sizeof(arrivals));
    memset(unit_species, 0, sizeof(unit_species));
    memset(unit_attack_tick, 0, sizeof(unit_attack_tick));
    memset(unit_support_clock,0,sizeof(unit_support_clock));
    memset(unit_join_ticks, 0, sizeof(unit_join_ticks));
    memset(unit_join_dx, 0, sizeof(unit_join_dx));
    memset(unit_join_dy, 0, sizeof(unit_join_dy));
    memset(unit_bearing_q8, 0, sizeof(unit_bearing_q8));
    unit_count = 0;
    phase = USURPER_PHASE_TUTORIAL;
    tutorial_done = tutorial_releases = tutorial_locked = 0;
    player_hp = player_hp_max;player_damage_fraction=0;
    tutorial_set_step(0u);
}

static void launch_from_research(void) {
#ifdef USURPER_EXHIBITION_THREE_STAGES
    if(campaign_stage_id>=3u){message("EXHIBITION: SELECT STAGE 1 TO 3 WITH COMMA/PERIOD");return;}
#endif
    reset_run();
#ifdef USURPER_PROFILE_ENABLED
    profile_visited_stage_mask|=1u<<campaign_stage_id;
#endif
    phase = USURPER_PHASE_ARRIVAL;
    transition_ticks = 43u;
    player_x = 40u * Q8;
    player_y = 240u * Q8;
    player_angle = 0u;
    message("OPENING / LOST SPACESHIP SIGNAL");
}

static void tutorial_step_update(void) {
    ++tutorial_timer;
    if (tutorial_complete_delay) {
        --tutorial_complete_delay;
        if (!tutorial_complete_delay) {
            if (tutorial_step == 8u) {
                tutorial_done = tutorial_seen = 1u;
                clear_tutorial_targets();
                phase = USURPER_PHASE_BATTLE;
                level_tick = spawn_index = 0;
                player_hp = player_hp_max;player_damage_fraction=0;
                message("SECTOR 1-1 / TUTORIAL COMPLETE");
            } else tutorial_set_step(tutorial_step + 1u);
        }
        return;
    }
    unsigned complete = 0;
    if (tutorial_step == 0u) {
        unsigned delta = (unsigned)abs_i(angle_delta((int)tutorial_start_angle, (int)player_angle));
        complete = tutorial_rotated && delta >= 18u;
    } else if (tutorial_step == 1u) {
        complete = (player_move_ticks || (controls_held & 15u)) &&
            distance_q8((int32_t)tutorial_start_x, (int32_t)tutorial_start_y, player_x, player_y) >= 28u * Q8;
    } else if (tutorial_step == 2u) complete = captured_total > 0u;
    else if (tutorial_step == 3u) complete = tutorial_releases > 0u;
    else if (tutorial_step == 4u) {
        complete = tutorial_locked && candidate_index == practice_skimmer_index &&
            practice_skimmer_index < ENEMY_MAX &&
            distance_q8(enemies[practice_skimmer_index].x, enemies[practice_skimmer_index].y, player_x, player_y) <= tech_capture_radius() * Q8;
    } else if (tutorial_step == 5u) {
        unsigned captured_types = 0;
        for (unsigned i = 0; i < unit_count; ++i) {
            if (unit_species[i] == SPECIES_DART) captured_types |= 1u;
            if (unit_species[i] == SPECIES_SKIMMER) captured_types |= 2u;
        }
        complete = captured_types == 3u;
    }
    else if (tutorial_step == 6u) complete = unit_count > 1u && unit_species[0] != tutorial_wheel_start;
    else if (tutorial_step == 7u) complete = tutorial_timer >= 420u;
    else if (tutorial_step == 8u) complete = tutorial_timer >= 300u;
    if (complete && tutorial_timer >= 36u) tutorial_complete_delay = TUTORIAL_ADVANCE_TICKS;
}

static void start_defeat(void) {
    if (defeat_pending || (phase != USURPER_PHASE_BATTLE && phase != USURPER_PHASE_MENU)) return;
    /* Arena resets the complete sortie immediately after this simulation step.
     * Do not destroy/clear it here and then clear it a second time in reset_run.
     * Ordinary campaign defeat, rewards and departure remain below unchanged. */
    
    us_supplies_stop(&supplies,&supply_callbacks);
    warden_field.active=0;warden_guard_left=0;memset(interceptors,0,sizeof(interceptors));memset(warden_claims,0,sizeof(warden_claims));
    collect_remaining_pickups();
    memset(gunner_volleys, 0, sizeof(gunner_volleys));
    memset(&rammer_charge, 0, sizeof(rammer_charge));
    cancel_combat_aim();
    /* fleet.destroy_fleet(), then _emit_complete(false): wait 0.55 s,
     * keep the camera fixed and warp the controller 760 px over 0.68 s
     * using TRANS_QUART / EASE_IN before exposing the results. */
    damage_flash_ticks = 24u;
    defeat_pending = DEFEAT_HOLD_TICKS + DEPARTURE_TICKS;
    defeat_tick = 0u;
    departure_start_x = player_x; departure_start_y = player_y;
    departure_angle = player_angle;
    unit_count = 0;
    capture_flash_ticks = release_flash_ticks = 0;
    candidate_index = manual_target_index = aim_capture_target_index = 0xffffffffu;
    aim_is_held = aim_slow_phase = aim_wall_ticks = 0;
    memset(skimmer_sweeps, 0, sizeof(skimmer_sweeps));
    player_vx = player_vy = 0;
    message("HULL LOST / CONTROLLER DEPARTING");
}

static unsigned advance_defeat_departure(void) {
    ++defeat_tick;
    player_angle = departure_angle;
    if (defeat_tick > DEFEAT_HOLD_TICKS) {
        unsigned tick = min_u(defeat_tick - DEFEAT_HOLD_TICKS, DEPARTURE_TICKS);
        uint64_t quart = (uint64_t)tick * tick * tick * tick;
        uint64_t duration = (uint64_t)DEPARTURE_TICKS * DEPARTURE_TICKS * DEPARTURE_TICKS * DEPARTURE_TICKS;
        int dx, dy;
        direction_q8(departure_angle, &dx, &dy);
        int32_t distance = (int32_t)(760u * Q8 * quart / duration);
        player_x = departure_start_x + (int32_t)((int64_t)dx * distance / Q8);
        player_y = departure_start_y + (int32_t)((int64_t)dy * distance / Q8);
    }
    return !--defeat_pending;
}

static void research_step(void) {
    if (controls_timeout && !ui_dragging) {
        int dx = (int)((controls_held >> 3) & 1u) - (int)((controls_held >> 2) & 1u);
        int dy = (int)((controls_held >> 1) & 1u) - (int)(controls_held & 1u);
        tech_pan_x += dx * 4 * 256 / (int)tech_zoom_q8;
        tech_pan_y += dy * 4 * 256 / (int)tech_zoom_q8;
        if (dx || dy) tech_tooltip_visible = 0;
    }
    if (reset_requested) {
        reset_requested = 0;
        reset_run();
        phase = USURPER_PHASE_TECH;
        message("RESEARCH CORE SYSTEMS / ENTER TO DEPLOY");
    }
    if (next_tech_requested) {
        next_tech_requested = 0;
        for (unsigned offset = 1; offset <= USURPER_TECH_NODE_COUNT; ++offset) {
            unsigned index = (selected_node + offset) % USURPER_TECH_NODE_COUNT;
            if (tech_revealed(index)) {
                selected_node = index;
                tech_pan_x = usurper_tech_nodes[index].x;
                tech_pan_y = usurper_tech_nodes[index].y;
                tech_tooltip_visible = 1u;
                break;
            }
        }
    }
    if (buy_requested) {
        buy_requested = 0;
        if (tech_can_buy(selected_node)) {
            unsigned cost = tech_cost(selected_node);
            alloy_wallet -= cost;
            ++tech_owned[selected_node];
            usurper_audio_event(US_AUDIO_RESEARCH);
            tech_feedback_ticks = 19u;
            tech_feedback_purchase = 1u;
            player_hp_max = tech_hull();
            player_shield_max = tech_shield();
            player_hp = min_u(player_hp, player_hp_max);
            player_shield = min_u(player_shield, player_shield_max);
            message("RESEARCH ACQUIRED");
        } else message(tech_unlocked(selected_node) ? "INSUFFICIENT ALLOY OR MAX RANK" : "RESEARCH REQUIREMENT LOCKED");
    }
    if (launch_requested) {
        launch_requested = 0;
        int core = tech_index("核心");
        if (core >= 0 && tech_rank((unsigned)core) == 0u) {
            selected_node = (unsigned)core;
            tech_tooltip_visible = 1u;
            message("RESEARCH CORE REQUIRED");
        } else {
            launch_from_research();
        }
    }
}

static void controller_contact_step(void) {
    if(!boss_active||boss_grace_ticks)return;
    unsigned inside=distance_q8(boss_x,boss_y,player_x,player_y)<25u*Q8+Q8/2u;
    if(inside&&!controller_contact_inside[0]&&!controller_contact_ticks[0]&&!player_invincible_ticks&&!rammer_contact_guard){
        damage_player(26u);controller_contact_ticks[0]=48u;
    }
    controller_contact_inside[0]=(uint8_t)inside;
    for(unsigned i=0;i<US_CONTROLLER_FIGHTERS;++i){
        const us_controller_fighter_t *fighter=&controller_boss.fighters[i];unsigned slot=i+1;
        if(controller_contact_serial[i]!=fighter->serial){
            controller_contact_serial[i]=fighter->serial;controller_contact_ticks[slot]=0;controller_contact_inside[slot]=0;
        }
        if(!fighter->present||fighter->queued){controller_contact_inside[slot]=0;continue;}
        unsigned radius=(unsigned)((7.0*0.65*(1.0+fighter->hit_kick*0.1)+5.5)*Q8);
        inside=distance_q8((int32_t)(fighter->x*Q8),(int32_t)(fighter->y*Q8),player_x,player_y)<radius;
        if(inside&&!controller_contact_inside[slot]&&!controller_contact_ticks[slot]&&!player_invincible_ticks&&!rammer_contact_guard){
            damage_player(6u);controller_contact_ticks[slot]=42u;
        }
        controller_contact_inside[slot]=(uint8_t)inside;
    }
}

static void step_scene(void) {
    if(loadout_panel){
        if(phase!=USURPER_PHASE_MENU&&phase!=USURPER_PHASE_TECH)loadout_panel=0;
        else{if(launch_requested||pause_requested){launch_requested=pause_requested=0;loadout_panel=0;}return;}
    }
    ++frame_tick;
    previous_player_angle = player_angle;
    unsigned was_slow_render = slow_motion_render_active;
    slow_motion_render_active = 0;
    if (pointer_click_ticks) --pointer_click_ticks;
    if (tech_feedback_ticks) --tech_feedback_ticks;
    if (controls_timeout && !--controls_timeout) {
        controls_held = 0;
        player_move_ticks = 0;
        aim_is_held = 0;
        aim_is_form = 0;
        aim_slow_phase = aim_wall_ticks = 0;
        ui_dragging = 0;
        pointer_visible = 0;
    }
    if (defeat_pending && phase != USURPER_PHASE_MENU)
        pause_requested = launch_requested = skip_requested = reset_requested = 0;
    if (pause_requested && gameplay_input_allowed()) {
        snapshot_scene_positions();
        pause_requested = 0u;
        paused_phase = phase;
        cancel_combat_aim();
        phase = USURPER_PHASE_PAUSED;
        message("SORTIE PAUSED / P TO RESUME");
        return;
    }
    if (!defeat_pending && controls_timeout && (phase == USURPER_PHASE_BATTLE || phase == USURPER_PHASE_TUTORIAL)) {
        move_x = ((controls_held >> 3) & 1u) - (int)((controls_held >> 2) & 1u);
        move_y = ((controls_held >> 1) & 1u) - (int)(controls_held & 1u);
        player_move_ticks = (move_x || move_y) ? 2u : 0u;
        int delta = angle_delta((int)player_angle, (int)aim_target_angle);
        unsigned turn_step = aim_is_held && aim_wall_ticks < 60u ? 5u : 22u;
        turn_step=us_form_turn_step(turn_step,player_dart_mobility());
        if (!rammer_charge.remaining_q16)
            player_angle = angle_offset(player_angle, clamp_i(delta, -(int)turn_step, (int)turn_step));
    }
    /* Godot's capture/release aim mode runs at Engine.time_scale=0.22.
     * Keep input and pointer updates real-time while advancing combat at the
     * same visible cadence as the reference. */
    if (!defeat_pending && aim_is_held && (phase == USURPER_PHASE_BATTLE || phase == USURPER_PHASE_TUTORIAL)) {
        /* fleet.gd restores Engine.time_scale after one real second even if
         * the player keeps holding the aim button. */
        if (aim_wall_ticks < 60u) ++aim_wall_ticks;
        if (aim_wall_ticks < 60u) {
            slow_motion_render_active = 1u;
            /* Begin at the current position; do not rewind the visible world
             * when entering slow motion. Hold this history until combat moves. */
            if (!was_slow_render) snapshot_scene_positions();
            aim_slow_phase += 22u;
            if (aim_slow_phase < 100u) return;
            aim_slow_phase -= 100u;
        } else aim_slow_phase = 0u;
    } else {
        aim_slow_phase = 0u;
        aim_wall_ticks = 0u;
    }
    snapshot_scene_positions();
    if (phase == USURPER_PHASE_MENU || gameplay_input_allowed()) {
        advance_unit_joins();
        if (release_flash_ticks) --release_flash_ticks;
        if (damage_flash_ticks) --damage_flash_ticks;
        if (release_cooldown_ticks) --release_cooldown_ticks;
        orbit_phase_q8 = (orbit_phase_q8 + 34u) & 0xffffu;
        advance_death_fx();
    }
    if (phase == USURPER_PHASE_MENU) {
        menu_demo_step();
        if (pause_requested) {
            pause_requested = 0;
            menu_confirm = 0;
        }
        if (launch_requested) {
            launch_requested = 0;
            if (menu_confirm) {
                if (menu_confirm_selection) {
                    usurper_scene_reset();
                    phase = USURPER_PHASE_TECH;
                }
                menu_confirm = 0;
            } else if (menu_selection == 0u) {
                reset_run();
                reset_tech_view();
                phase = USURPER_PHASE_TECH;
            } else if (menu_selection == 1u) {
                menu_confirm = 1u;
                menu_confirm_selection = 0;
            } else if (menu_selection == 2u) {
                ui_return_phase = phase;
                settings_selection = 0u;
                phase = USURPER_PHASE_SETTINGS;
            }
        }
        return;
    }
    if (phase == USURPER_PHASE_SETTINGS) {
        if(pause_requested||(launch_requested&&settings_selection==6u)){
            pause_requested=launch_requested=0;phase=ui_return_phase;
        }else if(launch_requested){launch_requested=0;settings_adjust(1);}
        return;
    }
    if (phase == USURPER_PHASE_CREDITS || phase == USURPER_PHASE_QUIT) {
        if (pause_requested || launch_requested) {
            pause_requested = launch_requested = 0;
            phase = ui_return_phase;
        }
        return;
    }
    if (pause_requested && phase == USURPER_PHASE_TECH) {
        pause_requested = 0;
        enter_main_menu();
        return;
    }
    if (reset_requested && phase != USURPER_PHASE_TECH) {
        reset_requested = 0;
        launch_requested = 0;
        settle_abandoned_run();
        reset_run();
        reset_tech_view();
        phase = USURPER_PHASE_TECH;
        message("RESEARCH CORE SYSTEMS / B TO BUY / ENTER TO DEPLOY");
        return;
    }
    if (phase == USURPER_PHASE_TECH) { research_step(); return; }
    if (phase == USURPER_PHASE_PAUSED) {
        if (campaign_stage_id == 5u && boundary_boss.crush_holding) {
            us_boundary_cinematic_tick(&boundary_boss, 1.0 / 60); us_boundary_flush_deferred(&boundary_boss);
        }
        if (pause_requested || launch_requested) {
            pause_requested = launch_requested = 0;
            phase = paused_phase;
            message("SORTIE RESUMED");
        }
        return;
    }
    if (pause_requested) {
        pause_requested = 0;
        if (phase == USURPER_PHASE_BATTLE || phase == USURPER_PHASE_TUTORIAL) {
            paused_phase = phase;
            phase = USURPER_PHASE_PAUSED;
            message("SORTIE PAUSED / P TO RESUME");
            return;
        }
    }
    if (phase == USURPER_PHASE_RESULT) {
        ++result_ticks;
        if (launch_requested || result_action) {
            launch_requested = 0;
            unsigned action = result_action ? result_action - 1u : result_selection;
            result_action = 0;
            if (action == 0u) launch_from_research();
            else if (action == 1u) {
                if(result_victory&&campaign_stage_id+1u<exhibition_stage_count())++campaign_stage_id;
                reset_run();
                reset_tech_view();
                phase = USURPER_PHASE_TECH;
                message("RESEARCH / B TO BUY / ENTER TO DEPLOY");
            } else {
                enter_main_menu();
            }
        }
        return;
    }
    if (phase == USURPER_PHASE_ARRIVAL) {
        arrival_step();
        if (transition_ticks) {
            --transition_ticks;
            unsigned remaining = transition_ticks;
            uint64_t eased = (uint64_t)remaining * remaining * remaining * remaining;
            player_x = 400 * Q8 - (int32_t)(360u * Q8 * eased / (43u * 43u * 43u * 43u));
        }
        if (!transition_ticks) {
            player_x = FIELD_W / 2 * Q8;
            if (!tutorial_seen && !campaign_stage_id) begin_tutorial();
            else {
                phase = USURPER_PHASE_BATTLE;
                spawn_starting_unit();
                message("SECTOR 1-1 / SURVIVE UNTIL THE ARMOR BOSS");
            }
        }
        return;
    }
    if (phase == USURPER_PHASE_TUTORIAL) {
        arrival_step();
        launch_requested = 0;
        if (skip_requested) {
            skip_requested = 0;
            tutorial_done = tutorial_seen = 1u;
            clear_tutorial_targets();
            phase = USURPER_PHASE_BATTLE;
            level_tick = spawn_index = 0;
            message("SECTOR 1-1 / TUTORIAL SKIPPED");
            return;
        }
        move_player();
        capture_step();
        /* Released bodies and sweeps remain live during the tutorial, just
         * as their Godot nodes do while campaign progression is suspended. */
        skimmer_release_step();
        gunner_release_step();
        projectile_step();
        tutorial_step_update();
        if (aim_is_held) ++aim_charge_ticks;
        return;
    }
    if (phase != USURPER_PHASE_BATTLE) return;
    if (campaign_stage_id == 5u && boundary_boss.crush_holding) {
        us_boundary_cinematic_tick(&boundary_boss, 1.0 / 60);
        if (boundary_boss.crush_holding && !defeat_pending) {
            boundary_bind_units(); us_boundary_output_t output;
            us_boundary_step_delta(&boundary_boss, player_x * 256, player_y * 256, player_vx * 256, player_vy * 256, (1.0 / 60) * 0.001, &output);
            boundary_remove_grabbed_units(); sync_boundary_boss();
            launch_requested = skip_requested = 0; return;
        }
        us_boundary_flush_deferred(&boundary_boss);
    }
    if (defeat_pending) {
        if (advance_defeat_departure()) finish_run(0u);
        return;
    }
    launch_requested = 0;
    skip_requested = 0;
    ++level_tick;
#ifdef USURPER_DIAGNOSTIC_WARDEN
    unsigned diagnostic_cycle=(level_tick-1u)%600u;
    if(!diagnostic_cycle&&!unit_count){unit_count=1;unit_species[0]=SPECIES_WARDEN;unit_instance_id[0]=++next_unit_instance_id;unit_support_clock[0]=0;}
    if(diagnostic_cycle==180u&&unit_count){release_cooldown_ticks=0;release_selected();}
    if(!(diagnostic_cycle%15u)){
        unsigned angle=(diagnostic_cycle*7u)%360u;int dx,dy;direction_q8(angle,&dx,&dy);
        spawn_enemy_projectile(player_x+dx*60,player_y+dy*60,0,1,0);
    }
#ifdef USURPER_DIAGNOSTIC_WARDEN_HULL
    if(level_tick==1u)(void)spawn_enemy(SPECIES_WARDEN,560*Q8,240*Q8,1);
    if(enemies[0].active&&enemies[0].species==SPECIES_WARDEN){
        static const uint16_t progress[8]={0,1,8,64,128,192,255,256};enemies[0].capture_q8=progress[((level_tick-1u)/60u)%8u];
        if(diagnostic_cycle==60u)us_warden_status_hit(&warden_status[0]);
        us_warden_status_step(&warden_status[0],1.0/60);
    }
#endif
#endif
    battle_elapsed+=1.0/60;
    warden_shield_budget+=3.0/60;if(warden_shield_budget>3)warden_shield_budget=3;
    warden_guard_left=warden_guard_left>1.0/60?warden_guard_left-1.0/60:0;
    move_player();
    player_form_step();
    if (player_shield_delay) --player_shield_delay;
    if (!player_shield_delay && player_shield < player_shield_max) {
        player_shield_recharge_fraction += 1u + tech_effect("H4");
        if (player_shield_recharge_fraction >= 120u) {
            player_shield = min_u(player_shield_max, player_shield + player_shield_recharge_fraction / 120u);
            player_shield_recharge_fraction %= 120u;
        }
    }
    if (player_invincible_ticks) --player_invincible_ticks;
    if (contact_cooldown) --contact_cooldown;
    unsigned drain_rank = 0;
    int n1 = tech_index("N1");
    if (n1 >= 0) drain_rank = tech_effective_rank((unsigned)n1);
    if (recovery_buffer_cooldown) --recovery_buffer_cooldown;
    unsigned drain_step = (unsigned)((uint64_t)1600u * us_campaign_stage(campaign_stage_id)->upkeep_q16 /
                                   65536u) * (100u - min_u(6u * drain_rank, 96u)) / 100u;
    if (recovery_guard_ticks) { --recovery_guard_ticks; drain_step = drain_step * 3u / 4u; }
    player_energy_drain_fraction += drain_step;
    if (player_energy_drain_fraction >= 60000u) {
        player_energy_drain_fraction -= 60000u;
        if (player_hp) --player_hp;
    }
    if (current_player_health()<=0) { store_player_health(0);start_defeat(); return; }
    for (unsigned i = 0; i < 7u; ++i) if (armor_column_cooldown[i]) --armor_column_cooldown[i];
    if (!0 && level_tick >= BOSS_WARNING_TICK && !boss_spawned) {
        if(level_tick==BOSS_WARNING_TICK)usurper_audio_event(US_AUDIO_WARNING);
        boss_warning_ticks = BOSS_TIME_TICK - level_tick;
        if(campaign_stage_id==6u) {
            us_hangar_birth(player_x*256,player_y*256,camera_x*256,camera_y*256,&hangar_warning_x,&hangar_warning_y,hangar_warning_valid);
            hangar_warning_valid=1;
        }
        if(campaign_stage_id==7u){us_controller_birth(player_x*256,player_y*256,camera_x*256,camera_y*256,&controller_warning_x,&controller_warning_y,controller_warning_valid);controller_warning_valid=1;}
    }
    if (!0 && level_tick >= BOSS_TIME_TICK && !boss_spawned) boss_spawn();
    if (!0) spawn_campaign_event();
    arrival_step();
    queue_offspring_expiry();
    move_enemies();
    if (defeat_pending) return;
    capture_step();
    unit_passive_step();
    boss_step();
    skimmer_release_step();
    if (phase != USURPER_PHASE_BATTLE) return;
    gunner_release_step();
    projectile_step();
    warden_field_step();
    interceptor_step();
    if(campaign_stage_id==7u&&boss_spawned){
        for(unsigned i=0;i<6;++i){unsigned damage=us_heavy_bomb_step(&controller_bombs[i],player_x/256.0f,player_y/256.0f);if(damage)damage_player(damage);}
        flush_controller_events();
    }
    flush_hangar_fighters();
    flush_offspring_expiry();
    if (defeat_pending || phase != USURPER_PHASE_BATTLE) return;
    if (!0) supply_step();
    pickup_step();
    if (boss_active && campaign_stage_id == 4u && !boss_grace_ticks && !player_invincible_ticks && !rammer_contact_guard) {
        for (unsigned i = 0; i < 2u; ++i) {
            const us_twin_actor_t *actor = &twin_boss.actors[i];
            if (actor->active && !twin_contact_ticks[i] && distance_q8((int32_t)(actor->x * Q8), (int32_t)(actor->y * Q8), player_x, player_y) < 25u * Q8 + Q8 / 2u) {
                damage_player(i ? 10u : 13u); twin_contact_ticks[i] = 48u;
            }
        }
    } else if(boss_active&&campaign_stage_id==6u&&!boss_grace_ticks) {
        unsigned inside=distance_q8(boss_x,boss_y,player_x,player_y)<25u*Q8+Q8/2u;
        if(inside&&!hangar_contact_inside&&!hangar_contact_tick&&!player_invincible_ticks&&!rammer_contact_guard) {
            damage_player(18u);hangar_contact_tick=48u;
        }
        hangar_contact_inside=inside;
    } else if(campaign_stage_id==7u) {
        controller_contact_step();
    } else if (boss_active && campaign_stage_id != 4u && campaign_stage_id != 5u && campaign_stage_id!=6u && !boss_grace_ticks && distance_q8(boss_x, boss_y, player_x, player_y) < 28u * Q8 && !contact_cooldown && !player_invincible_ticks && !rammer_contact_guard) {
        damage_player(campaign_stage_id >= 2u ? 12u : campaign_stage_id == 1u ? 18u : 8u);
        contact_cooldown = 48u;
    }
    if (player_hp == 0u && !defeat_pending) start_defeat();
}

void usurper_scene_reset(void) {
    loadout_panel=loadout_selection=settings_shake_ticks=0;
#ifdef USURPER_PROFILE_ENABLED
    profile_visited_stage_mask=0;
#endif
    us_loadout_reset(&selected_loadout);
    campaign_stage_id = 0u;
    completed_stage_mask = 0u;
    menu_demo_run_index = menu_demo_shown_species_mask = 0u;
    memset(tech_owned, 0, sizeof(tech_owned));
    species_seen_mask = (uint16_t)(seen_bit(SPECIES_DART) | seen_bit(SPECIES_SKIMMER));
    alloy_wallet = alloy_remainder = last_run_alloy = cleared_sectors = tutorial_seen = 0;
    tutorial_done = 0;
    packet_size = packet_resync = 0;
    frame_tick = 0;
    reset_run();
    reset_tech_view();
    phase = USURPER_PHASE_MENU;
    menu_selection = menu_confirm = menu_confirm_selection = 0;
    menu_demo_reset();
    pointer_x = (int)USURPER_GPU_WIDTH / 2;
    pointer_y = (int)USURPER_GPU_HEIGHT / 2;
    pointer_visible = pointer_click_ticks = 0;
    ui_return_phase = USURPER_PHASE_MENU;
    message("USURPER MAIN MENU");
}

static int ui_inside(int x, int y, int left, int top, int width, int height) {
    return x >= left && y >= top && x < left + width && y < top + height;
}

static void ui_controls(unsigned held, unsigned previous, int mouse_x, int mouse_y, int wheel, unsigned moved) {
    int x = mouse_x * GAME_W / (int)USURPER_GPU_WIDTH;
    int y = mouse_y * GAME_H / (int)USURPER_GPU_HEIGHT;
    unsigned clicked = (held & 16u) && !(previous & 16u);
    if(clicked&&phase!=USURPER_PHASE_BATTLE&&phase!=USURPER_PHASE_TUTORIAL)usurper_audio_event(US_AUDIO_CLICK);
    if(loadout_panel){
        unsigned pressed=held&~previous;
        if(pressed&1u)loadout_selection=(loadout_selection+4u)%5u;
        if(pressed&2u)loadout_selection=(loadout_selection+1u)%5u;
        if(pressed&4u)loadout_cycle(-1);
        if(pressed&8u)loadout_cycle(1);
        for(unsigned i=0;(moved||clicked)&&i<5u;++i)
            if(ui_inside(mouse_x,mouse_y,scale_design_coord(204),scale_design_coord(150+(int)i*58),
                         (int)scale_design_extent(552),(int)scale_design_extent(46))){
                loadout_selection=i;if(clicked)loadout_cycle(mouse_x<scale_design_coord(480)?-1:1);break;
            }
        return;
    }
    if((phase==USURPER_PHASE_MENU||phase==USURPER_PHASE_TECH)&&clicked&&
       ui_inside(mouse_x,mouse_y,scale_design_coord(phase==USURPER_PHASE_TECH?390:774),scale_design_coord(phase==USURPER_PHASE_TECH?14:34),(int)scale_design_extent(165),(int)scale_design_extent(34))){loadout_panel=1;loadout_selection=0;return;}
    if (phase == USURPER_PHASE_MENU) {
        unsigned pressed = held & ~previous;
        if (pressed & 4u) {
            if (menu_confirm) menu_confirm_selection ^= 1u;
            else menu_selection = (menu_selection + MENU_ITEM_COUNT - 1u) % MENU_ITEM_COUNT;
        } else if (pressed & 8u) {
            if (menu_confirm) menu_confirm_selection ^= 1u;
            else menu_selection = (menu_selection + 1u) % MENU_ITEM_COUNT;
        }
        if (menu_confirm) {
            if (moved || clicked) {
                if (ui_inside(x, y, 214, 216, 96, 28)) menu_confirm_selection = 0;
                else if (ui_inside(x, y, 330, 216, 96, 28)) menu_confirm_selection = 1;
            }
            if (clicked && (ui_inside(x, y, 214, 216, 96, 28) || ui_inside(x, y, 330, 216, 96, 28)))
                launch_requested = 1u;
        } else if (moved || clicked) {
            for (unsigned i = 0; i < MENU_ITEM_COUNT; ++i) {
                if (ui_inside(mouse_x, mouse_y,
                              scale_design_coord(MENU_LEFT + (int)i * MENU_STEP),
                              scale_design_coord(MENU_TOP), 182, 42)) {
                    menu_selection = i;
                    if (clicked) launch_requested = 1u;
                }
            }
        }
        return;
    }
    if (phase == USURPER_PHASE_PAUSED) {
        if (clicked && ui_inside(x, y, 245, 155, 150, 27)) pause_requested = 1u;
        else if (clicked && ui_inside(x, y, 245, 188, 150, 27)) {
            ui_return_phase = USURPER_PHASE_PAUSED;
            settings_selection = 0u;
            phase = USURPER_PHASE_SETTINGS;
        }
        else if (clicked && ui_inside(x, y, 245, 221, 150, 28)) reset_requested = 1u;
        else if (clicked && ui_inside(x, y, 245, 254, 150, 28)) {
            settle_abandoned_run();
            enter_main_menu();
        }
        return;
    }
    if (phase == USURPER_PHASE_RESULT) {
        unsigned pressed = held & ~previous;
        if (pressed & 4u) result_selection = (result_selection + 2u) % 3u;
        else if (pressed & 8u) result_selection = (result_selection + 1u) % 3u;
        for (unsigned i = 0; (moved || clicked) && i < 3u; ++i) {
            /* Match the rendered footer and its underline in design pixels. */
            int left = RESULT_BUTTON_X + (int)i * RESULT_BUTTON_STEP;
            if (ui_inside(mouse_x, mouse_y, scale_design_coord(left), scale_design_coord(RESULT_BUTTON_Y),
                          (int)scale_design_extent(RESULT_BUTTON_WIDTH), (int)scale_design_extent(RESULT_BUTTON_HEIGHT))) {
                result_selection = i;
                if (clicked) result_action = i + 1u;
                break;
            }
        }
        return;
    }
    if (phase == USURPER_PHASE_SETTINGS) {
        unsigned pressed=held&~previous;
        if(pressed&1u)settings_selection=(settings_selection+6u)%7u;
        if(pressed&2u)settings_selection=(settings_selection+1u)%7u;
        if(pressed&4u)settings_adjust(-1);
        if(pressed&8u)settings_adjust(1);
        for(unsigned i=0;(moved||clicked)&&i<6u;++i){
            if(ui_inside(mouse_x,mouse_y,scale_design_coord(190),scale_design_coord(132+(int)i*50),
                         (int)scale_design_extent(576),(int)scale_design_extent(42))){
                settings_selection=i;if(clicked)settings_adjust(mouse_x<scale_design_coord(550)?-1:1);break;
            }
        }
        if (clicked && ui_inside(mouse_x, mouse_y, scale_design_coord(SETTINGS_BACK_X), scale_design_coord(SETTINGS_BACK_Y),
            (int)scale_design_extent(SETTINGS_BACK_W), (int)scale_design_extent(SETTINGS_BACK_H))) {settings_selection=6u;launch_requested = 1u;}
        return;
    }
    if (phase == USURPER_PHASE_CREDITS || phase == USURPER_PHASE_QUIT) {
        if (clicked || wheel) launch_requested = 1u;
        return;
    }
    if (phase != USURPER_PHASE_TECH) return;
    if (clicked && ui_inside(x, y, 12, 11, 70, 23)) { enter_main_menu(); return; }
    for(unsigned stage=0;stage<min_u(3u,exhibition_stage_count());++stage)
        if(clicked&&ui_inside(x,y,374+(int)stage*44,10,40,24)) {
            campaign_stage_id=stage;reset_run();message(us_campaign_stage(stage)->name);return;
        }
    if (clicked && ui_inside(x, y, 424, 316, 28, 32) && campaign_stage_id) {
        --campaign_stage_id; reset_run(); message(us_campaign_stage(campaign_stage_id)->name); return;
    }
    if (clicked && ui_inside(x, y, 598, 316, 28, 32) && campaign_stage_id + 1u < exhibition_stage_count()) {
        ++campaign_stage_id; reset_run(); message(us_campaign_stage(campaign_stage_id)->name); return;
    }
    if (clicked && ui_inside(x, y, 454, 316, 142, 32)) { launch_requested = 1u; return; }
    if (clicked && ui_inside(x, y, 394, 316, 28, 32)) { tech_help_visible ^= 1u; return; }
    if (!ui_inside(x, y, 146, 46, 484, 264)) { tech_tooltip_visible = 0; ui_dragging = 0; return; }
    if ((held & 32u) && !(previous & 32u)) {
        ui_dragging = 1u;
        ui_last_x = x;
        ui_last_y = y;
    }
    if (!(held & 32u)) ui_dragging = 0;
    if (ui_dragging) {
        tech_pan_x -= (x - ui_last_x) * 256 / (int)tech_zoom_q8;
        tech_pan_y -= (y - ui_last_y) * 256 / (int)tech_zoom_q8;
        ui_last_x = x;
        ui_last_y = y;
        tech_tooltip_visible = 0;
        return;
    }
    if (wheel) {
        unsigned old = tech_zoom_q8;
        tech_zoom_q8 = (unsigned)clamp_i((int)old - wheel * 64, 128, 384);
        tech_pan_x += (x - 388) * 256 / (int)old - (x - 388) * 256 / (int)tech_zoom_q8;
        tech_pan_y += (y - 178) * 256 / (int)old - (y - 178) * 256 / (int)tech_zoom_q8;
        tech_tooltip_visible = 0;
        return;
    }
    unsigned hovered = 0xffffffffu;
    for (unsigned i = 0; i < USURPER_TECH_NODE_COUNT; ++i) {
        if (!tech_revealed(i)) continue;
        int cx = 388 + (usurper_tech_nodes[i].x - tech_pan_x) * (int)tech_zoom_q8 / 256;
        int cy = 178 + (usurper_tech_nodes[i].y - tech_pan_y) * (int)tech_zoom_q8 / 256;
        int side = (usurper_tech_nodes[i].kind ? 36 : 28) * (int)tech_zoom_q8 / 256;
        if (ui_inside(x, y, cx - side / 2, cy - side / 2, side, side)) { hovered = i; break; }
    }
    if (hovered != tech_hovered && hovered != 0xffffffffu) {
        tech_feedback_ticks = 13u;
        tech_feedback_purchase = 0;
    }
    tech_hovered = hovered;
    tech_tooltip_visible = hovered != 0xffffffffu;
    if (tech_tooltip_visible) {
        selected_node = hovered;
        if (clicked) buy_requested = 1u;
    }
}

void usurper_scene_controls(unsigned held, unsigned aim_angle, int mouse_x, int mouse_y, int wheel) {
    unsigned previous = controls_held;
    unsigned moved = mouse_x != pointer_x || mouse_y != pointer_y;
    controls_held = held & 63u;
    controls_timeout = 30u;
    if(scene_settings.control_mode==2u)aim_target_angle=player_angle;
    else if(scene_settings.control_mode==1u&&(held&15u)){
        int dx=(int)((held>>3)&1u)-(int)((held>>2)&1u),dy=(int)((held>>1)&1u)-(int)(held&1u);
        if(dx||dy)aim_target_angle=angle_to(dx,dy);
    }else aim_target_angle = aim_angle % 360u;
    pointer_x = clamp_i(mouse_x, 0, (int)USURPER_GPU_WIDTH - 1);
    pointer_y = clamp_i(mouse_y, 0, (int)USURPER_GPU_HEIGHT - 1);
    pointer_visible = !(held & USURPER_POINTER_HIDDEN);
    if (held & USURPER_CONTROL_CANCEL) {
        controls_held = player_move_ticks = 0;
        move_x = move_y = 0;
        aim_is_held = ui_dragging = tech_tooltip_visible = 0;
        aim_is_form = 0;
        aim_slow_phase = aim_wall_ticks = 0;
        aim_capture_target_index = 0xffffffffu;
        return;
    }
    if ((held & 48u) & ~previous) pointer_click_ticks = 12u;
    if (angle_delta((int)player_angle, (int)aim_target_angle)) tutorial_rotated = 1u;
    ui_controls(held, previous, pointer_x, pointer_y, wheel, moved);
    if (defeat_pending || (phase != USURPER_PHASE_BATTLE && phase != USURPER_PHASE_TUTORIAL)) return;
    if (phase == USURPER_PHASE_TUTORIAL && (held & 16u) && !(previous & 16u) &&
        ui_inside(pointer_x * GAME_W / (int)USURPER_GPU_WIDTH,
                  pointer_y * GAME_H / (int)USURPER_GPU_HEIGHT, 430, 318, 194, 26)) {
        skip_requested = 1u;
        return;
    }
    if ((held & 16u) && !(previous & 16u)) begin_combat_aim();
    if (!(held & 16u) && (previous & 16u) && aim_is_held) {
        player_angle = aim_target_angle;
        commit_combat_aim();
        aim_capture_target_index = 0xffffffffu;
    }
    if ((held & 32u) && !(previous & 32u)) {
        int32_t wx = (0?pointer_x*25*Q8/32:(render_camera_x + (pointer_x * GAME_W / (int)USURPER_GPU_WIDTH - GAME_W / 2) * Q8));
        int32_t wy = (0?pointer_y*5*Q8/6:(render_camera_y + (pointer_y * GAME_H / (int)USURPER_GPU_HEIGHT - GAME_H / 2) * Q8));
        unsigned nearest = 0xffffffffu, score = 14u * Q8;
        for (unsigned i = 0; i < ENEMY_MAX; ++i) {
            if (!enemies[i].active) continue;
            unsigned distance = distance_q8(wx, wy, enemies[i].x, enemies[i].y);
            if (distance <= score) { score = distance; nearest = i; }
        }
        manual_target_index = nearest;
        if (phase == USURPER_PHASE_TUTORIAL && tutorial_step == 4u)
            tutorial_locked = nearest == practice_skimmer_index;
    }
    if (wheel && !aim_is_held) cycle_unit(wheel);
}

int usurper_scene_console_input_allowed(void) {
    return !packet_size && !packet_resync;
}

void usurper_scene_uart_byte(unsigned byte) {
    if (!packet_size) {
        if (byte == 0xfeu) packet_size = 1u;
        else if (!packet_resync) usurper_scene_key((int)byte);
        return;
    }
    input_packet[packet_size - 1u] = (uint8_t)byte;
    if (++packet_size <= sizeof(input_packet)) return;
    packet_size = 0;
    unsigned checksum = 0xa5u;
    for (unsigned i = 0; i < 9u; ++i) checksum ^= input_packet[i];
    if (checksum != input_packet[9]) {
        packet_resync = 1u;
        /* A missing byte can leave the next packet's header inside this
         * rejected frame. Preserve its suffix instead of dispatching it as
         * ASCII keys (including renderer mode digits in the main loop). */
        for (unsigned i = sizeof(input_packet); i > 0u; --i) {
            if (input_packet[i - 1u] != 0xfeu) continue;
            unsigned suffix = (unsigned)sizeof(input_packet) - i;
            for (unsigned j = 0; j < suffix; ++j) input_packet[j] = input_packet[i + j];
            packet_size = suffix + 1u;
            break;
        }
        return;
    }
    packet_resync = 0u;
    unsigned angle = input_packet[2] | (unsigned)input_packet[3] << 8;
    int x = input_packet[4] | (unsigned)input_packet[5] << 8;
    int y = input_packet[6] | (unsigned)input_packet[7] << 8;
    if (!(input_packet[1] & (USURPER_POINTER_HIDDEN | USURPER_CONTROL_CANCEL)) &&
        (phase == USURPER_PHASE_BATTLE || phase == USURPER_PHASE_TUTORIAL)) {
        /* Host packets encode an angle about the viewport center. With the
         * original camera dead zone, aim from the rendered controller instead. */
        int32_t wx = (0?x*25*Q8/32:(render_camera_x + (x * GAME_W / (int)USURPER_GPU_WIDTH - GAME_W / 2) * Q8));
        int32_t wy = (0?y*5*Q8/6:(render_camera_y + (y * GAME_H / (int)USURPER_GPU_HEIGHT - GAME_H / 2) * Q8));
        if (wx != render_player_x || wy != render_player_y)
            angle = angle_to(wx - render_player_x, wy - render_player_y);
        else angle = player_angle;
    }
    usurper_scene_controls(input_packet[1], angle, x, y, (int8_t)input_packet[8]);
}

void usurper_scene_input(unsigned input) {
    if (!gameplay_input_allowed()) input &= ~(USURPER_INPUT_UP | USURPER_INPUT_DOWN |
        USURPER_INPUT_LEFT | USURPER_INPUT_RIGHT | USURPER_INPUT_RELEASE |
        USURPER_INPUT_TARGET | USURPER_INPUT_WHEEL);
    if (phase != USURPER_PHASE_TECH) input &= ~(USURPER_INPUT_TECH_NEXT | USURPER_INPUT_TECH_BUY);
    if (input & USURPER_INPUT_UP) { move_y = -1; player_move_ticks = 18u; }
    if (input & USURPER_INPUT_DOWN) { move_y = 1; player_move_ticks = 18u; }
    if (input & USURPER_INPUT_LEFT) { move_x = -1; player_move_ticks = 18u; }
    if (input & USURPER_INPUT_RIGHT) { move_x = 1; player_move_ticks = 18u; }
    if (input & USURPER_INPUT_START) launch_requested = 1u;
    if (input & USURPER_INPUT_PAUSE) pause_requested = 1u;
    if (input & USURPER_INPUT_RESET) reset_requested = 1u;
    if (input & USURPER_INPUT_RELEASE) {
        if (aim_is_held) commit_combat_aim();
        else begin_combat_aim();
    }
    if (input & USURPER_INPUT_TARGET) target_requested = 1u;
    if (input & USURPER_INPUT_TECH_NEXT) next_tech_requested = 1u;
    if (input & USURPER_INPUT_TECH_BUY) buy_requested = 1u;
    if (input & USURPER_INPUT_WHEEL) wheel_requested = 1u;
}

void usurper_scene_key(int key) {
    if((phase==USURPER_PHASE_MENU||phase==USURPER_PHASE_TECH)&&(key=='l'||key=='L')){loadout_panel^=1u;loadout_selection=0;return;}
    if(loadout_panel){
        if(key==27||key=='p'||key=='P'||key==13||key==10){loadout_panel=0;return;}
        if(key=='w'||key=='W')loadout_selection=(loadout_selection+4u)%5u;
        if(key=='s'||key=='S')loadout_selection=(loadout_selection+1u)%5u;
        if(key=='a'||key=='A')loadout_cycle(-1);
        if(key=='d'||key=='D'||key==' ')loadout_cycle(1);
        return;
    }
    if(phase==USURPER_PHASE_TECH&&(key==','||key=='.')){
        unsigned limit=exhibition_stage_count();
        if(campaign_stage_id>=limit)campaign_stage_id=limit-1u;
        else if(key==','&&campaign_stage_id)--campaign_stage_id;
        else if(key=='.'&&campaign_stage_id+1u<limit)++campaign_stage_id;
        reset_run();message(us_campaign_stage(campaign_stage_id)->name);return;
    }
    if((key==13||key==' ')&&(phase==USURPER_PHASE_MENU||phase==USURPER_PHASE_TECH||phase==USURPER_PHASE_RESULT))usurper_audio_event(US_AUDIO_CLICK);
    if(phase==USURPER_PHASE_SETTINGS){
        if(key=='w'||key=='W'){settings_selection=(settings_selection+6u)%7u;return;}
        if(key=='s'||key=='S'){settings_selection=(settings_selection+1u)%7u;return;}
        if(key=='a'||key=='A'){settings_adjust(-1);return;}
        if(key=='d'||key=='D'){settings_adjust(1);return;}
    }
    if (phase == USURPER_PHASE_RESULT && (key == 'a' || key == 'A' || key == 'd' || key == 'D')) {
        result_selection = (result_selection + (key == 'a' || key == 'A' ? 2u : 1u)) % 3u;
        return;
    }
    if (phase == USURPER_PHASE_MENU && (key == 'a' || key == 'A' || key == 'd' || key == 'D')) {
        if (menu_confirm) menu_confirm_selection ^= 1u;
        else menu_selection = (menu_selection + (key == 'a' || key == 'A' ? MENU_ITEM_COUNT - 1u : 1u)) % MENU_ITEM_COUNT;
        return;
    }
    if (!gameplay_input_allowed() && (key == 'f' || key == 'F' || key == 't' || key == 'T' ||
        key == 'v' || key == 'V' || key == 'i' || key == 'I' || key == 'j' || key == 'J' ||
        key == 'k' || key == 'K' || key == 'l' || key == 'L')) return;
    if (phase != USURPER_PHASE_TECH && (key == 'b' || key == 'B' || key == 'n' || key == 'N')) return;
    if (phase != USURPER_PHASE_TUTORIAL && (key == 'x' || key == 'X')) return;
    if (!gameplay_input_allowed() && phase != USURPER_PHASE_TECH &&
        (key == 'w' || key == 'W' || key == 's' || key == 'S' ||
         key == 'a' || key == 'A' || key == 'd' || key == 'D')) return;
    switch (key) {
        case 'w': case 'W':
            if (phase == USURPER_PHASE_TECH) tech_pan_y -= 24;
            else { move_y = -1; player_move_ticks = 18u; }
            break;
        case 's': case 'S':
            if (phase == USURPER_PHASE_TECH) tech_pan_y += 24;
            else { move_y = 1; player_move_ticks = 18u; }
            break;
        case 'a': case 'A':
            if (phase == USURPER_PHASE_TECH) tech_pan_x -= 24;
            else { move_x = -1; player_move_ticks = 18u; }
            break;
        case 'd': case 'D':
            if (phase == USURPER_PHASE_TECH) tech_pan_x += 24;
            else { move_x = 1; player_move_ticks = 18u; }
            break;
        case 'i': case 'I': player_angle = (player_angle + 350u) % 360u; tutorial_rotated = 1u; break;
        case 'k': case 'K': player_angle = (player_angle + 10u) % 360u; tutorial_rotated = 1u; break;
        case 'j': case 'J': player_angle = (player_angle + 260u) % 360u; tutorial_rotated = 1u; break;
        case 'l': case 'L': player_angle = (player_angle + 100u) % 360u; tutorial_rotated = 1u; break;
        case 'f': case 'F':
            if (aim_is_held) commit_combat_aim();
            else begin_combat_aim();
            break;
        case 't': case 'T': target_requested = 1u; break;
        case 'g': case 'G': usurper_scene_form_aim(!aim_is_form); break;
        case 'v': case 'V': wheel_requested = 1u; break;
        case 'n': case 'N': next_tech_requested = 1u; break;
        case 'b': case 'B': buy_requested = 1u; break;
        case 'x': case 'X': skip_requested = 1u; break;
        case 'p': case 'P': case 27: pause_requested = 1u; break;
        case 'r': case 'R': reset_requested = 1u; break;
        case ' ': case '\r': case '\n': case 'e': case 'E': launch_requested = 1u; break;
        default: break;
    }
    if (wheel_requested) {
        wheel_requested = 0;
        cycle_unit(1);
    }
    if (target_requested) {
        target_requested = 0;
        if (phase == USURPER_PHASE_TUTORIAL && tutorial_step == 4u && practice_skimmer_index < ENEMY_MAX) {
            manual_target_index = practice_skimmer_index;
            tutorial_locked = 1u;
        } else {
            unsigned nearest = 0xffffffffu, score = 0xffffffffu;
            int dx, dy;
            direction_q8(player_angle, &dx, &dy);
            for (unsigned i = 0; i < ENEMY_MAX; ++i) {
                if (!enemies[i].active) continue;
                unsigned distance = distance_q8(player_x, player_y, enemies[i].x, enemies[i].y);
                unsigned facing = angle_to(enemies[i].x - player_x, enemies[i].y - player_y);
                if ((unsigned)abs_i(angle_delta((int)player_angle, (int)facing)) > 30u) continue;
                if (distance < score) { score = distance; nearest = i; }
            }
            manual_target_index = nearest;
            (void)dx;
            (void)dy;
        }
    }
    usurper_scene_input(0u);
}

static uint16_t rgb565(unsigned r, unsigned g, unsigned b) {
    return (uint16_t)(((r & 0xf8u) << 8) | ((g & 0xfcu) << 3) | (b >> 3));
}

static int emit_raw(const ts2d_command_t *command) {
    if (render_failed) return -1;
    if (current_emit(current_emit_context, command)) { render_failed = 1u; return -1; }
    return 0;
}

static void fill_rect(int x, int y, unsigned width, unsigned height, uint16_t color) {
    if (!width || !height) return;
    if (graph_clip) {
        /* Research nodes routinely sit above/left of the scroll viewport.
         * Unsigned max turned negative coordinates into full-height strips. */
        int left = clamp_i(x, 219, 945);
        int top = clamp_i(y, 69, 465);
        int right = clamp_i(x + (int)width, 219, 945);
        int bottom = clamp_i(y + (int)height, 69, 465);
        if (right <= left || bottom <= top) return;
        x = left; y = top; width = (unsigned)(right - left); height = (unsigned)(bottom - top);
    }
    us_command_words_t packet=us_command_pack(TS2D_OP_FILL_RECT,TS2D_FLAG_CLIP_TO_VIEWPORT,0,0,
        0,current_framebuffer,0,0,(int16_t)scale_design_coord(x),(int16_t)scale_design_coord(y),
        (uint16_t)scale_design_extent(width),(uint16_t)scale_design_extent(height),0,USURPER_GPU_STRIDE,color);
    (void)emit_raw(&packet.command);
}

static void ui_blit_alpha(const usurper_ui_region_t *region, int x, int y,
                          unsigned alpha, unsigned extra_flags) {
    if (!region->w || !region->h) return;
    us_command_words_t packet=us_command_pack(TS2D_OP_BLIT_ALPHA_PIXEL,
        (uint8_t)(TS2D_FLAG_CLIP_TO_VIEWPORT|TS2D_FLAG_SRC_ARGB4444|extra_flags),TS2D_FMT_ARGB4444,(uint8_t)min_u(alpha,255u),
        USURPER_ATLAS_BASE+(region->y+region->h>32767u?region->y*USURPER_ATLAS_STRIDE:0u),current_framebuffer,
        (int16_t)region->x,region->y+region->h>32767u?0:(int16_t)region->y,(int16_t)x,(int16_t)y,
        region->w,region->h,USURPER_ATLAS_STRIDE,USURPER_GPU_STRIDE,0);
    (void)emit_raw(&packet.command);
}

static void ui_blit(const usurper_ui_region_t *region, int x, int y) {
    ui_blit_alpha(region, x, y, 255u, 0u);
}

static void background_blit(int src_x, int src_y, unsigned width, unsigned height,
                            int dst_x, int dst_y) {
    if (!width || !height) return;
    ts2d_command_t command = {.opcode = TS2D_OP_BLIT_OPAQUE,
        .flags = TS2D_FLAG_CLIP_TO_VIEWPORT, .src_format = TS2D_FMT_RGB565,
        .src_base_addr = USURPER_BG_BASE, .dst_base_addr = current_framebuffer,
        .src_x = (int16_t)src_x, .src_y = (int16_t)src_y,
        .dst_x = (int16_t)dst_x, .dst_y = (int16_t)dst_y,
        .width = (uint16_t)width, .height = (uint16_t)height,
        .src_stride_bytes = USURPER_BG_STRIDE, .dst_stride_bytes = USURPER_GPU_STRIDE};
    (void)emit_raw(&command);
}

static void ui_layer(const usurper_ui_region_t *region) {
    ui_blit(region, region->ox, region->oy);
}

static void ui_text_alpha(int x, int y, const char *value, unsigned alpha) {
    const unsigned char *cursor = (const unsigned char *)value;
    while (*cursor) {
        unsigned codepoint = *cursor++;
        if ((codepoint & 0xe0u) == 0xc0u && *cursor) {
            codepoint = (codepoint & 31u) << 6;
            codepoint |= *cursor++ & 63u;
        } else if ((codepoint & 0xf0u) == 0xe0u && cursor[0] && cursor[1]) {
            codepoint = (codepoint & 15u) << 12;
            codepoint |= (cursor[0] & 63u) << 6 | (cursor[1] & 63u);
            cursor += 2;
        }
        unsigned found=0;
        for (unsigned i = 0; i < USURPER_GLYPH_COUNT; ++i) {
            if (us_ui_glyphs[i].codepoint == codepoint) {
                ui_blit_alpha(&us_ui_glyphs[i].region, x, y, alpha, 0u);
                x += (int)us_ui_glyphs[i].advance;
                found=1;
                break;
            }
        }
        if(!found)for(unsigned i=0;i<US_SUPPLY_GLYPH_COUNT;++i)if(us_supply_glyphs[i].codepoint==codepoint){
            const us_supply_glyph_t *glyph=&us_supply_glyphs[i];
            ts2d_command_t command={.opcode=TS2D_OP_BLIT_ALPHA_PIXEL,.flags=TS2D_FLAG_CLIP_TO_VIEWPORT|TS2D_FLAG_SRC_ARGB4444,
                .src_format=TS2D_FMT_ARGB4444,.global_alpha=(uint8_t)alpha,.src_base_addr=US_SUPPLY_GLYPH_BASE,
                .dst_base_addr=current_framebuffer,.src_x=0,.src_y=(int16_t)glyph->y,.dst_x=(int16_t)x,.dst_y=(int16_t)y,
                .width=32,.height=(uint16_t)glyph->h,.src_stride_bytes=64,.dst_stride_bytes=USURPER_GPU_STRIDE};
            (void)emit_raw(&command);x+=(int)glyph->advance;break;
        }
    }
}

static void ui_text(int x, int y, const char *value) {
    ui_text_alpha(x, y, value, 255u);
}
static unsigned supply_text_width(const char *value){
    const unsigned char *cursor=(const unsigned char *)value;unsigned width=0;
    while(*cursor){unsigned codepoint=*cursor++;
        if((codepoint&0xe0u)==0xc0u&&*cursor){codepoint=(codepoint&31u)<<6;codepoint|=*cursor++&63u;}
        else if((codepoint&0xf0u)==0xe0u&&cursor[0]&&cursor[1]){codepoint=(codepoint&15u)<<12;codepoint|=(cursor[0]&63u)<<6|(cursor[1]&63u);cursor+=2;}
        for(unsigned i=0;i<US_SUPPLY_GLYPH_COUNT;++i)if(us_supply_glyphs[i].codepoint==codepoint){width+=us_supply_glyphs[i].advance;break;}
    }return width;
}
static void supply_text(int x,int y,const char *value){
    const unsigned char *cursor=(const unsigned char *)value;
    while(*cursor){unsigned codepoint=*cursor++;
        if((codepoint&0xe0u)==0xc0u&&*cursor){codepoint=(codepoint&31u)<<6;codepoint|=*cursor++&63u;}
        else if((codepoint&0xf0u)==0xe0u&&cursor[0]&&cursor[1]){codepoint=(codepoint&15u)<<12;codepoint|=(cursor[0]&63u)<<6|(cursor[1]&63u);cursor+=2;}
        for(unsigned i=0;i<US_SUPPLY_GLYPH_COUNT;++i)if(us_supply_glyphs[i].codepoint==codepoint){
            const us_supply_glyph_t *glyph=&us_supply_glyphs[i];
            ts2d_command_t command={.opcode=TS2D_OP_BLIT_ALPHA_PIXEL,.flags=TS2D_FLAG_CLIP_TO_VIEWPORT|TS2D_FLAG_SRC_ARGB4444,.src_format=TS2D_FMT_ARGB4444,.global_alpha=255,
                .src_base_addr=US_SUPPLY_GLYPH_BASE,.dst_base_addr=current_framebuffer,.src_x=0,.src_y=(int16_t)glyph->y,.dst_x=(int16_t)x,.dst_y=(int16_t)y,
                .width=32,.height=(uint16_t)glyph->h,.src_stride_bytes=64,.dst_stride_bytes=USURPER_GPU_STRIDE};
            (void)emit_raw(&command);x+=(int)glyph->advance;break;
        }
    }
}

static void ui_number(int x, int y, unsigned value) {
    char buffer[12];
    unsigned offset = 11;
    buffer[offset] = 0;
    do { buffer[--offset] = (char)('0' + value % 10u); value /= 10u; } while (value);
    ui_text(x, y, buffer + offset);
}

static void ui_ratio(int x, int y, unsigned value, unsigned maximum) {
    ui_number(x, y, value);
    unsigned digits = 1, remaining = value;
    while (remaining >= 10u) { ++digits; remaining /= 10u; }
    ui_text(x + (int)digits * 10, y, "/");
    ui_number(x + (int)(digits + 1u) * 10, y, maximum);
}

static void blit(int src_x, int src_y, unsigned src_width, unsigned src_height,
                 int dst_x, int dst_y, unsigned dst_width, unsigned dst_height) {
    if (!src_width || !src_height || !dst_width || !dst_height) return;
    (void)dst_width;
    (void)dst_height;
    ts2d_command_t command = {.opcode = TS2D_OP_BLIT_ALPHA_PIXEL,
        .flags = TS2D_FLAG_CLIP_TO_VIEWPORT | TS2D_FLAG_SRC_ARGB4444,
        .src_format = TS2D_FMT_ARGB4444, .global_alpha = 255u,
        .src_base_addr = USURPER_ATLAS_BASE + ((unsigned)src_y + src_height > 32767u ? (unsigned)src_y * USURPER_ATLAS_STRIDE : 0u),
        .dst_base_addr = current_framebuffer,
        .src_x = (int16_t)src_x, .src_y = (unsigned)src_y + src_height > 32767u ? 0 : (int16_t)src_y,
        .dst_x = (int16_t)scale_design_coord(dst_x),
        .dst_y = (int16_t)scale_design_coord(dst_y),
        .width = (uint16_t)src_width, .height = (uint16_t)src_height,
        .src_stride_bytes = USURPER_ATLAS_STRIDE, .dst_stride_bytes = USURPER_GPU_STRIDE};
    (void)emit_raw(&command);
}

#include "usurper_line_clip.h"
static void line_rect(int x0, int y0, int x1, int y1, uint16_t color, unsigned width) {
    int dx = abs_i(x1 - x0), dy = abs_i(y1 - y0);
    if (!dx) {
        fill_rect(x0 - (int)width / 2, y0 < y1 ? y0 : y1, width, (unsigned)(dy + 1), color);
        return;
    }
    if (!dy) {
        fill_rect(x0 < x1 ? x0 : x1, y0 - (int)width / 2, (unsigned)(dx + 1), width, color);
        return;
    }
    int steps = (int)max_u((unsigned)dx, (unsigned)dy) / (int)max_u(width, 1u);
    if (steps < 1) steps = 1;
    int first=0,last=steps;
    if(graph_clip) {
        if(!width)return;
        int half=(int)width/2;
        us_line_clip_axis(x0,x1-x0,steps,219+half-(int)width+1,945+half-1,&first,&last);
        us_line_clip_axis(y0,y1-y0,steps,69+half-(int)width+1,465+half-1,&first,&last);
    }
    for (int i = first; i <= last; ++i) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        fill_rect(x - (int)width / 2, y - (int)width / 2, width, width, color);
    }
}

static void line_rect_fast(int x0, int y0, int x1, int y1, uint16_t color, unsigned width) {
    int dx = abs_i(x1 - x0), dy = abs_i(y1 - y0);
    if (!dx || !dy) {
        line_rect(x0, y0, x1, y1, color, width);
        return;
    }
    int steps = (int)max_u((unsigned)dx, (unsigned)dy) / 3;
    if (steps < 1) steps = 1;
    unsigned stamp = max_u(width, 2u);
    for (int i = 0; i <= steps; ++i) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        fill_rect(x - (int)stamp / 2, y - (int)stamp / 2, stamp, stamp, color);
    }
}

static void line_rect_trail(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs_i(x1 - x0), dy = abs_i(y1 - y0);
    int steps = (int)max_u((unsigned)dx, (unsigned)dy) / 12;
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; ++i) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        fill_rect(x - 2, y - 2, 4u, 4u, color);
    }
}

static void outline_rect(int x, int y, unsigned width, unsigned height, uint16_t color, unsigned weight) {
    fill_rect(x, y, width, weight, color);
    fill_rect(x, y + (int)height - (int)weight, width, weight, color);
    fill_rect(x, y, weight, height, color);
    fill_rect(x + (int)width - (int)weight, y, weight, height, color);
}

static void small_ring(int x, int y, unsigned radius, uint16_t color) {
    for (unsigned angle = 0; angle < 360u; angle += 12u) {
        int dx, dy;
        direction_q8(angle, &dx, &dy);
        fill_rect(x + dx * (int)radius / 256 - 1, y + dy * (int)radius / 256 - 1, 3u, 3u, color);
    }
}

static unsigned ship_kind(unsigned species, unsigned friendly) {
    if (!species) return 0u;
    if (friendly) return species == SPECIES_SKIMMER ? 5u : species == SPECIES_GUNNER ? 6u : 4u;
    return species == SPECIES_DART ? 1u : species == SPECIES_SKIMMER ? 2u : species == SPECIES_GUNNER ? 3u : 1u;
}

static void draw_ship(unsigned species, unsigned friendly, unsigned angle, int x, int y) {
#ifdef USURPER_RAMMER_REFERENCE_SPRITES
    if (species == SPECIES_RAMMER) {
        const usurper_ui_region_t *region = &us_ui_rammer_hulls_frames[friendly ? 9u : 0u][(angle % 360u) / 4u];
        ui_blit(region, scale_design_coord(x) + region->ox, scale_design_coord(y) + region->oy);
        return;
    }
#endif
    unsigned type = ship_kind(species, friendly);
    unsigned tile = type * USURPER_ASSET_SHIP_VARIANTS +
        (angle % 360u) * USURPER_ASSET_SHIP_VARIANTS / 360u;
    usurper_ui_region_t region = {(unsigned short)((tile % 16u) * 64u),
        (unsigned short)(us_ui_source_ships.y + tile / 16u * 32u), 64u, 32u, 0, 0};
    ui_blit(&region, scale_design_coord(x) - 32, scale_design_coord(y) - 16);
}

static void draw_gunner_capture(unsigned progress, unsigned angle, int x, int y) {
#ifdef USURPER_GUNNER_REFERENCE_SPRITES
    unsigned level = min_u(8u, (progress + 16u) / 32u);
    if (level) {
        const usurper_ui_region_t *region = &us_ui_gunner_capture_frames[level - 1u][angle % 360u / 4u];
        ui_blit(region, scale_design_coord(x) + region->ox, scale_design_coord(y) + region->oy);
        return;
    }
#else
    (void)progress;
#endif
    draw_ship(SPECIES_GUNNER, 0u, angle, x, y);
}

static void draw_gunner_projectile(unsigned active, unsigned angle, int x, int y) {
#ifdef USURPER_GUNNER_REFERENCE_SPRITES
    const usurper_ui_region_t *region = &us_ui_gunner_projectiles_frames[active ? 1u : 0u][angle % 360u / 4u];
    ui_blit(region, scale_design_coord(x) + region->ox, scale_design_coord(y) + region->oy);
#else
    (void)active; (void)angle;
    fill_rect(x - 2, y - 2, 4u, 4u, rgb565(59, 185, 214));
#endif
}

static void draw_gunner_reticles(void) {
    for (unsigned i = 0; i < GUNNER_VOLLEY_MAX; ++i) {
        GunnerVolley *volley = &gunner_volleys[i];
        if (!volley->reticle_q8) continue;
        int x, y;
        world_to_screen(volley->target_x, volley->target_y, &x, &y);
        small_ring(x, y, 14u, rgb565(255, 224, 120));
        for (unsigned axis = 0; axis < 360u; axis += 90u) {
            int dx, dy;
            direction_q8(axis, &dx, &dy);
            line_rect(x + dx * 8 / 256, y + dy * 8 / 256,
                      x + dx * 24 / 256, y + dy * 24 / 256, rgb565(16, 26, 36), 4u);
            line_rect(x + dx * 8 / 256, y + dy * 8 / 256,
                      x + dx * 24 / 256, y + dy * 24 / 256, rgb565(255, 224, 120), 2u);
        }
        fill_rect(x - 2, y - 2, 4u, 4u, rgb565(255, 255, 255));
    }
}

static void draw_ship_glow(int x, int y, unsigned friendly, unsigned angle) {
    const usurper_ui_region_t *glow = friendly ? &us_ui_glow_friendly : &us_ui_glow_enemy;
    /* A small sinusoidal pulse matches the source glow better than a six-step
     * sawtooth and avoids visible brightness popping at 60 Hz. */
    int wave = (int)usurper_sin_q8[(frame_tick * 3u + angle) % 360u];
    int pulse = 3 + wave * 3 / 256;
    int size = 46 + pulse;
    unsigned glow_alpha = (unsigned)(218 + wave * 24 / 256);
    ui_blit_alpha(glow, scale_design_coord(x) - size / 2,
                  scale_design_coord(y) - size / 2, glow_alpha, 0u);
    int bx, by;
    direction_q8((angle + 180u) % 360u, &bx, &by);
    uint16_t hot = friendly ? rgb565(184, 239, 255) : rgb565(255, 145, 77);
    int tail_x = x + bx * (6 + pulse) / 256;
    int tail_y = y + by * (6 + pulse) / 256;
    unsigned tail_size = 2u + (unsigned)(pulse > 3 ? 1 : 0);
    fill_rect(tail_x - (int)tail_size / 2, tail_y - (int)tail_size / 2,
              tail_size, tail_size, hot);
}

static void draw_ship_reflection(unsigned species, unsigned friendly, unsigned angle, int x, int y) {
#ifdef USURPER_RAMMER_REFERENCE_SPRITES
    if (species == SPECIES_RAMMER) {
        unsigned frame = (90u - (angle % 360u) / 4u) % 90u;
        const usurper_ui_region_t *region = &us_ui_rammer_hulls_frames[friendly ? 9u : 0u][frame];
        ui_blit_alpha(region, scale_design_coord(x) + region->ox,
            scale_design_coord(y) - region->oy - region->h + 8, 22u, TS2D_FLAG_FLIP_Y);
        return;
    }
#endif
    unsigned type = ship_kind(species, friendly);
    /* FLIP_Y mirrors the sprite's heading around the horizontal plane. Pick
     * the complementary source frame so the reflection keeps the same world
     * heading as the hull instead of appearing to face backwards. */
    unsigned frame = (angle % 360u) * USURPER_ASSET_SHIP_VARIANTS / 360u;
    unsigned reflected_frame = (USURPER_ASSET_SHIP_VARIANTS - frame) % USURPER_ASSET_SHIP_VARIANTS;
    unsigned tile = type * USURPER_ASSET_SHIP_VARIANTS +
        reflected_frame;
    usurper_ui_region_t region = {(unsigned short)((tile % 16u) * 64u),
        (unsigned short)(us_ui_source_ships.y + tile / 16u * 32u), 64u, 32u, 0, 0};
    /* Sprite sizes are already in framebuffer pixels; share the hull's
     * center and offset only the reflection, without scaling its half-size. */
    ui_blit_alpha(&region, scale_design_coord(x) - 32,
                  scale_design_coord(y) - 16 + 8, 22u, TS2D_FLAG_FLIP_Y);
}

static unsigned tech_icon_x(unsigned index) {
    return (index % USURPER_TECH_ICON_COLUMNS) * TECH_ICON_SIZE;
}

static unsigned tech_icon_y(unsigned index) {
    return USURPER_TECH_ICON_Y + (index / USURPER_TECH_ICON_COLUMNS) * TECH_ICON_SIZE;
}

#define US_RESEARCH_ICON_IMPLEMENTATION
#include "usurper_research_icons.h"

static void tech_screen_position(unsigned index, int *x, int *y) {
    const usurper_tech_node_t *node = &usurper_tech_nodes[index];
    *x = 388 + ((int)node->x - (int)tech_pan_x) * (int)tech_zoom_q8 / 256;
    *y = 178 + ((int)node->y - (int)tech_pan_y) * (int)tech_zoom_q8 / 256;
    *x = design_x(*x);
    *y = design_y(*y);
}

static unsigned node_color(unsigned index) {
    if (!tech_unlocked(index)) return rgb565(115, 122, 128);
    if (tech_cost(index) == 0xffffffffu) return rgb565(226, 179, 72);
    return tech_can_buy(index) ? rgb565(104, 223, 136) : rgb565(237, 89, 103);
}

static void draw_tech_screen(void) {
    fill_rect(0, 0, USURPER_DESIGN_WIDTH, USURPER_DESIGN_HEIGHT, rgb565(8, 15, 21));
    ui_layer(&us_ui_research_chrome);
    /* The chrome atlas contains a five-zero placeholder. Replace its entire
     * numeric area, keeping the currency icon and border. Glyphs use physical
     * pixels, so even UINT32_MAX fits in the right-aligned100-pixel field. */
    fill_rect(804,21,130,29,rgb565(0,0,0));
    unsigned wallet_digits=1u,wallet_value=alloy_wallet;
    while(wallet_value>=10u){wallet_value/=10u;++wallet_digits;}
    ui_number(990-(int)wallet_digits*10,25,alloy_wallet);
    if (campaign_stage_id) {
        fill_rect(design_x(454), design_y(317), design_x(142), design_y(30), rgb565(17, 29, 36));
        ui_text(scale_design_coord(design_x(476)), scale_design_coord(design_y(325)), "进入区域");
        char number[3] = {'0', (char)('1' + campaign_stage_id), 0};
        ui_text(scale_design_coord(design_x(528)), scale_design_coord(design_y(325)), number);
    }
    ui_text(scale_design_coord(design_x(435)), scale_design_coord(design_y(325)), campaign_stage_id ? "<" : " ");
    ui_text(scale_design_coord(design_x(608)), scale_design_coord(design_y(325)), campaign_stage_id+1u < exhibition_stage_count() ? ">" : " ");
    graph_clip = 1u;
    for (unsigned i = 0; i < USURPER_TECH_NODE_COUNT; ++i) {
        if (!tech_revealed(i) || !usurper_tech_nodes[i].parent[0]) continue;
        int parent = tech_index(usurper_tech_nodes[i].parent);
        if (parent < 0) continue;
        int x0, y0, x1, y1;
        tech_screen_position((unsigned)parent, &x0, &y0);
        tech_screen_position(i, &x1, &y1);
        uint16_t color = tech_rank((unsigned)parent) ? rgb565(214, 227, 230) : rgb565(86, 97, 104);
        unsigned cursor = selected_node, on_path = 0;
        for (unsigned hop = 0; hop < USURPER_TECH_NODE_COUNT && cursor < USURPER_TECH_NODE_COUNT; ++hop) {
            if (cursor == i) { on_path = tech_tooltip_visible; break; }
            int ancestor = tech_index(usurper_tech_nodes[cursor].parent);
            if (ancestor < 0) break;
            cursor = (unsigned)ancestor;
        }
        if (on_path) color = rgb565(255, 224, 120);
        line_rect(x0, y0, x1, y1, color, on_path ? 3u : 1u);
    }
    for (unsigned i = 0; i < USURPER_TECH_NODE_COUNT; ++i) {
        if (!tech_revealed(i)) continue;
        int x, y;
        tech_screen_position(i, &x, &y);
        unsigned side = (usurper_tech_nodes[i].kind ? 54u : 42u) * tech_zoom_q8 / 256u;
        if (i == selected_node && tech_feedback_ticks && tech_feedback_purchase) {
            unsigned scale = tech_feedback_ticks > 13u ? 256u + (19u - tech_feedback_ticks) * 14u :
                tech_feedback_ticks > 7u ? 338u - (13u - tech_feedback_ticks) * 19u :
                225u + (7u - tech_feedback_ticks) * 31u / 7u;
            side = side * scale / 256u;
        }
        uint16_t color = node_color(i);
        fill_rect(x - (int)side / 2, y - (int)side / 2, side, side, rgb565(10, 17, 24));
        outline_rect(x - (int)side / 2, y - (int)side / 2, side, side, color, 2u);
        if (tech_unlocked(i)) {
            unsigned icon_size = side - 6u;
            research_icon(i,x-(int)icon_size/2,y-(int)icon_size/2,icon_size);
        } else if (!tech_unlocked(i) && x > 237 && x < 918 && y > 87 && y < 438) {
            ui_text(scale_design_coord(x) - 8, scale_design_coord(y) - 13, "?");
        }
        if (tech_unlocked(i) && !tech_rank(i) && x > 237 && x < 918 && y > 87 && y < 438)
            ui_text(scale_design_coord(x + (int)side / 2 - 8), scale_design_coord(y - (int)side / 2 - 14), "新");
    }
    graph_clip = 0;
    if (tech_tooltip_visible && selected_node < USURPER_TECH_NODE_COUNT) {
        const usurper_ui_region_t *region = &us_ui_tooltips[selected_node][min_u(tech_rank(selected_node), 8u)];
        int x, y;
        tech_screen_position(selected_node, &x, &y);
        x = scale_design_coord(x) + 40;
        y = scale_design_coord(y) - 24;
        if (x + region->w > 1011) x -= region->w + 80;
        x = clamp_i(x, 13, 1011 - region->w);
        y = clamp_i(y, 70, 506 - region->h);
        ui_blit(region, x, y);
    }
    if (tech_help_visible) ui_layer(&us_ui_research_help);
}

static void draw_background(void) {
    int32_t view_x = phase == USURPER_PHASE_MENU ? menu_demo_render_camera_x : render_camera_x;
    int32_t view_y = phase == USURPER_PHASE_MENU ? menu_demo_render_camera_y : render_camera_y;
    int sx = (int)(((int64_t)view_x - GAME_W / 2 * Q8) * 8 / (5 * Q8));
    int sy = (int)(((int64_t)view_y - GAME_H / 2 * Q8) * 8 / (5 * Q8));
    int dx = sx < 0 ? -sx : 0, dy = sy < 0 ? -sy : 0;
    unsigned source_x = (unsigned)(sx < 0 ? 0 : sx);
    unsigned source_y = (unsigned)(sy < 0 ? 0 : sy);
    unsigned width = (unsigned)dx >= USURPER_GPU_WIDTH || source_x >= USURPER_BG_WIDTH ? 0u :
        min_u(USURPER_GPU_WIDTH - (unsigned)dx, USURPER_BG_WIDTH - source_x);
    unsigned height = (unsigned)dy >= USURPER_GPU_HEIGHT || source_y >= USURPER_BG_HEIGHT ? 0u :
        min_u(USURPER_GPU_HEIGHT - (unsigned)dy, USURPER_BG_HEIGHT - source_y);
    /* The following opaque copy replaces every pixel in its rectangle. Clear
     * only the uncovered strips, in exact framebuffer pixel coordinates;
     * design-coordinate scaling here would create one-pixel seams. */
    us_background_rect_t uncovered[4];
    unsigned clear_count = us_background_uncovered(USURPER_GPU_WIDTH,
        USURPER_GPU_HEIGHT, (unsigned)dx, (unsigned)dy, width, height, uncovered);
    for (unsigned i = 0; i < clear_count; ++i) {
        const us_background_rect_t *r = &uncovered[i];
        ts2d_command_t command = {.opcode = TS2D_OP_FILL_RECT,
            .flags = TS2D_FLAG_CLIP_TO_VIEWPORT, .dst_base_addr = current_framebuffer,
            .dst_x = (int16_t)r->x, .dst_y = (int16_t)r->y,
            .width = (uint16_t)r->width, .height = (uint16_t)r->height,
            .dst_stride_bytes = USURPER_GPU_STRIDE, .parameter = rgb565(16, 26, 36)};
        (void)emit_raw(&command);
    }
    background_blit(sx < 0 ? 0 : sx, sy < 0 ? 0 : sy, width, height, dx, dy);
}

#if 0
static void draw_legacy_background(void) {
    fill_rect(0, 0, USURPER_DESIGN_WIDTH, USURPER_DESIGN_HEIGHT, rgb565(16, 26, 36));
    /* Broad blue bands reproduce the faint reflected light from the arena
     * surface without introducing a UI bar at the top of the frame. */
    fill_rect(0, 72, USURPER_DESIGN_WIDTH, 2u, rgb565(19, 39, 55));
    fill_rect(0, 418, USURPER_DESIGN_WIDTH, 2u, rgb565(12, 29, 43));
    /* The reference arena has a sparse parallax star field behind the grid.
     * Keep the pattern deterministic so captures and replay remain stable. */
    for (unsigned star = 0; star < 56u; ++star) {
        unsigned sx = (star * 97u + 31u) % USURPER_DESIGN_WIDTH;
        unsigned sy = (star * 53u + 17u) % USURPER_DESIGN_HEIGHT;
        unsigned drift = (frame_tick / 8u + star * 3u) % 9u;
        sx = (sx + drift) % USURPER_DESIGN_WIDTH;
        uint16_t tint = star % 7u == 0u ? rgb565(121, 190, 226) :
            (star % 3u == 0u ? rgb565(74, 117, 145) : rgb565(40, 73, 94));
        fill_rect((int)sx, (int)sy, star % 11u == 0u ? 2u : 1u,
                  star % 11u == 0u ? 2u : 1u, tint);
    }
    for (unsigned sheen = 0; sheen < 10u; ++sheen) {
        unsigned sx = (sheen * 101u + frame_tick / 3u) % USURPER_DESIGN_WIDTH;
        unsigned sy = 92u + (sheen * 37u) % 340u;
        fill_rect((int)sx, (int)sy, 12u + (sheen & 3u) * 4u, 1u,
                  sheen & 1u ? rgb565(22, 52, 70) : rgb565(27, 63, 82));
    }
    int left = max_u(0u, (unsigned)max_u(0, (int)(player_x / Q8) - GAME_W / 2));
    int top = max_u(0u, (unsigned)max_u(0, (int)(player_y / Q8) - GAME_H / 2));
    int right = (int)min_u(FIELD_W, (unsigned)max_u(0, (int)(player_x / Q8) + GAME_W / 2));
    int bottom = (int)min_u(FIELD_H, (unsigned)max_u(0, (int)(player_y / Q8) + GAME_H / 2));
    /* Anchor the grid to the camera viewport rather than the moving world
     * bounds. This keeps every vertical and horizontal cell evenly spaced. */
    int first_x = (int)(player_x / Q8) - GAME_W / 2;
    int first_y = (int)(player_y / Q8) - GAME_H / 2;
    for (int world_x = first_x; world_x <= first_x + GAME_W; world_x += 32) {
        int x0, y0, x1, y1;
        world_to_screen(world_x * Q8, top * Q8, &x0, &y0);
        world_to_screen(world_x * Q8, bottom * Q8, &x1, &y1);
        uint16_t color = world_x % 128 == 0 ? rgb565(52, 74, 85) : rgb565(32, 52, 65);
        line_rect(x0, y0, x1, y1, color, 1u);
    }
    for (int world_y = first_y; world_y <= first_y + GAME_H; world_y += 32) {
        int x0, y0, x1, y1;
        world_to_screen(left * Q8, world_y * Q8, &x0, &y0);
        world_to_screen(right * Q8, world_y * Q8, &x1, &y1);
        uint16_t color = world_y % 128 == 0 ? rgb565(52, 74, 85) : rgb565(32, 52, 65);
        line_rect(x0, y0, x1, y1, color, 1u);
    }
    int bx0, by0, bx1, by1, bx2, by2, bx3, by3;
    world_to_screen(2 * Q8, 2 * Q8, &bx0, &by0);
    world_to_screen((FIELD_W - 2) * Q8, 2 * Q8, &bx1, &by1);
    world_to_screen((FIELD_W - 2) * Q8, (FIELD_H - 2) * Q8, &bx2, &by2);
    world_to_screen(2 * Q8, (FIELD_H - 2) * Q8, &bx3, &by3);
    uint16_t border = rgb565(53, 82, 93);
    line_rect(bx0, by0, bx1, by1, border, 3u);
    line_rect(bx1, by1, bx2, by2, border, 3u);
    line_rect(bx2, by2, bx3, by3, border, 3u);
    line_rect(bx3, by3, bx0, by0, border, 3u);
}
#endif

static void draw_hud_reference(void) {
    fill_rect(27, 29, 15u, 5u, rgb565(237, 69, 77));
    fill_rect(32, 24, 5u, 15u, rgb565(237, 69, 77));
    unsigned hull_width = player_hp_max ? (unsigned)(current_player_health()*181u/player_hp_max) : 0u;
    fill_rect(53, 21, 188u, 24u, rgb565(9, 19, 28));
    outline_rect(53, 21, 188u, 24u, rgb565(155, 166, 178), 1u);
    fill_rect(56, 24, hull_width, 18u, rgb565(204, 59, 66));
    ui_ratio(123, 22, visible_player_health(), player_hp_max);
    if (player_shield_max) {
        fill_rect(53, 45, 188u, 24u, rgb565(9, 19, 28));
        outline_rect(53, 45, 188u, 24u, rgb565(155, 166, 178), 1u);
        fill_rect(56, 48, 181u * player_shield / player_shield_max, 18u, rgb565(63, 156, 202));
        ui_ratio(123, 48, player_shield, player_shield_max);
    }
    if (phase != USURPER_PHASE_TUTORIAL) ui_ratio(266, 22, unit_count, tech_capacity());
    ui_number(930, 21, alloy_run);
    fill_rect(12, 533, 936u, 6u, rgb565(19, 48, 62));
    fill_rect(12, 533, 936u * min_u(level_tick, BOSS_TIME_TICK) / BOSS_TIME_TICK, 6u, rgb565(57, 185, 214));
}

static void draw_capture_bar(int x, int y, unsigned value, unsigned width) {
    fill_rect(x, y, width, 3u, rgb565(26, 36, 42));
    fill_rect(x, y, width * min_u(value, 256u) / 256u, 3u, rgb565(57, 189, 255));
}

static int sparse_ring_emit_scene(void *context,const ts2d_command_t *command){
    (void)context;return emit_raw(command);
}

static void draw_range_ring(int x, int y, unsigned radius, unsigned alpha) {
    for (unsigned i = 0; i < USURPER_RANGE_RING_COUNT; ++i) {
        if (us_ui_range_rings[i].radius != radius) continue;
        const usurper_ui_region_t *region = &us_ui_range_rings[i].region;
        /* Atlas sizes already use framebuffer pixels. Scale the centre once,
         * then subtract the unscaled sprite half-size. */
        int left=scale_design_coord(x)-(int)region->w/2;
        int top=scale_design_coord(y)-(int)region->h/2;
        const us_sparse_ring_t *sparse=us_sparse_ring_find(sparse_ring_atlas_crc,
            USURPER_ATLAS_WIDTH,asset_height,radius,region->x,region->y,region->w,region->h);
        if(sparse){
            ts2d_command_t whole={.opcode=TS2D_OP_BLIT_ALPHA_PIXEL,
                .flags=TS2D_FLAG_CLIP_TO_VIEWPORT|TS2D_FLAG_SRC_ARGB4444,
                .src_format=TS2D_FMT_ARGB4444,.global_alpha=(uint8_t)min_u(alpha,255u),
                .src_base_addr=USURPER_ATLAS_BASE+(region->y+region->h>32767u?region->y*USURPER_ATLAS_STRIDE:0u),
                .dst_base_addr=current_framebuffer,.src_x=(int16_t)region->x,
                .src_y=region->y+region->h>32767u?0:(int16_t)region->y,
                .dst_x=(int16_t)left,.dst_y=(int16_t)top,.width=region->w,.height=region->h,
                .src_stride_bytes=USURPER_ATLAS_STRIDE,.dst_stride_bytes=USURPER_GPU_STRIDE};
            int result=us_sparse_ring_emit(sparse,&whole,sparse_ring_emit_scene,0);
            if(result!=-2){if(result)render_failed=1;return;}
        }
        ui_blit_alpha(region,left,top,alpha,0u);
        return;
    }
}

static void skimmer_scan_endpoint(unsigned radius, unsigned angle, int *x, int *y) {
    int dx, dy;
    direction_q8(angle, &dx, &dy);
    world_to_screen(render_player_x + dx * (int)radius,
                    render_player_y + dy * (int)radius, x, y);
}

static void draw_skimmer_scan(const SkimmerSweep *scan, unsigned angle, int turn, int px, int py) {
    int scan_x, scan_y;
    skimmer_scan_endpoint(scan->radius, angle, &scan_x, &scan_y);
    uint16_t trail_color = turn > 0 ? rgb565(57, 189, 255) : rgb565(29, 95, 128);
    uint16_t beam_color = turn > 0 ? rgb565(184, 239, 255) : rgb565(92, 120, 128);
    for (unsigned trail = 1u; trail <= 5u; ++trail) {
        unsigned a = angle_offset(angle, -turn * (int)(trail * 3u));
        int tx, ty;
        skimmer_scan_endpoint(scan->radius, a, &tx, &ty);
        line_rect_trail(px, py, tx, ty, trail_color);
    }
    line_rect(px, py, scan_x, scan_y, beam_color, 3u);
    small_ring(scan_x, scan_y, 8u, trail_color);
}

static void draw_death_fx(void) {
    for (unsigned i = 0; i < DEATH_FX_MAX; ++i) {
        DeathFx *fx = &death_fx[i];
        if (!fx->active) continue;
        int x, y;
        world_to_screen(fx->x, fx->y, &x, &y);
        unsigned radius = 3u + fx->age * 2u;
        uint16_t color = fx->age < 4u ? rgb565(255, 224, 150) :
            (fx->age < 12u ? (fx->species == SPECIES_GUNNER ? rgb565(195, 89, 251) : rgb565(255, 145, 77)) :
             rgb565(204u - min_u(fx->age * 3u, 120u), 65u, 48u));
        for (unsigned shard = 0; shard < 8u; ++shard) {
            unsigned angle = (shard * 45u + fx->age * 7u + fx->species * 19u) % 360u;
            int dx, dy;
            direction_q8(angle, &dx, &dy);
            int spark_x = x + dx * (int)radius / 256;
            int spark_y = y + dy * (int)radius / 256;
            int core_x = x + dx * (int)(radius / 2u) / 256;
            int core_y = y + dy * (int)(radius / 2u) / 256;
            unsigned spark_size = fx->age < 10u ? 4u : 3u;
            fill_rect(spark_x - (int)spark_size / 2, spark_y - (int)spark_size / 2,
                      spark_size, spark_size, color);
            fill_rect(core_x - 1, core_y - 1, 2u, 2u, rgb565(255, 224, 120));
        }
        if (fx->age < 5u) fill_rect(x - 3, y - 3, 6u, 6u, rgb565(255, 255, 255));
    }
}

#ifdef USURPER_FLAME_REFERENCE_SPRITES
typedef struct { float x, y, scale, cx, sy; } FlameCanvas;
static void flame_span(void *context, int x, int y, unsigned width, unsigned color, unsigned alpha) {
    (void)context;
    if (!width || !alpha || color >= 8u) return;
    unsigned row = us_ui_flame_palette.y + color;
    ts2d_command_t command = {.opcode = TS2D_OP_BLIT_ALPHA_CONST,
        .flags = TS2D_FLAG_CLIP_TO_VIEWPORT,
        .src_format = TS2D_FMT_RGB565, .global_alpha = (uint8_t)alpha,
        .src_base_addr = USURPER_ATLAS_BASE + (row > 32766u ? row * USURPER_ATLAS_STRIDE : 0),
        .dst_base_addr = current_framebuffer, .src_x = (int16_t)us_ui_flame_palette.x,
        .src_y = row > 32766u ? 0 : (int16_t)row, .dst_x = (int16_t)x, .dst_y = (int16_t)y,
        .width = (uint16_t)width, .height = 1,
        .src_stride_bytes = USURPER_ATLAS_STRIDE, .dst_stride_bytes = USURPER_GPU_STRIDE};
    (void)emit_raw(&command);
}
static void flame_polygon(void *context, const us_flame_vertex_t *vertices, unsigned count, unsigned color, unsigned alpha) {
    FlameCanvas *canvas = (FlameCanvas *)context;
    us_flame_vertex_t points[64];
    if (count > 64u) return;
    float xx = canvas->cx * canvas->scale, yy = canvas->sy * canvas->scale;
    for (unsigned i = 0; i < count; ++i) {
        points[i].x = canvas->x + vertices[i].x * xx - vertices[i].y * yy;
        points[i].y = canvas->y + vertices[i].x * yy + vertices[i].y * xx;
    }
    us_flame_raster_polygon(points, count, color, alpha, USURPER_GPU_WIDTH, USURPER_GPU_HEIGHT, flame_span, NULL);
}
static void flame_circle(void *context, float x, float y, float radius, unsigned color, unsigned alpha) {
    us_flame_vertex_t points[64]; us_flame_circle_polygon(x, y, radius, points);
    flame_polygon(context, points, 64u, color, alpha);
}
static void flame_rectangle(FlameCanvas *canvas, float x, float y, float width, float height, unsigned color) {
    us_flame_vertex_t points[4] = {{x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}};
    flame_polygon(canvas, points, 4u, color, 255u);
}
static void draw_flame_boss(int32_t x, int32_t y) {
    int32_t view_x = phase == USURPER_PHASE_MENU ? menu_demo_render_camera_x : render_camera_x;
    int32_t view_y = phase == USURPER_PHASE_MENU ? menu_demo_render_camera_y : render_camera_y;
    FlameCanvas canvas = {(x - view_x) / 256.0f * 1.6f + USURPER_GPU_WIDTH * 0.5f,
        (y - view_y) / 256.0f * 1.6f + USURPER_GPU_HEIGHT * 0.5f, 1.6f, 0, 0};
    us_fx_direction_float(flame_boss.rotation, &canvas.cx, &canvas.sy);
    const usurper_ui_region_t *body = &us_ui_flame_hulls_frames[flame_boss.flash > 0][boss_angle / 4u];
    ui_blit(body, (int)canvas.x + body->ox, (int)canvas.y + body->oy);
    if (flame_boss.state == 0u) {
        float dx = flame_boss.destination_x - flame_boss.x_float, dy = flame_boss.destination_y - flame_boss.y_float;
        float mx = dx * canvas.cx + dy * canvas.sy, my = -dx * canvas.sy + dy * canvas.cx;
        flame_rectangle(&canvas, mx - 23.5f, my - 23.5f, 47, 3, 0);
        flame_rectangle(&canvas, mx - 23.5f, my + 20.5f, 47, 3, 0);
        flame_rectangle(&canvas, mx - 23.5f, my - 20.5f, 3, 41, 0);
        flame_rectangle(&canvas, mx + 20.5f, my - 20.5f, 3, 41, 0);
        flame_rectangle(&canvas, mx - 9, my - 1, 18, 2, 7);
        flame_rectangle(&canvas, mx - 1, my - 9, 2, 18, 7);
    } else if (flame_boss.state == 2u) {
        float hx = flame_boss.locked_x * canvas.cx + flame_boss.locked_y * canvas.sy;
        float hy = -flame_boss.locked_x * canvas.sy + flame_boss.locked_y * canvas.cx;
        us_flame_painter_t painter = {&canvas, flame_polygon, flame_circle};
        us_flame_plume(hx, hy, 235 + flame_boss.phase * 25, flame_boss.elapsed, &painter);
    }
    if (flame_boss.core_exposed) {
        double pulse = 0.5 + 0.5 * us_fx_sin_double(flame_boss.elapsed * 14);
        unsigned alpha = (unsigned)((0.15 + 0.1 * pulse) * 255 + 0.5);
        flame_circle(&canvas, -2, 0, (float)(6 + 2 * pulse), 5u, alpha);
        flame_circle(&canvas, -2, 0, 3, 6u, 255u);
    }
    ui_text(scale_design_coord(design_x(144)), scale_design_coord(design_y(323)), us_campaign_stage(3u)->name);
    fill_rect(design_x(144), design_y(338), design_x(350), design_y(9), rgb565(42,24,36));
    fill_rect(design_x(144), design_y(338), (unsigned)(design_x(350) * flame_boss.hp / flame_boss.max_hp), design_y(9), rgb565(255,71,126));
}
#endif

#ifdef USURPER_TWIN_REFERENCE_SPRITES
static void twin_span(void *context, int x, int y, unsigned width, unsigned color, unsigned alpha) {
    (void)context;
    if (!width || !alpha || color >= 9u) return;
    unsigned row = us_ui_twin_palette.y + color;
    ts2d_command_t command = {.opcode = TS2D_OP_BLIT_ALPHA_CONST, .flags = TS2D_FLAG_CLIP_TO_VIEWPORT,
        .src_format = TS2D_FMT_RGB565, .global_alpha = (uint8_t)alpha,
        .src_base_addr = USURPER_ATLAS_BASE + (row > 32766u ? row * USURPER_ATLAS_STRIDE : 0),
        .dst_base_addr = current_framebuffer, .src_x = (int16_t)us_ui_twin_palette.x,
        .src_y = row > 32766u ? 0 : (int16_t)row, .dst_x = (int16_t)x, .dst_y = (int16_t)y,
        .width = (uint16_t)width, .height = 1,
        .src_stride_bytes = USURPER_ATLAS_STRIDE, .dst_stride_bytes = USURPER_GPU_STRIDE};
    (void)emit_raw(&command);
}
static void twin_polygon(FlameCanvas *canvas, const us_flame_vertex_t *vertices, unsigned count, unsigned color, unsigned alpha) {
    us_flame_vertex_t points[64]; if (count > 64u) return;
    float xx = canvas->cx * canvas->scale, yy = canvas->sy * canvas->scale;
    for (unsigned i = 0; i < count; ++i) {
        points[i].x = canvas->x + vertices[i].x * xx - vertices[i].y * yy;
        points[i].y = canvas->y + vertices[i].x * yy + vertices[i].y * xx;
    }
    us_flame_raster_polygon(points, count, color, alpha, USURPER_GPU_WIDTH, USURPER_GPU_HEIGHT, twin_span, NULL);
}
static FlameCanvas twin_canvas(float x, float y, float rotation) {
    int32_t vx = phase == USURPER_PHASE_MENU ? menu_demo_render_camera_x : render_camera_x;
    int32_t vy = phase == USURPER_PHASE_MENU ? menu_demo_render_camera_y : render_camera_y;
    FlameCanvas canvas = {(x - vx / 256.0f) * 1.6f + USURPER_GPU_WIDTH * 0.5f,
        (y - vy / 256.0f) * 1.6f + USURPER_GPU_HEIGHT * 0.5f, 1.6f, 0, 0};
    us_fx_direction_float(rotation, &canvas.cx, &canvas.sy); return canvas;
}
static void twin_rect(FlameCanvas *canvas, float x, float y, float w, float h, unsigned color, unsigned alpha) {
    us_flame_vertex_t points[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    twin_polygon(canvas, points, 4u, color, alpha);
}
static void twin_line(FlameCanvas *canvas, float x, float y, float tx, float ty, float width, unsigned color, unsigned alpha) {
    float dx = tx - x, dy = ty - y, length = us_fx_sqrt_float(dx * dx + dy * dy);
    if (!length) return;
    float nx = -dy / length * width * 0.5f, ny = dx / length * width * 0.5f;
    us_flame_vertex_t points[4] = {{x + nx, y + ny}, {tx + nx, ty + ny}, {tx - nx, ty - ny}, {x - nx, y - ny}};
    twin_polygon(canvas, points, 4u, color, alpha);
}
static void twin_outline(FlameCanvas *canvas, float x, float y, float width, unsigned color, unsigned alpha) {
    twin_rect(canvas, x - 6, y - 6, 12, width, color, alpha);
    twin_rect(canvas, x - 6, y + 6 - width, 12, width, color, alpha);
    twin_rect(canvas, x - 6, y - 6 + width, width, 12 - width * 2, color, alpha);
    twin_rect(canvas, x + 6 - width, y - 6 + width, width, 12 - width * 2, color, alpha);
}
static unsigned twin_angle(float rotation) {
    int angle = (int)(rotation * (180 / 3.14159265358979323846));
    angle %= 360; if (angle < 0) angle += 360; return (unsigned)angle / 4u;
}
static void draw_twin_boss(void) {
    for (unsigned i = 0; i < 8u; ++i) if (twin_explosions[i].active) {
        double age = twin_explosions[i].age;
        if (age >= 0.2 && ((unsigned)((age - 0.2) / 0.05) & 1u)) continue;
        double growth = age / 0.055; if (growth > 1) growth = 1;
        if (growth < 0.5) growth = 4 * growth * growth * growth;
        else { double shifted = -2 * growth + 2; growth = 1 - shifted * shifted * shifted * 0.5; }
        float half = (float)(88 / 1.4142135623730950488 * growth), corner = half * 0.32f;
        if (corner < 2) corner = 2;
        double ratio = 1 - age / 0.55;
        FlameCanvas canvas = twin_canvas(twin_explosions[i].x, twin_explosions[i].y, 0);
        twin_rect(&canvas, -half, -half, half * 2, half * 2, 3, (unsigned)(ratio * 0.08 * 255 + 0.5));
        for (unsigned side = 0; side < 4u; ++side) {
            float sx = side == 0 || side == 3 ? -1.0f : 1.0f, sy = side < 2u ? -1.0f : 1.0f;
            float x = sx * half, y = sy * half; unsigned color = age < 0.2 ? 8u : 3u;
            twin_line(&canvas, x - sx * corner, y, x, y, 2, color, (unsigned)(ratio * 255 + 0.5));
            twin_line(&canvas, x, y, x, y - sy * corner, 2, color, (unsigned)(ratio * 255 + 0.5));
        }
        float radius = (float)(2.4 * ratio * (88 / (13 * 1.4142135623730950488))); if (radius < 0.8f) radius = 0.8f;
        us_flame_vertex_t circle[64]; us_flame_circle_polygon(0, 0, radius, circle);
        twin_polygon(&canvas, circle, 64u, 8u, (unsigned)(ratio * 0.72 * 255 + 0.5));
    }
    for (unsigned i = 0; i < US_TWIN_BOMBS; ++i) {
        const us_twin_bomb_t *bomb = &twin_boss.bombs[i]; if (!bomb->active) continue;
        FlameCanvas canvas = twin_canvas(bomb->x, bomb->y, 0);
        if (twin_boss.actors[1].active)
            twin_line(&canvas, twin_boss.actors[1].x - bomb->x, twin_boss.actors[1].y - bomb->y, 0, 0, 1, 6, 77);
        float dx = bomb->landing_x - bomb->x, dy = bomb->landing_y - bomb->y;
        float half = (float)(88 / 1.4142135623730950488), corner = half * 0.32f;
        double progress = bomb->age / 1.6; if (progress > 1) progress = 1;
        twin_rect(&canvas, dx - half, dy - half, half * 2, half * 2, 6, (unsigned)((0.1 + 0.16 * progress) * 255 + 0.5));
        for (unsigned side = 0; side < 4u; ++side) {
            float sx = side == 0 || side == 3 ? -1.0f : 1.0f, sy = side < 2u ? -1.0f : 1.0f;
            float x = dx + sx * half, y = dy + sy * half;
            twin_line(&canvas, x - sx * corner, y, x, y, 2.0f, 6, 166);
            twin_line(&canvas, x, y, x, y - sy * corner, 2.0f, 6, 166);
        }
        twin_rect(&canvas, dx - 2, dy - 2, 4, 4, 6, 166);
        double blink = bomb->lifetime - (int)(bomb->lifetime / 0.14) * 0.14;
        twin_rect(&canvas, -4, -4, 8, 8, bomb->lifetime < 0.55 && blink < 0.07 ? 7u : 6u, 255u);
    }
    for (unsigned role = 0; role < 2u; ++role) {
        const us_twin_actor_t *a = &twin_boss.actors[role]; if (!a->active) continue;
        FlameCanvas canvas = twin_canvas(a->x, a->y, a->rotation); unsigned angle = twin_angle(a->rotation);
        const usurper_ui_region_t *body;
        if (!role) {
            for (unsigned part = 0; part < US_TWIN_PARTS; ++part) {
                if (twin_boss.part_hp[part] <= 0) continue;
                float hx, hy; us_fx_direction_float((float)(part * (6.28318530717958647692 / 6)), &hx, &hy);
                twin_line(&canvas, hx * 26 * 0.45f, hy * 26 * 0.45f, hx * 26, hy * 26, 5, a->flash > 0 ? 1u : 0u, 82);
            }
            unsigned level = a->shielded ? 0u : twin_boss.remaining ? 1u : 2u;
            if (a->flash > 0) level += 3u;
            body = &us_ui_twin_splitter_frames[level][angle];
        } else body = &us_ui_twin_bomber_frames[a->flash > 0][angle];
        ui_blit(body, (int)canvas.x + body->ox, (int)canvas.y + body->oy);
        if (!role) {
            if (a->shielded) {
                const usurper_ui_region_t *shield = &us_ui_twin_shield_frames[a->shield_flash > 0][angle];
                ui_blit(shield, (int)canvas.x + shield->ox, (int)canvas.y + shield->oy);
            } else if (a->shield_flash > 0) {
                double progress = 1 - a->shield_flash / 0.5;
                for (unsigned part = 0; part < 6u; ++part) {
                    float hx, hy; us_fx_direction_float((float)(part * (6.28318530717958647692 / 6)), &hx, &hy);
                    twin_rect(&canvas, hx * (float)(44 + progress * 24) - 3, hy * (float)(44 + progress * 24) - 3, 6, 6, 0, (unsigned)((1 - progress) * 255 + 0.5));
                }
            }
            if (a->move_state == 1u) {
                float dx = a->locked_x * canvas.cx + a->locked_y * canvas.sy;
                float dy = -a->locked_x * canvas.sy + a->locked_y * canvas.cx;
                for (int offset = -9; offset <= 9; offset += 18)
                    twin_line(&canvas, dx * 48 + dy * offset, dy * 48 - dx * offset, dx * 61 + dy * offset, dy * 61 - dx * offset, 4, a->flash > 0 ? 1u : 0u, 255);
            }
            for (unsigned part = 0; part < US_TWIN_PARTS; ++part) {
                int32_t px, py; if (!us_twin_target_position(&twin_boss, part, &px, &py)) continue;
                FlameCanvas part_canvas = twin_canvas(px / 65536.0f, py / 65536.0f, a->rotation);
                unsigned level = twin_boss.part_flash[part] > 0 ? 2u : a->shielded ? 0u : 1u;
                const usurper_ui_region_t *plate = &us_ui_twin_parts_frames[level][angle];
                ui_blit(plate, (int)part_canvas.x + plate->ox, (int)part_canvas.y + plate->oy);
            }
        } else {
            if (a->cast_left > 0 || a->shot_flash > 0) twin_rect(&canvas, -5, -5, 10, 10, 4, 255);
            if (a->cast_left > 0) {
                float dx = a->cast_x - a->x, dy = a->cast_y - a->y;
                twin_outline(&canvas, dx * canvas.cx + dy * canvas.sy, -dx * canvas.sy + dy * canvas.cx, 2, a->flash > 0 ? 1u : 3u, 179);
            }
        }
    }
    for (unsigned role = 0; role < 2u; ++role) {
        const us_twin_actor_t *a = &twin_boss.actors[role]; int dx = design_x(144 + (int)role * 180);
        ui_text(scale_design_coord(dx), scale_design_coord(design_y(323)), role ? "轰炸者" : "分裂者");
        fill_rect(dx, design_y(338), design_x(170), design_y(11), rgb565(42,24,36));
        fill_rect(dx, design_y(338), (unsigned)(design_x(170) * a->hp / a->max_hp), design_y(11), rgb565(233,86,101));
        if (!role && a->active && a->shielded) fill_rect(design_x(269), design_y(325), design_x(45), design_y(7), rgb565(73,182,235));
    }
}
#endif

typedef struct {
    uint16_t colors[USURPER_DYNAMIC_PALETTE_ROWS],color_slots[USURPER_DYNAMIC_PALETTE_ROWS]; unsigned count, alpha,bank;
    uint32_t palette, source; int x,y; unsigned width,height; unsigned failed,prepared;uint16_t rgb;
    us_conversion_mask_t conversion;us_boundary_color_t modulate,control;unsigned modulated;uint16_t converted_rgb,source_rgb;
    float owner_x,owner_y,owner_rotation,owner_scale,paint_alpha;unsigned local_lines,alpha_cached;
    int cache_x,cache_y;
} BoundaryCanvas;
typedef struct {uint16_t colors[USURPER_DYNAMIC_PALETTE_ROWS];uint8_t valid[USURPER_DYNAMIC_PALETTE_ROWS];uint32_t frame[USURPER_DYNAMIC_PALETTE_ROWS];unsigned cursor;} BoundaryPalette;
static BoundaryPalette boundary_palette_cache[2];
static uint32_t geometry_frame_serial;
typedef struct {unsigned offset,count,line,alpha;float width;us_boundary_color_t tint;uint16_t rgb,converted_rgb;unsigned aa;float owner_x,owner_y,owner_rotation,owner_scale;} BoundaryPrimitive;
static BoundaryPrimitive boundary_primitives[512];
static us_flame_vertex_t boundary_vertices[4096];
static unsigned boundary_primitive_count,boundary_vertex_count;
static uint32_t boundary_times[4];
unsigned usurper_scene_boundary_time(unsigned part){return part<4u?boundary_times[part]:0;}
static uint32_t boundary_now(void) {
#ifdef USURPER_HOST
    return 0;
#else
    return now();
#endif
}
static unsigned boundary_channel(float value) { return value <= 0 ? 0 : value >= 1 ? 255u : (unsigned)us_sf_add(us_sf_mul(value,255),0.5f); }
static void boundary_register_color(BoundaryCanvas *canvas,unsigned alpha,uint16_t rgb){
    if(alpha==255u||rgb==rgb565(255,98,123)||rgb==rgb565(98,120,136))return;
    unsigned row=0;while(row<canvas->count&&canvas->colors[row]!=rgb)++row;
    if(row==canvas->count){if(row==USURPER_DYNAMIC_PALETTE_ROWS){canvas->failed=1;return;}canvas->colors[canvas->count++]=rgb;}
}
static void boundary_record(void *context,const us_flame_vertex_t *points,unsigned count,float width,us_boundary_color_t tint,unsigned line) {
    BoundaryCanvas *canvas=context;
    if(canvas->modulated){tint.r=us_sf_mul(tint.r,canvas->modulate.r);tint.g=us_sf_mul(tint.g,canvas->modulate.g);tint.b=us_sf_mul(tint.b,canvas->modulate.b);tint.a=us_sf_mul(tint.a,canvas->modulate.a);}
    unsigned alpha=boundary_channel(tint.a);if(!alpha||!count)return;
    float margin=line?width*0.5f:0,left=render_camera_x/256.0f-320-margin,top=render_camera_y/256.0f-180-margin;
    float right=left+640+2*margin,bottom=top+360+2*margin;unsigned outside=15u;
    for(unsigned i=0;i<count;++i)outside&=(points[i].x<left?1u:0u)|(points[i].x>right?2u:0u)|
        (points[i].y<top?4u:0u)|(points[i].y>bottom?8u:0u);
    if(outside&&!canvas->local_lines)return;
    if(boundary_primitive_count==512u||count>4096u-boundary_vertex_count){canvas->failed=1;return;}
    uint16_t rgb=(uint16_t)rgb565(boundary_channel(tint.r),boundary_channel(tint.g),boundary_channel(tint.b));
    uint16_t converted=rgb;
    if(canvas->conversion.mode){us_boundary_color_t c=us_conversion_color(tint,0,1,-9,9,canvas->control,0);converted=rgb565(boundary_channel(c.r),boundary_channel(c.g),boundary_channel(c.b));}
    boundary_primitives[boundary_primitive_count++]=(BoundaryPrimitive){.offset=boundary_vertex_count,.count=count,.line=line,.alpha=alpha,.width=width,.tint=tint,.rgb=rgb,.converted_rgb=converted,
        .owner_x=canvas->owner_x,.owner_y=canvas->owner_y,.owner_rotation=canvas->owner_rotation,.owner_scale=canvas->owner_scale};
    memcpy(boundary_vertices+boundary_vertex_count,points,count*sizeof(*points));boundary_vertex_count+=count;
    boundary_register_color(canvas,alpha,rgb);if(canvas->conversion.mode)boundary_register_color(canvas,alpha,converted);
}
static void boundary_record_polygon(void *ctx,const us_flame_vertex_t *p,unsigned n,us_boundary_color_t c){boundary_record(ctx,p,n,0,c,0);}
static void boundary_record_line(void *ctx,const us_flame_vertex_t *p,unsigned n,float w,us_boundary_color_t c){boundary_record(ctx,p,n,w,c,1);}
static void boundary_record_mesh_line(void *ctx,const us_flame_vertex_t *p,unsigned n,float w,us_boundary_color_t c,unsigned aa){
    BoundaryCanvas *canvas=ctx;unsigned before=boundary_primitive_count;
    /* Local source coordinates cannot be culled against the world viewport. */
    boundary_record(ctx,p,n,w,c,2);
    if(boundary_primitive_count!=before){BoundaryPrimitive *line=&boundary_primitives[before];line->aa=aa;if(aa) {boundary_register_color(canvas,1,line->rgb);boundary_register_color(canvas,1,line->converted_rgb);}}
}
static void boundary_flush_span(BoundaryCanvas *canvas) {
    if (!canvas->height) return;
    us_command_words_t packet=us_command_pack(canvas->alpha==255u?TS2D_OP_FILL_RECT:TS2D_OP_BLIT_ALPHA_CONST,
        TS2D_FLAG_CLIP_TO_VIEWPORT,TS2D_FMT_RGB565,(uint8_t)canvas->alpha,canvas->source,current_framebuffer,
        0,0,(int16_t)canvas->x,(int16_t)canvas->y,(uint16_t)canvas->width,(uint16_t)canvas->height,
        USURPER_GPU_STRIDE,USURPER_GPU_STRIDE,canvas->rgb);
    (void)emit_raw(&packet.command); canvas->height=0;
}
static void boundary_solid_span(BoundaryCanvas *canvas,int x,int y,unsigned width){
    if(!width)return;
    /* Adjacent pixels of one resolved paint may share a command. These
     * sources are uniform across the full 1024-pixel row; do not combine
     * overlapping spans or a previously extended vertical rectangle. */
    if(canvas->height==1u&&canvas->y==y&&canvas->x+(int)canvas->width==x&&
        width<=USURPER_GPU_WIDTH&&canvas->width<=USURPER_GPU_WIDTH-width){
        canvas->width+=width;return;
    }
    unsigned solid=canvas->alpha==255u||canvas->source<USURPER_DYNAMIC_PALETTE_BASE;
    if(canvas->height&&canvas->x==x&&canvas->width==width&&canvas->y+(int)canvas->height==y&&
        (solid||canvas->height<USURPER_DYNAMIC_PALETTE_TILE_HEIGHT)) { ++canvas->height; return; }
    boundary_flush_span(canvas);canvas->x=x;canvas->y=y;canvas->width=width;canvas->height=1;
}
static void boundary_select_paint(BoundaryCanvas *canvas,unsigned alpha,uint16_t rgb){
    if(alpha==255u)canvas->source=0;
    else if(rgb==rgb565(255,98,123)) canvas->source=USURPER_SOLID_BASE;
    else if(rgb==rgb565(98,120,136)) canvas->source=USURPER_SOLID_BASE+USURPER_SOLID_BYTES;
    else {
        unsigned row=0;while(row<canvas->count&&canvas->colors[row]!=rgb) ++row;
        if(row==canvas->count){canvas->failed=1;return;}
        canvas->source=canvas->palette+canvas->color_slots[row]*USURPER_DYNAMIC_PALETTE_TILE_HEIGHT*USURPER_GPU_STRIDE;
    }
    canvas->alpha=alpha;
}
static void boundary_painted_span(BoundaryCanvas *canvas,int x,int y,unsigned width,uint16_t rgb){
    if(!width)return;
    if(canvas->rgb!=rgb){boundary_flush_span(canvas);canvas->rgb=rgb;boundary_select_paint(canvas,canvas->alpha,rgb);}
    boundary_solid_span(canvas,x,y,width);
}
static void boundary_emit_span(BoundaryCanvas *canvas,int x,int y,unsigned width){
    if(!canvas->conversion.mode){boundary_solid_span(canvas,x,y,width);return;}
    /* The primitive's original paint is kept separately from the span merger. */
    uint16_t original=canvas->source_rgb;
    unsigned filled;unsigned first=us_conversion_mask_split(&canvas->conversion,x,y,width,&filled);
    boundary_painted_span(canvas,x,y,first,filled?canvas->converted_rgb:original);
    boundary_painted_span(canvas,x+(int)first,y,width-first,filled?original:canvas->converted_rgb);
}
static void boundary_span(void *context,int x,int y,unsigned width,unsigned color,unsigned alpha){
    us_geometry_cache_span(x,y,width);(void)color;(void)alpha;boundary_emit_span(context,x,y,width);
}
static void boundary_resolved_alpha(void *context,int x,int y,unsigned width,unsigned alpha){
    BoundaryCanvas *canvas=context;if(!alpha)return;
    x+=canvas->cache_x;y+=canvas->cache_y;
    if(canvas->alpha!=alpha){boundary_flush_span(canvas);boundary_select_paint(canvas,alpha,canvas->rgb);}
    boundary_emit_span(canvas,x,y,width);
}
static void boundary_coverage(void *context,int x,int y,unsigned width,float coverage){
    BoundaryCanvas *canvas=context;unsigned alpha=boundary_channel(us_sf_mul(canvas->paint_alpha,coverage));
    if(canvas->alpha_cached)us_geometry_cache_alpha(x,y,width,alpha);
    else us_geometry_cache_coverage(x,y,width,coverage);
    boundary_resolved_alpha(context,x,y,width,alpha);
}
static void boundary_cached_alpha(void *context,int x,int y,unsigned width,unsigned alpha){
    us_geometry_cache_alpha(x,y,width,alpha);
    boundary_resolved_alpha(context,x,y,width,alpha);
}
static void boundary_polygon(void *context,const us_flame_vertex_t *vertices,unsigned count,us_boundary_color_t tint) {
    BoundaryCanvas *canvas=context; if(count>US_RASTER_POLYGON_MAX) return;
    unsigned alpha=canvas->prepared?canvas->alpha:boundary_channel(tint.a);if(!alpha) return;
    float left=render_camera_x/256.0f-320,top=render_camera_y/256.0f-180,right=left+640,bottom=top+360;
    unsigned outside=15u;
    for(unsigned i=0;i<count;++i) outside&=(vertices[i].x<left?1u:0u)|(vertices[i].x>right?2u:0u)|
        (vertices[i].y<top?4u:0u)|(vertices[i].y>bottom?8u:0u);
    if(outside)return;
    uint16_t rgb=canvas->prepared?canvas->source_rgb:(uint16_t)rgb565(boundary_channel(tint.r),boundary_channel(tint.g),boundary_channel(tint.b));
    canvas->rgb=rgb;
    boundary_select_paint(canvas,alpha,rgb);if(canvas->failed)return;
    us_flame_vertex_t points[US_RASTER_POLYGON_MAX];float camera_x=render_camera_x/256.0f,camera_y=render_camera_y/256.0f;
    for(unsigned i=0;i<count;++i) {
        points[i].x=us_sf_add(us_sf_mul(us_sf_add(vertices[i].x,-camera_x),1.6f),USURPER_GPU_WIDTH*0.5f);
        points[i].y=us_sf_add(us_sf_mul(us_sf_add(vertices[i].y,-camera_y),1.6f),USURPER_GPU_HEIGHT*0.5f);
    }
    us_raster_polygon(points,count,0u,alpha,USURPER_GPU_WIDTH,USURPER_GPU_HEIGHT,boundary_span,canvas);
    us_geometry_cache_flush();
    boundary_flush_span(canvas);
}
static void boundary_line(void *context,const us_flame_vertex_t *points,unsigned count,float width,us_boundary_color_t tint) {
    float margin=width*0.5f,left=render_camera_x/256.0f-320-margin,top=render_camera_y/256.0f-180-margin;
    float right=left+640+2*margin,bottom=top+360+2*margin;
    for(unsigned i=1;i<count;++i) {
        if((points[i-1].x<left&&points[i].x<left)||(points[i-1].x>right&&points[i].x>right)||
            (points[i-1].y<top&&points[i].y<top)||(points[i-1].y>bottom&&points[i].y>bottom))continue;
        float dx=us_sf_add(points[i].x,-points[i-1].x),dy=us_sf_add(points[i].y,-points[i-1].y);
        float length=us_fx_sqrt_float(us_sf_add(us_sf_mul(dx,dx),us_sf_mul(dy,dy)));
        if(!length) continue;
        float nx=us_sf_mul(us_sf_mul(us_sf_div(-dy,length),width),0.5f),ny=us_sf_mul(us_sf_mul(us_sf_div(dx,length),width),0.5f);
        us_flame_vertex_t quad[4]={{us_sf_add(points[i-1].x,nx),us_sf_add(points[i-1].y,ny)},{us_sf_add(points[i].x,nx),us_sf_add(points[i].y,ny)},
            {us_sf_add(points[i].x,-nx),us_sf_add(points[i].y,-ny)},{us_sf_add(points[i-1].x,-nx),us_sf_add(points[i-1].y,-ny)}};
        boundary_polygon(context,quad,4u,tint);
    }
}
static void geometry_replay_span(void *context,int x,int y,unsigned width){boundary_span(context,x,y,width,0,0);}
static void geometry_replay_flush(void *context){boundary_flush_span(context);}
static void boundary_mesh_line(BoundaryCanvas *canvas,const BoundaryPrimitive *primitive){
    us_polyline_mesh_t mesh;const us_flame_vertex_t *points=boundary_vertices+primitive->offset;
    if(!us_polyline_mesh(points,primitive->count,primitive->width,primitive->aa,&mesh)){canvas->failed=1;return;}
    float cx,sy;us_fx_direction_float(primitive->owner_rotation,&cx,&sy);cx=us_sf_mul(cx,primitive->owner_scale);sy=us_sf_mul(sy,primitive->owner_scale);
    for(unsigned part=0;part<mesh.count;++part){us_polyline_strip_t *strip=&mesh.strips[part];
        for(unsigned i=0;i<strip->count;++i){us_polyline_vertex_t *v=&strip->vertices[i];float x=us_sf_add(us_sf_add(us_sf_mul(cx,v->x),us_sf_mul(-sy,v->y)),primitive->owner_x);
            float y=us_sf_add(us_sf_add(us_sf_mul(sy,v->x),us_sf_mul(cx,v->y)),primitive->owner_y);
            v->x=us_sf_add(us_sf_mul(us_sf_add(x,-render_camera_x/256.0f),1.6f),512);v->y=us_sf_add(us_sf_mul(us_sf_add(y,-render_camera_y/256.0f),1.6f),288);}
        for(unsigned i=2;i<strip->count;++i){us_polyline_vertex_t triangle[3]={strip->vertices[i-2],strip->vertices[i-1],strip->vertices[i]};
            if(canvas->alpha_cached&&us_geometry_cache_available())us_triangle_resolved_alpha(triangle,USURPER_GPU_WIDTH,USURPER_GPU_HEIGHT,canvas->paint_alpha,boundary_cached_alpha,canvas);
            else us_triangle_coverage(triangle,USURPER_GPU_WIDTH,USURPER_GPU_HEIGHT,boundary_coverage,canvas);
            us_geometry_cache_flush();boundary_flush_span(canvas);}
    }
}
static unsigned boundary_translated_mesh(BoundaryCanvas *canvas,const BoundaryPrimitive *primitive){
    if(primitive->line!=2||!us_geometry_cache_available())return 0;
    unsigned worst_vertices=primitive->aa?primitive->count*6u+14u:primitive->count*2u;
    if(3u+(primitive->aa?3u:1u)+3u*worst_vertices>US_GEOMETRY_CACHE_KEY_MAX/4u)return 0;
    us_polyline_mesh_t mesh;const us_flame_vertex_t *points=boundary_vertices+primitive->offset;
    if(!us_polyline_mesh(points,primitive->count,primitive->width,primitive->aa,&mesh))return 0;
    unsigned words=3u+mesh.count;for(unsigned part=0;part<mesh.count;++part)words+=3u*mesh.strips[part].count;
    if(words>US_GEOMETRY_CACHE_KEY_MAX/4u)return 0;
    uint32_t key[US_GEOMETRY_CACHE_KEY_MAX/4u];key[0]=0x584d4754u;memcpy(&key[1],&canvas->paint_alpha,4);key[2]=mesh.count;
    float cx,sy;us_fx_direction_float(primitive->owner_rotation,&cx,&sy);cx=us_sf_mul(cx,primitive->owner_scale);sy=us_sf_mul(sy,primitive->owner_scale);
    unsigned at=3;int minx=INT32_MAX,miny=INT32_MAX;
    for(unsigned part=0;part<mesh.count;++part){us_polyline_strip_t *strip=&mesh.strips[part];key[at++]=strip->count;
        for(unsigned i=0;i<strip->count;++i){us_polyline_vertex_t *v=&strip->vertices[i];float x=us_sf_add(us_sf_add(us_sf_mul(cx,v->x),us_sf_mul(-sy,v->y)),primitive->owner_x);
            float y=us_sf_add(us_sf_add(us_sf_mul(sy,v->x),us_sf_mul(cx,v->y)),primitive->owner_y);
            x=us_sf_add(us_sf_mul(us_sf_add(x,-render_camera_x/256.0f),1.6f),512);y=us_sf_add(us_sf_mul(us_sf_add(y,-render_camera_y/256.0f),1.6f),288);
            if(!(x>=0&&x<=USURPER_GPU_WIDTH&&y>=0&&y<=USURPER_GPU_HEIGHT))return 0;
            /* Match the raster's 1/256 grid exactly before removing an integer
             * translation. All accepted meshes are wholly inside the viewport;
             * clipped/oversized shapes keep the original path below. */
            int gx=us_triangle_grid(x),gy=us_triangle_grid(y);
            if(gx<minx)minx=gx;
            if(gy<miny)miny=gy;
            key[at++]=(uint32_t)gx;key[at++]=(uint32_t)gy;memcpy(&key[at++],&v->alpha,4);
        }
    }
    int origin_x=minx>>8,origin_y=miny>>8;at=3;
    for(unsigned part=0;part<mesh.count;++part){us_polyline_strip_t *strip=&mesh.strips[part];++at;
        for(unsigned i=0;i<strip->count;++i){key[at]-=(uint32_t)(origin_x*256);key[at+1]-=(uint32_t)(origin_y*256);
            strip->vertices[i].x=key[at]/256.0f;strip->vertices[i].y=key[at+1]/256.0f;at+=3;}
    }
    boundary_select_paint(canvas,canvas->alpha,canvas->rgb);if(canvas->failed)return 1;
    canvas->cache_x=origin_x;canvas->cache_y=origin_y;canvas->alpha_cached=1;
    if(!us_geometry_cache_begin_alpha(key,words*4u,boundary_resolved_alpha,geometry_replay_flush,canvas)){
        for(unsigned part=0;part<mesh.count;++part){us_polyline_strip_t *strip=&mesh.strips[part];
            for(unsigned i=2;i<strip->count;++i){us_polyline_vertex_t triangle[3]={strip->vertices[i-2],strip->vertices[i-1],strip->vertices[i]};
                us_triangle_resolved_alpha(triangle,USURPER_GPU_WIDTH-(unsigned)origin_x,USURPER_GPU_HEIGHT-(unsigned)origin_y,canvas->paint_alpha,boundary_cached_alpha,canvas);
                us_geometry_cache_flush();boundary_flush_span(canvas);}
        }
        us_geometry_cache_end();
    }
    canvas->cache_x=canvas->cache_y=0;canvas->alpha_cached=0;return 1;
}
static void boundary_cached_primitive_body(BoundaryCanvas *canvas,const BoundaryPrimitive *primitive){
    const us_flame_vertex_t *points=boundary_vertices+primitive->offset;
    canvas->alpha_cached=0;
    if(boundary_translated_mesh(canvas,primitive))return;
    if(44u+primitive->count*sizeof(*points)>US_GEOMETRY_CACHE_KEY_MAX){
        if(primitive->line==2){boundary_mesh_line(canvas,primitive);return;}
        if(primitive->line)boundary_line(canvas,points,primitive->count,primitive->width,primitive->tint);
        else boundary_polygon(canvas,points,primitive->count,primitive->tint);
        return;
    }
    uint8_t key[US_GEOMETRY_CACHE_KEY_MAX];uint32_t words[11]={primitive->line,primitive->count,0,(uint32_t)render_camera_x,(uint32_t)render_camera_y,primitive->aa,0,0,0,0,0};
    memcpy(&words[2],&primitive->width,4);memcpy(&words[6],&primitive->owner_x,4);memcpy(&words[7],&primitive->owner_y,4);memcpy(&words[8],&primitive->owner_rotation,4);memcpy(&words[9],&primitive->owner_scale,4);
    if(primitive->line==2)memcpy(&words[10],&canvas->paint_alpha,4);
    memcpy(key,words,sizeof(words));memcpy(key+sizeof(words),points,primitive->count*sizeof(*points));
    boundary_select_paint(canvas,canvas->alpha,canvas->rgb);if(canvas->failed)return;
    canvas->alpha_cached=primitive->line==2;
    unsigned replayed=primitive->line==2?us_geometry_cache_begin_alpha(key,(unsigned)(sizeof(words)+primitive->count*sizeof(*points)),boundary_resolved_alpha,geometry_replay_flush,canvas):us_geometry_cache_begin(key,(unsigned)(sizeof(words)+primitive->count*sizeof(*points)),geometry_replay_span,geometry_replay_flush,canvas);
    if(!replayed){
        if(primitive->line==2)boundary_mesh_line(canvas,primitive);
        else if(primitive->line)boundary_line(canvas,points,primitive->count,primitive->width,primitive->tint);
        else boundary_polygon(canvas,points,primitive->count,primitive->tint);
        us_geometry_cache_end();
    }
}
static int triangle_emit(void *context,const ts2d_command_t *command){(void)context;return emit_raw(command);}
#include "usurper_line_mesh_cache.h"
static int boundary_triangle_mesh(BoundaryCanvas *canvas,const BoundaryPrimitive *primitive){
    us_triangle_begin(&triangle_batch);
    us_polyline_mesh_t mesh;const us_flame_vertex_t *points=boundary_vertices+primitive->offset;
    if(!us_line_mesh_cached(points,primitive->width,&mesh)){return 0;}
    float cx,sy;us_fx_direction_float(primitive->owner_rotation,&cx,&sy);cx=us_sf_mul(cx,primitive->owner_scale);sy=us_sf_mul(sy,primitive->owner_scale);
    /* Core and feather strips share vertices. Reuse the exact transformed
     * bit pattern within this primitive; paint/alpha and triangle order stay live. */
    uint32_t keys_x[32],keys_y[32];float screen_x[32],screen_y[32];unsigned transformed=0;
    for(unsigned part=0;part<mesh.count;++part){us_polyline_strip_t *strip=&mesh.strips[part];
        for(unsigned i=0;i<strip->count;++i){us_polyline_vertex_t *v=&strip->vertices[i];
            union {float f;uint32_t u;} key_x={.f=v->x},key_y={.f=v->y};
            unsigned found=0;while(found<transformed&&(keys_x[found]!=key_x.u||keys_y[found]!=key_y.u))++found;
            if(found<transformed){v->x=screen_x[found];v->y=screen_y[found];continue;}
            float x=us_sf_add(us_sf_add(us_sf_mul(cx,v->x),us_sf_mul(-sy,v->y)),primitive->owner_x);
            float y=us_sf_add(us_sf_add(us_sf_mul(sy,v->x),us_sf_mul(cx,v->y)),primitive->owner_y);
            v->x=us_sf_add(us_sf_mul(us_sf_add(x,-render_camera_x/256.0f),1.6f),512);v->y=us_sf_add(us_sf_mul(us_sf_add(y,-render_camera_y/256.0f),1.6f),288);
            if(transformed<32u){keys_x[transformed]=key_x.u;keys_y[transformed]=key_y.u;
                screen_x[transformed]=v->x;screen_y[transformed]=v->y;++transformed;}}
        for(unsigned i=2;i<strip->count;++i){us_polyline_vertex_t triangle[3]={strip->vertices[i-2],strip->vertices[i-1],strip->vertices[i]};
            if(us_triangle_append(&triangle_batch,triangle,canvas->paint_alpha))return 0;}
    }
    int result=us_triangle_commit(&triangle_batch,current_framebuffer,canvas->source_rgb,triangle_emit,NULL);
    return result==1 ? 0 : result<0 ? -1 : 1;
}
static void boundary_cached_primitive(BoundaryCanvas *canvas,const BoundaryPrimitive *primitive){
    if(primitive->line==2 && primitive->count==2 && primitive->aa && !canvas->conversion.mode){
        boundary_flush_span(canvas);
        int result=boundary_triangle_mesh(canvas,primitive);
        if(result>0)return;
        if(result<0){canvas->failed=render_failed=1;return;}
    }
    boundary_cached_primitive_body(canvas,primitive);
}

static void prepare_geometry_canvas(BoundaryCanvas *canvas) {
    memset(canvas,0,sizeof(*canvas));canvas->bank=current_framebuffer==USURPER_FB_B;
    canvas->owner_scale=1;
    canvas->palette=USURPER_DYNAMIC_PALETTE_BASE+canvas->bank*USURPER_DYNAMIC_PALETTE_ROWS*USURPER_DYNAMIC_PALETTE_TILE_HEIGHT*USURPER_GPU_STRIDE;
    boundary_primitive_count=boundary_vertex_count=0;
}
static void render_geometry_canvas(BoundaryCanvas *canvas) {
    uint32_t sample_start=boundary_now();
    if(canvas->failed){render_failed=1;return;}
    BoundaryPalette *cache=&boundary_palette_cache[canvas->bank];uint8_t selected[USURPER_DYNAMIC_PALETTE_ROWS]={0};unsigned dirty=0;
    for(unsigned row=0;row<USURPER_DYNAMIC_PALETTE_ROWS;++row)selected[row]=cache->valid[row]&&cache->frame[row]==geometry_frame_serial;
    /* Reserve existing colors before choosing replacements, so no tile used
     * by this frame can be overwritten while its commands are in flight. */
    for(unsigned color=0;color<canvas->count;++color) {
        unsigned row=0;while(row<USURPER_DYNAMIC_PALETTE_ROWS&&(!cache->valid[row]||cache->colors[row]!=canvas->colors[color]))++row;
        canvas->color_slots[color]=(uint16_t)row;if(row<USURPER_DYNAMIC_PALETTE_ROWS){selected[row]=1;cache->frame[row]=geometry_frame_serial;}
    }
    for(unsigned color=0;color<canvas->count;++color)if(canvas->color_slots[color]==USURPER_DYNAMIC_PALETTE_ROWS) {
        unsigned row=cache->cursor,visited=0;while(selected[row]&&visited++<USURPER_DYNAMIC_PALETTE_ROWS)row=(row+1u)%USURPER_DYNAMIC_PALETTE_ROWS;
        if(selected[row]){canvas->failed=render_failed=1;return;}
        selected[row]=1;cache->cursor=(row+1u)%USURPER_DYNAMIC_PALETTE_ROWS;cache->valid[row]=1;cache->colors[row]=canvas->colors[color];canvas->color_slots[color]=(uint16_t)row;
        cache->frame[row]=geometry_frame_serial;
        /* Tiles and their even-width rows are word aligned. Upload two equal
         * RGB565 pixels per RV32 store; lifetime/pinning and bytes are unchanged. */
        volatile uint32_t *pixels=(volatile uint32_t *)(uintptr_t)(canvas->palette+row*USURPER_DYNAMIC_PALETTE_TILE_HEIGHT*USURPER_GPU_STRIDE);
        uint32_t pair=(uint32_t)canvas->colors[color]|((uint32_t)canvas->colors[color]<<16);
        for(unsigned x=0;x<USURPER_GPU_WIDTH*USURPER_DYNAMIC_PALETTE_TILE_HEIGHT/2u;++x)pixels[x]=pair;
        dirty=1;
    }
    if(dirty)drain();
    uint32_t sample_mark=boundary_now();boundary_times[1]=sample_mark-sample_start;sample_start=sample_mark;
    for(unsigned i=0;i<boundary_primitive_count;++i) {
        const BoundaryPrimitive *p=&boundary_primitives[i];
        canvas->prepared=1;canvas->alpha=p->alpha;canvas->rgb=p->rgb;canvas->source_rgb=p->rgb;canvas->converted_rgb=p->converted_rgb;canvas->paint_alpha=p->tint.a;
        boundary_cached_primitive(canvas,p);
    }
    if(canvas->failed){render_failed=1;return;}
    sample_mark=boundary_now();boundary_times[2]=sample_mark-sample_start;
}
static void draw_hangar_boss(void) {
    BoundaryCanvas canvas;prepare_geometry_canvas(&canvas);uint32_t start=boundary_now();
    us_boundary_painter_t painter={&canvas,boundary_record_polygon,boundary_record_line};us_hangar_draw(&hangar_boss,&painter);
    boundary_times[0]=boundary_now()-start;render_geometry_canvas(&canvas);boundary_times[3]=0;
    ui_text(scale_design_coord(design_x(144)),scale_design_coord(design_y(323)),us_campaign_stage(6u)->name);
    fill_rect(design_x(144),design_y(338),design_x(350),design_y(9),rgb565(42,24,36));
    fill_rect(design_x(144),design_y(338),(unsigned)(design_x(350)*hangar_boss.hp/hangar_boss.max_hp),design_y(9),rgb565(220,220,220));
}
static void controller_record_line(void *ctx,const us_flame_vertex_t *v,unsigned n,float w,us_boundary_color_t c,unsigned aa){
    boundary_record_mesh_line(ctx,v,n,w,c,aa);
}
static void draw_controller_boss(void){
    BoundaryCanvas canvas;prepare_geometry_canvas(&canvas);uint32_t start=boundary_now();
    us_controller_painter_t painter={&canvas,boundary_record_polygon,controller_record_line};us_controller_draw(&controller_boss,&painter);
    boundary_times[0]=boundary_now()-start;render_geometry_canvas(&canvas);boundary_times[3]=0;
    for(unsigned i=0;i<6;++i)if(controller_bombs[i].active){
        int x,y;world_to_screen((int32_t)(controller_bombs[i].x*Q8),(int32_t)(controller_bombs[i].y*Q8),&x,&y);
        fill_rect(scale_design_coord(x-4),scale_design_coord(y-4),9,9,rgb565(255,145,77));
        world_to_screen((int32_t)(controller_bombs[i].destination_x*Q8),(int32_t)(controller_bombs[i].destination_y*Q8),&x,&y);
        int half=(int)scale_design_extent(64);outline_rect(scale_design_coord(x)-half,scale_design_coord(y)-half,(unsigned)half*2,(unsigned)half*2,rgb565(255,145,77),1);
    }
    ui_text(scale_design_coord(design_x(144)),scale_design_coord(design_y(323)),us_campaign_stage(7)->name);
    fill_rect(design_x(144),design_y(338),design_x(350),design_y(9),rgb565(42,24,36));
    fill_rect(design_x(144),design_y(338),(unsigned)(design_x(350)*controller_boss.hp/controller_boss.max_hp),design_y(9),rgb565(198,229,236));
}
#ifdef USURPER_WARDEN_BAKED
static int warden_baked_coordinate(float position,int32_t camera,unsigned center){
    float value=(position-camera/256.0f)*1.6f+center;return (int)(value+(value<0?-0.5f:0.5f));
}
#endif
static void draw_warden_field(void){
    if(!warden_field.active)return;
#ifdef USURPER_WARDEN_BAKED
    if(us_warden_baked_ready()){
        int x=warden_baked_coordinate(warden_field.x,render_camera_x,512),y=warden_baked_coordinate(warden_field.y,render_camera_y,288);
        int result=us_warden_baked_field(&warden_field,x,y,current_framebuffer,current_emit,current_emit_context);
        if(result!=-2){if(result)render_failed=1;return;}
    }
#endif
    BoundaryCanvas canvas;prepare_geometry_canvas(&canvas);
    us_controller_painter_t painter={&canvas,boundary_record_polygon,controller_record_line};us_warden_draw(&warden_field,&painter);
    render_geometry_canvas(&canvas);
}
static void warden_local_polygon(void *context,const us_flame_vertex_t *local,unsigned count,us_boundary_color_t color){
    BoundaryCanvas *canvas=context;us_flame_vertex_t points[US_RASTER_POLYGON_MAX];if(count>US_RASTER_POLYGON_MAX){canvas->failed=1;return;}
    float cx,sy;us_fx_direction_float(canvas->owner_rotation,&cx,&sy);cx=us_sf_mul(cx,canvas->owner_scale);sy=us_sf_mul(sy,canvas->owner_scale);
    for(unsigned i=0;i<count;++i)points[i]=(us_flame_vertex_t){us_sf_add(us_sf_add(us_sf_mul(cx,local[i].x),us_sf_mul(-sy,local[i].y)),canvas->owner_x),us_sf_add(us_sf_add(us_sf_mul(sy,local[i].x),us_sf_mul(cx,local[i].y)),canvas->owner_y)};
    unsigned mode=canvas->local_lines;canvas->local_lines=0;boundary_record_polygon(context,points,count,color);canvas->local_lines=mode;
}
static void warden_canvas_owner(BoundaryCanvas *canvas,const us_warden_hull_view_t *view){canvas->owner_x=view->x;canvas->owner_y=view->y;canvas->owner_rotation=view->rotation;canvas->owner_scale=view->scale;canvas->local_lines=1;}
static void draw_warden_hull(const us_warden_hull_view_t *view){
#ifdef USURPER_WARDEN_BAKED
    if(us_warden_baked_ready()){
        int x=warden_baked_coordinate(view->x,render_camera_x,512),y=warden_baked_coordinate(view->y,render_camera_y,288);
        us_warden_hull_view_t body=*view;if(view->slow)body.disruption=1;
        int result=us_warden_baked_hull(&body,x,y,current_framebuffer,current_emit,current_emit_context);
        if(result!=-2){
            if(result){render_failed=1;return;}
            /* Only dynamic narrow strokes remain procedural. The large hull,
             * aura fill/border and capture arc/sector never rasterize here. */
            if(view->slow){
                BoundaryCanvas overlay;prepare_geometry_canvas(&overlay);warden_canvas_owner(&overlay,view);overlay.control=view->controlled;
                if(view->material)us_conversion_mask_init(&overlay.conversion,view->x,view->y,view->rotation,view->scale,render_camera_x/256.0f,render_camera_y/256.0f,(float)view->progress,-9,9);
                if(view->supply){overlay.modulated=1;overlay.modulate=(us_boundary_color_t){0.7f,0.85f,1,1};}
                us_controller_painter_t painter={&overlay,warden_local_polygon,controller_record_line};us_warden_slow_draw_local(view,&painter);render_geometry_canvas(&overlay);
                if(us_warden_baked_aura(view,x,y,current_framebuffer,current_emit,current_emit_context))render_failed=1;
            }
            if(view->escort_valid&&view->escort_alive){
                BoundaryCanvas overlay;prepare_geometry_canvas(&overlay);warden_canvas_owner(&overlay,view);
                us_controller_painter_t painter={&overlay,warden_local_polygon,controller_record_line};us_warden_escort_draw_local(view,&painter);render_geometry_canvas(&overlay);
            }
            return;
        }
    }
#endif
    BoundaryCanvas canvas;prepare_geometry_canvas(&canvas);
    warden_canvas_owner(&canvas,view);
    canvas.control=view->controlled;
    if(view->material)us_conversion_mask_init(&canvas.conversion,view->x,view->y,view->rotation,view->scale,render_camera_x/256.0f,render_camera_y/256.0f,(float)view->progress,-9,9);
    if(view->supply){canvas.modulated=1;canvas.modulate=(us_boundary_color_t){0.7f,0.85f,1,1};}
    us_controller_painter_t painter={&canvas,warden_local_polygon,controller_record_line};us_warden_hull_draw_local(view,&painter);render_geometry_canvas(&canvas);
    prepare_geometry_canvas(&canvas);warden_canvas_owner(&canvas,view);painter.context=&canvas;us_warden_aura_draw_local(view,&painter);render_geometry_canvas(&canvas);
}
static void draw_warden_capture(float x,float y,double progress){
    BoundaryCanvas canvas;prepare_geometry_canvas(&canvas);us_controller_painter_t painter={&canvas,boundary_record_polygon,controller_record_line};us_warden_capture_draw(x,y,progress,&painter);render_geometry_canvas(&canvas);
}
static void draw_warden_marker(float x,float y,unsigned front){
#ifdef USURPER_WARDEN_BAKED
    if(us_warden_baked_ready()){
        int sx=warden_baked_coordinate(x,render_camera_x,512),sy=warden_baked_coordinate(y,render_camera_y,288);
        if(us_warden_baked_marker(front,sx,sy,current_framebuffer,current_emit,current_emit_context))render_failed=1;
        return;
    }
#endif
    BoundaryCanvas canvas;prepare_geometry_canvas(&canvas);us_controller_painter_t painter={&canvas,boundary_record_polygon,controller_record_line};us_warden_marker_draw(x,y,front,&painter);render_geometry_canvas(&canvas);
}
static void draw_player_form(void) {
    double cooldown=selected_loadout.form_index==9?player_form.guard_cooldown:form_combat.dash_cooldown;
#ifdef USURPER_WARDEN_BAKED
    if(us_warden_baked_ready()){
        int x=warden_baked_coordinate(render_player_x/256.0f,render_camera_x,512);
        int y=warden_baked_coordinate(render_player_y/256.0f,render_camera_y,288);
        int status=us_warden_baked_form(selected_loadout.form_index,render_player_angle,player_shield>0,
            damage_flash_ticks>0,cooldown,player_form.guard_flash,rammer_charge.form_owned&&rammer_charge.remaining_q16,
            x,y,current_framebuffer,current_emit,current_emit_context);
        if(status!=-2){if(status)render_failed=1;return;}
    }
#endif
    BoundaryCanvas canvas;prepare_geometry_canvas(&canvas);
    canvas.owner_x=render_player_x/256.0f;canvas.owner_y=render_player_y/256.0f;
    canvas.owner_rotation=(float)(render_player_angle*0.017453292519943295);canvas.local_lines=1;
    us_controller_painter_t painter={&canvas,warden_local_polygon,controller_record_line};
    us_form_hull_draw(&painter,selected_loadout.form_index,player_shield>0,damage_flash_ticks>0,cooldown,
        player_form.guard_flash,rammer_charge.form_owned&&rammer_charge.remaining_q16);
    render_geometry_canvas(&canvas);
}
static void draw_interceptors(void){
    for(unsigned i=0;i<WARDEN_THREAT_COUNT;++i){
        const us_interceptor_t *motion=&interceptors[i].motion;if(!motion->active)continue;
        int x,y;world_to_screen((int32_t)(motion->x*Q8),(int32_t)(motion->y*Q8),&x,&y);
        double angle=motion->rotation;if(angle<0)angle+=6.28318530717958647692;
        unsigned frame=((unsigned)(angle*14.32394487827058022+0.5))%90u;
        ts2d_command_t command={.opcode=TS2D_OP_BLIT_ALPHA_PIXEL,.flags=TS2D_FLAG_CLIP_TO_VIEWPORT|TS2D_FLAG_SRC_ARGB4444,
            .src_format=TS2D_FMT_ARGB4444,.global_alpha=255,.src_base_addr=US_INTERCEPTOR_SPRITE_BASE,.dst_base_addr=current_framebuffer,
            .src_x=(int16_t)((frame%10)*40),.src_y=(int16_t)((frame/10)*40),.dst_x=(int16_t)(scale_design_coord(x)-20),.dst_y=(int16_t)(scale_design_coord(y)-20),
            .width=40,.height=40,.src_stride_bytes=US_INTERCEPTOR_SPRITE_WIDTH*2,.dst_stride_bytes=USURPER_GPU_STRIDE};
        (void)emit_raw(&command);
    }
}
static void draw_boundary_boss(void) {
    BoundaryCanvas canvas;prepare_geometry_canvas(&canvas);uint32_t sample_start=boundary_now();
    us_boundary_painter_t painter={&canvas,boundary_record_polygon,boundary_record_line};us_boundary_draw(&boundary_boss,&painter);
    boundary_times[0]=boundary_now()-sample_start;render_geometry_canvas(&canvas);
    if(canvas.failed)return;
    sample_start=boundary_now();uint32_t sample_mark;
    for(unsigned side=0;side<4u;++side) {
        const us_boundary_part_t *arm=&boundary_boss.parts[side*3u];if(arm->captive<0) continue;
        unsigned slot=(unsigned)arm->captive,angle=boundary_unit_facing[slot]/256u,species=boundary_unit_species[slot];
        unsigned type=ship_kind(species,1u),tile=type*USURPER_ASSET_SHIP_VARIANTS+(angle%360u)*USURPER_ASSET_SHIP_VARIANTS/360u;
        usurper_ui_region_t region={(unsigned short)((tile%16u)*64u),(unsigned short)(us_ui_source_ships.y+tile/16u*32u),64,32,0,0};
        if(species==SPECIES_RAMMER) region=us_ui_rammer_hulls_frames[9][(angle%360u)/4u];
        double progress=arm->crush_progress; unsigned w=(unsigned)(region.w*(1+0.18*progress)+0.5),h=(unsigned)(region.h*(1-0.85*progress)+0.5);
        if(!w||!h||w>96u||h>64u) continue;
        uint32_t base=USURPER_CAPTIVE_SCRATCH_BASE+(current_framebuffer==USURPER_FB_B?0x10000u:0)+side*0x3000u;
        volatile uint16_t *target=(volatile uint16_t *)(uintptr_t)base;
        const volatile uint16_t *atlas=(const volatile uint16_t *)(uintptr_t)USURPER_ATLAS_BASE;
        unsigned bright[16];for(unsigned shade=0;shade<16u;++shade)bright[shade]=min_u(15u,(unsigned)(shade*(1+progress)+0.5));
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
            uint16_t pixel=atlas[(region.y+y*region.h/h)*(USURPER_ATLAS_STRIDE/2u)+region.x+x*region.w/w];
            unsigned r=bright[(pixel>>8)&15u],g=bright[(pixel>>4)&15u],b=bright[pixel&15u];
            target[y*w+x]=(uint16_t)((pixel&0xf000u)|(r<<8)|(g<<4)|b);
        }
        drain();int32_t vx=render_camera_x,vy=render_camera_y;
        int x=(int)((arm->x-vx/256.0f)*1.6f+USURPER_GPU_WIDTH*0.5f),y=(int)((arm->y-vy/256.0f)*1.6f+USURPER_GPU_HEIGHT*0.5f);
        ts2d_command_t command={.opcode=TS2D_OP_BLIT_ALPHA_PIXEL,.flags=TS2D_FLAG_CLIP_TO_VIEWPORT|TS2D_FLAG_SRC_ARGB4444,
            .src_format=TS2D_FMT_ARGB4444,.global_alpha=255,.src_base_addr=base,.dst_base_addr=current_framebuffer,
            .dst_x=(int16_t)(x-(int)w/2),.dst_y=(int16_t)(y-(int)h/2),.width=(uint16_t)w,.height=(uint16_t)h,
            .src_stride_bytes=(uint16_t)(w*2),.dst_stride_bytes=USURPER_GPU_STRIDE};(void)emit_raw(&command);
    }
    sample_mark=boundary_now();boundary_times[3]=sample_mark-sample_start;
    ui_text(scale_design_coord(design_x(144)),scale_design_coord(design_y(323)),us_campaign_stage(5u)->name);
    fill_rect(design_x(144),design_y(338),design_x(350),design_y(9),rgb565(42,24,36));
    fill_rect(design_x(144),design_y(338),(unsigned)(design_x(350)*boundary_boss.hp/boundary_boss.max_hp),design_y(9),rgb565(255,98,123));
}

#include "usurper_archon_arc_fast.h"

static void draw_boss(void) {
    if (!boss_active) return;
    if(campaign_stage_id==7u){draw_controller_boss();return;}
    if(campaign_stage_id==6u){draw_hangar_boss();return;}
    if (campaign_stage_id == 5u) { draw_boundary_boss(); return; }
#ifdef USURPER_TWIN_REFERENCE_SPRITES
    if (campaign_stage_id == 4u) { draw_twin_boss(); return; }
#endif
    int32_t draw_boss_x = render_interpolate(boss_prev_x, boss_x);
    int32_t draw_boss_y = render_interpolate(boss_prev_y, boss_y);
#ifdef USURPER_FLAME_REFERENCE_SPRITES
    if (campaign_stage_id == 3u) { draw_flame_boss(draw_boss_x, draw_boss_y); return; }
#endif
    int x, y;
    world_to_screen(draw_boss_x, draw_boss_y, &x, &y);
#ifdef USURPER_ARCHON_REFERENCE_SPRITES
    if (campaign_stage_id == 2u) {
        unsigned variant = archon_boss.flash ? (archon_boss.phase ? 4u : 3u) :
            archon_boss.phase ? 2u : archon_boss.core_exposed ? 1u : 0u;
        const usurper_ui_region_t *body = &us_ui_archon_hulls_frames[variant][boss_angle / 4u];
        ui_blit(body, scale_design_coord(x) + body->ox, scale_design_coord(y) + body->oy);
        if (archon_boss.shield > 0) {
            /* Same 48-segment arcs and start angle as duel_boss_visual.gd.
             * Native line primitives currently approximate alpha/antialiasing. */
            for (unsigned arc = 0; arc < 2u; ++arc) {
                double start = arc ? -1.57079632679489661923 : 0;
                double span = 6.28318530717958647692 * (arc ? archon_boss.shield / archon_boss.shield_max : 1.0);
                static us_arc_offsets_t arc_cache[2];
                const double *offsets=us_arc_offsets(&arc_cache[arc],span);
                int old_x = 0, old_y = 0;
                for (unsigned point = 0; point < 48u; ++point) {
                    float angle=(float)(archon_boss.rotation+start+offsets[point]);
                    /* Each arc keeps all 48 exact angle keys. The general 64-slot
                     * direction cache thrashes across these 96 shield vertices. */
                    static struct {uint32_t key,valid;int32_t x,y;} point_cache[2][48];
                    union {float f;uint32_t u;} angle_key={.f=angle};
                    int32_t dx,dy;
                    if(point_cache[arc][point].valid&&point_cache[arc][point].key==angle_key.u) {
                        dx=point_cache[arc][point].x;dy=point_cache[arc][point].y;
                    } else {
                        if(!us_archon_arc_offset_fast(angle,&dx,&dy)) {
                            float cx,sy;us_fx_direction_float(angle,&cx,&sy);
                            dx=(int32_t)(cx*52*Q8);dy=(int32_t)(sy*52*Q8);
                        }
                        point_cache[arc][point].key=angle_key.u;point_cache[arc][point].valid=1;
                        point_cache[arc][point].x=dx;point_cache[arc][point].y=dy;
                    }
                    int ax, ay;
                    world_to_screen(draw_boss_x+dx,draw_boss_y+dy,&ax,&ay);
                    if (point) line_rect(old_x, old_y, ax, ay, arc ? rgb565(133,170,240) : rgb565(31,45,68), arc ? 3u : 8u);
                    old_x = ax; old_y = ay;
                }
            }
        }
        for (unsigned part = 0; part < US_ARCHON_PARTS; ++part) {
            int32_t tx, ty;
            if (!attack_target_position(ENEMY_MAX + part, &tx, &ty)) continue;
            int px, py; world_to_screen(tx, ty, &px, &py);
            const us_archon_turret_t *turret = &archon_boss.parts[part];
            unsigned white = turret->flash != 0;
            int hull_angle = (int)(turret->rotation * 180.0 / 3.14159265358979323846);
            hull_angle = (hull_angle % 360 + 360) % 360;
            int barrel_angle = (int)(turret->barrel * 180.0 / 3.14159265358979323846);
            barrel_angle = (barrel_angle % 360 + 360) % 360;
            const usurper_ui_region_t *hull = &us_ui_archon_turret_hulls_frames[white][(unsigned)hull_angle / 4u];
            const usurper_ui_region_t *barrel = &us_ui_archon_barrels_frames[white][(unsigned)barrel_angle / 4u];
            ui_blit(hull, scale_design_coord(px) + hull->ox, scale_design_coord(py) + hull->oy);
            ui_blit(barrel, scale_design_coord(px) + barrel->ox, scale_design_coord(py) + barrel->oy);
            int hx, hy; world_to_screen(tx - 13 * Q8, ty - 20 * Q8, &hx, &hy);
            unsigned length = (unsigned)(39.0 * turret->hp / 640.0);
            fill_rect(hx, hy, 39u, 3u, rgb565(38,57,76));
            if (length) fill_rect(hx, hy, length, 3u, white ? rgb565(255,255,255) : rgb565(133,170,240));
        }
        ui_text(scale_design_coord(design_x(144)), scale_design_coord(design_y(323)), "执政官");
        fill_rect(design_x(144), design_y(338), design_x(350), design_y(9), rgb565(30,39,56));
        fill_rect(design_x(144), design_y(338), (unsigned)(design_x(350) * archon_boss.hp / archon_boss.max_hp), design_y(9), rgb565(133,170,240));
        return;
    }
#endif
#ifdef USURPER_MECHANIC_REFERENCE_SPRITES
    if (campaign_stage_id == 1u) {
        unsigned frame = boss_angle / 4u, exposed = mechanic_boss.core_exposed;
        const usurper_ui_region_t *body = &us_ui_mechanic_hulls_frames[exposed][frame];
        ui_blit(body, scale_design_coord(x) + body->ox, scale_design_coord(y) + body->oy);
        for (unsigned part = mechanic_boss.phase * 3u; part < mechanic_boss.phase * 3u + 3u; ++part) {
            if (!mechanic_boss.part_hp_q16[part]) continue;
            const usurper_ui_region_t *line = &us_ui_mechanic_connectors_frames[exposed * 3u + part % 3u][frame];
            ui_blit(line, scale_design_coord(x) + line->ox, scale_design_coord(y) + line->oy);
            int32_t tx, ty;
            if (!attack_target_position(ENEMY_MAX + part, &tx, &ty)) continue;
            int px, py; world_to_screen(tx, ty, &px, &py);
            unsigned variant = mechanic_boss.part_flash[part] ? 2u : us_mechanic_target_open(&mechanic_boss, part) ? 1u : 0u;
            const usurper_ui_region_t *lock = &us_ui_mechanic_locks_frames[variant][frame];
            ui_blit(lock, scale_design_coord(px) + lock->ox, scale_design_coord(py) + lock->oy);
            int dx, dy, cx, cy;
            direction_q8(boss_angle, &dx, &dy);
            cx = px + dy * 14 / Q8; cy = py - dx * 14 / Q8;
            line_rect(cx - dx * 9 / Q8, cy - dy * 9 / Q8,
                      cx + dx * 9 / Q8, cy + dy * 9 / Q8, rgb565(53,61,68), 2u);
            unsigned length = (unsigned)((uint64_t)18u * mechanic_boss.part_hp_q16[part] / (90u * US_FX_ONE));
            line_rect(cx - dx * 9 / Q8, cy - dy * 9 / Q8,
                      cx + dx * ((int)length - 9) / Q8, cy + dy * ((int)length - 9) / Q8,
                      variant ? rgb565(255,224,120) : rgb565(120,120,128), 2u);
        }
        if (mechanic_boss.attack_clock < 0.65) {
            int ex, ey, sx, sy;
            world_to_screen(boss_x + mechanic_boss.locked_x * 394 / 256, boss_y + mechanic_boss.locked_y * 394 / 256, &ex, &ey);
            world_to_screen(boss_x + mechanic_boss.locked_x * 30 / 256, boss_y + mechanic_boss.locked_y * 30 / 256, &sx, &sy);
            line_rect(sx, sy, ex, ey, rgb565(136,68,58), 2u);
        }
        ui_text(scale_design_coord(design_x(144)), scale_design_coord(design_y(323)), "破阵者");
        fill_rect(design_x(144), design_y(338), design_x(350), design_y(9), rgb565(40,31,30));
        fill_rect(design_x(144), design_y(338), (unsigned)((uint64_t)design_x(350) * mechanic_boss.hp_q16 / mechanic_boss.max_hp_q16),
                  design_y(9), rgb565(237,118,93));
        return;
    }
#endif
    unsigned tile = boss_angle * USURPER_ASSET_BOSS_VARIANTS / 360u;
    unsigned sx = (tile % USURPER_ASSET_BOSS_COLUMNS) * USURPER_ASSET_BOSS_SIZE;
    unsigned sy = USURPER_ASSET_BOSS_Y + (tile / USURPER_ASSET_BOSS_COLUMNS) * USURPER_ASSET_BOSS_SIZE;
    blit((int)sx, (int)sy, USURPER_ASSET_BOSS_SIZE, USURPER_ASSET_BOSS_SIZE,
         x - (int)USURPER_ASSET_BOSS_SIZE / 2, y - (int)USURPER_ASSET_BOSS_SIZE / 2,
         USURPER_ASSET_BOSS_SIZE, USURPER_ASSET_BOSS_SIZE);
    static const int8_t offset_degrees[7] = {-58, -39, -19, 0, 19, 39, 58};
    for (unsigned row = 0; row < 3u; ++row) {
        for (unsigned col = 0; col < 7u; ++col) {
            if (!armor_alive[row][col]) continue;
            unsigned angle = angle_offset(boss_angle, offset_degrees[col]);
            int dx, dy;
            direction_q8(angle, &dx, &dy);
            int radius = 58 - (int)row * 14;
            int px, py;
            world_to_screen(draw_boss_x + dx * radius, draw_boss_y + dy * radius, &px, &py);
            unsigned tile_index = row * USURPER_ASSET_PLATE_VARIANTS +
                angle * USURPER_ASSET_PLATE_VARIANTS / 360u;
            unsigned tx = (tile_index % USURPER_ASSET_PLATE_COLUMNS) * USURPER_ASSET_PLATE_SIZE;
            unsigned ty = USURPER_ASSET_PLATE_Y + (tile_index / USURPER_ASSET_PLATE_COLUMNS) * USURPER_ASSET_PLATE_SIZE;
            blit((int)tx, (int)ty, USURPER_ASSET_PLATE_SIZE, USURPER_ASSET_PLATE_SIZE,
                 px - (int)USURPER_ASSET_PLATE_SIZE / 2, py - (int)USURPER_ASSET_PLATE_SIZE / 2,
                 USURPER_ASSET_PLATE_SIZE, USURPER_ASSET_PLATE_SIZE);
        }
    }
    int bar_width = 120;
    fill_rect(x - bar_width / 2, y - 62, bar_width, 5u, rgb565(40, 31, 30));
    fill_rect(x - bar_width / 2, y - 62, bar_width * boss_hp / BOSS_BASE_HP, 5u, rgb565(237, 171, 72));
}

static void draw_battle(void) {
    draw_background();
    draw_warden_field();
    int px, py;
    world_to_screen(render_player_x, render_player_y, &px, &py);
    /* The reference only shows the capture reach while a live target is
     * selectable or a release/sweep is active. Keeping it out of the menu
     * demo also preserves the original unobstructed composition. */
    if (!defeat_pending && skimmer_active_sweeps()) {
        for (unsigned i = 0; i <= SKIMMER_SWEEP_MAX; ++i) {
            if (!skimmer_sweeps[i].ticks_left) continue;
            unsigned duplicate = 0u;
            for (unsigned j = 0; j < i; ++j)
                if (skimmer_sweeps[j].ticks_left && skimmer_sweeps[j].radius == skimmer_sweeps[i].radius) duplicate = 1u;
            if (!duplicate) draw_range_ring(px, py, skimmer_sweeps[i].radius, 122u);
        }
    }
    else if (!defeat_pending && phase != USURPER_PHASE_MENU)
        draw_range_ring(px, py, tech_capture_radius(), 255u);
    for (unsigned i = 0; i < ENEMY_MAX; ++i) {
        Enemy *enemy = &enemies[i];
        if (!enemy->active) continue;
        int x, y;
        world_to_screen(render_interpolate(enemy->prev_x, enemy->x),
                        render_interpolate(enemy->prev_y, enemy->y), &x, &y);
        int margin=enemy->species==SPECIES_WARDEN&&!enemy->supply?(int)(80*warden_status[i].scale+2):40;
        if (x < -margin || y < -margin-8 || x > (int)USURPER_DESIGN_WIDTH + margin ||
            y > (int)USURPER_DESIGN_HEIGHT + margin+8) continue;
        unsigned angle = enemy_render_draw_angle(i);
        if(enemy->species==SPECIES_WARDEN){
            const us_warden_ai_t *motion=&warden_motion[i];us_warden_hull_view_t view={.x=render_interpolate(enemy->prev_x,enemy->x)/256.0f,.y=render_interpolate(enemy->prev_y,enemy->y)/256.0f,
                .scale=warden_status[i].scale,.radius=80,.alive=1,.supply=enemy->supply,.flash=warden_status[i].flash>0,.status_time=warden_status[i].time,.material=enemy->capture_q8!=0,.progress=enemy->capture_q8/256.0,
                .disruption=motion->disruption,.controlled={0.16f,0.7f,1,1}};
            if(motion->escort_valid&&motion->escort_id<ENEMY_MAX&&enemy_generation[motion->escort_id]==motion->escort_generation){
                const Enemy *escort=&enemies[motion->escort_id];view.escort_valid=1;view.escort_alive=escort->active;view.escort_x=render_interpolate(escort->prev_x,escort->x)/256.0f;view.escort_y=render_interpolate(escort->prev_y,escort->y)/256.0f;}
            draw_warden_hull(&view);
        }else{
        draw_ship_reflection(enemy->species, 0u, angle, x, y);
        draw_ship_glow(x, y, 0u, angle);
        if (enemy->species == SPECIES_GUNNER) draw_gunner_capture(enemy->capture_q8, angle, x, y);
#ifdef USURPER_RAMMER_REFERENCE_SPRITES
        else if (enemy->species == SPECIES_RAMMER && enemy->capture_q8) {
            unsigned level = 1u + min_u(7u, enemy->capture_q8 * 8u / Q8);
            const usurper_ui_region_t *region = &us_ui_rammer_hulls_frames[level][angle / 4u];
            ui_blit(region, scale_design_coord(x) + region->ox, scale_design_coord(y) + region->oy);
        }
#endif
        else draw_ship(enemy->species, 0u, angle, x, y);
        }
        if(enemy->supply){
            int wx,wy;world_to_screen(enemy->x,enemy->y,&wx,&wy);uint16_t color=rgb565(150,217,255);
            for(int side=-1;side<=1;side+=2){
                int edge=wx+side*24,inner=wx+side*18;
                line_rect_fast(inner,wy-12,edge,wy-12,color,1);line_rect_fast(edge,wy-12,edge,wy+12,color,1);line_rect_fast(edge,wy+12,inner,wy+12,color,1);
            }
            if(enemy->supply_marker){
                static const char *const labels[13]={"支援 · 尖兵","支援 · 漫游者","支援 · 突击者","支援 · 炮击者","支援 · 护卫者","支援 · 分裂者","支援 · 轰炸者","支援 · 汲能者","支援 · 长枪兵","支援 · 追踪者","支援 · 虚空猎手","支援 · 航母","支援 · 碎片"};
                if(enemy->species>0&&enemy->species<=13){const char *label=labels[enemy->species-1];supply_text(scale_design_coord(wx)-(int)supply_text_width(label)/2,scale_design_coord(wy-40),label);}
            }
        }else if(enemy->species!=SPECIES_WARDEN){
            unsigned width = enemy->species == SPECIES_GUNNER ? 34u : 27u;
            fill_rect(x - (int)width / 2, y - 25, width, 3u, rgb565(49, 27, 34));
            fill_rect(x - (int)width / 2, y - 25, width * enemy->hp / max_u(1u, enemy->max_hp), 3u, rgb565(240, 58, 63));
        }
        if (enemy->capture_q8){
            if(enemy->species==SPECIES_WARDEN)draw_warden_capture(render_interpolate(enemy->prev_x,enemy->x)/256.0f,render_interpolate(enemy->prev_y,enemy->y)/256.0f,enemy->capture_q8/256.0);
            else draw_capture_bar(x - 13, y + 20, enemy->capture_q8, 26u);
        }
        if (enemy->flash&&enemy->species!=SPECIES_WARDEN) {
            unsigned flash_radius = enemy->elite ? 18u : 12u;
            small_ring(x, y, flash_radius, rgb565(255, 224, 120));
        }
        if (i == manual_target_index){
            if(enemy->species==SPECIES_WARDEN)draw_warden_marker(render_interpolate(enemy->prev_x,enemy->x)/256.0f,render_interpolate(enemy->prev_y,enemy->y)/256.0f,0);
            else small_ring(x, y, 14u, rgb565(255, 255, 255));
        }
    }
    draw_death_fx();
    if (candidate_index < ENEMY_MAX && enemies[candidate_index].active) {
        int x, y;
        world_to_screen(render_interpolate(enemies[candidate_index].prev_x, enemies[candidate_index].x),
                        render_interpolate(enemies[candidate_index].prev_y, enemies[candidate_index].y), &x, &y);
        uint16_t beam = enemies[candidate_index].capture_q8 ? rgb565(255, 224, 102) :
            (aim_is_held ? rgb565(214, 227, 230) : rgb565(59, 185, 214));
        line_rect_fast(px, py, x, y, beam, aim_is_held ? 3u : 2u);
        if (enemies[candidate_index].capture_q8)
            line_rect_fast(px, py, x, y, rgb565(226, 179, 72), 1u);
    }
    for (unsigned i = 0; i < unit_count; ++i) {
        int x, y;
        int32_t world_x, world_y;
        unit_world_position(i, &world_x, &world_y);
        world_to_screen(render_interpolate(unit_prev_x[i], world_x),
                        render_interpolate(unit_prev_y[i], world_y), &x, &y);
        unsigned facing = unit_species[i] == SPECIES_GUNNER ?
            render_interpolate_angle(unit_prev_facing_q8[i] / Q8, unit_facing_q8[i] / Q8) : render_player_angle;
        if(unit_species[i]==SPECIES_WARDEN){
            us_warden_hull_view_t view={.x=render_interpolate(unit_prev_x[i],world_x)/256.0f,.y=render_interpolate(unit_prev_y[i],world_y)/256.0f,
                .rotation=(float)(facing*0.01745329251994329577),.scale=unit_hull_scale[i]>0?unit_hull_scale[i]:1,.radius=80,.alive=0,.material=1,.progress=1,.disruption=1,.controlled={0.16f,0.7f,1,1}};
            draw_warden_hull(&view);
        }else{
            draw_ship_reflection(unit_species[i], 1u, facing, x, y);
            draw_ship_glow(x, y, 1u, facing);
            draw_ship(unit_species[i], 1u, facing, x, y);
        }
        if (!i){
            if(unit_species[i]==SPECIES_WARDEN)draw_warden_marker(render_interpolate(unit_prev_x[i],world_x)/256.0f,render_interpolate(unit_prev_y[i],world_y)/256.0f,1);
            else small_ring(x, y, 13u, rgb565(255, 224, 120));
        }
    }
    for (unsigned i = 0; i <= SKIMMER_SWEEP_MAX; ++i) {
        const SkimmerSweep *scan = &skimmer_sweeps[i];
        if (!scan->ticks_left) continue;
        draw_skimmer_scan(scan, scan->angle_q16 / 65536u, 1, px, py);
        if (scan->echo) {
            unsigned angle = (2u * scan->start_q16 + 360u * 65536u - scan->angle_q16) % (360u * 65536u);
            draw_skimmer_scan(scan, angle / 65536u, -1, px, py);
        }
    }
    for (unsigned i = 0; i < PROJECTILE_MAX; ++i) {
        if (!projectiles[i].active) continue;
        int x, y;
        int32_t draw_x = render_interpolate(projectiles[i].prev_x, projectiles[i].x);
        int32_t draw_y = render_interpolate(projectiles[i].prev_y, projectiles[i].y);
        world_to_screen(draw_x, draw_y, &x, &y);
        if (projectiles[i].kind == SHOT_GUNNER) {
            draw_gunner_projectile(projectiles[i].burst, angle_to(projectiles[i].vx, projectiles[i].vy), x, y);
            continue;
        }
        uint16_t color = projectiles[i].kind == SHOT_ENEMY ? rgb565(255, 145, 77) : rgb565(89, 170, 255);
        if (projectiles[i].kind != SHOT_ENEMY) {
            int tx, ty;
            world_to_screen(draw_x - projectiles[i].vx / 8,
                            draw_y - projectiles[i].vy / 8, &tx, &ty);
            line_rect(tx, ty, x, y, rgb565(46, 116, 185), 2u);
        }
        if (!(projectiles[i].kind == SHOT_DART && projectiles[i].hull)) {
            fill_rect(x - 2, y - 2, projectiles[i].kind == SHOT_DART ? 7u : 4u, 4u, color);
        }
    }
    draw_interceptors();
    if (release_flash_ticks) {
        small_ring(px, py, 10u + release_flash_ticks, rgb565(255, 224, 120));
        line_rect(px, py,
                  px + (int)usurper_sin_q8[(player_angle + 90u) % 360u] * 24 / 256,
                  py + (int)usurper_sin_q8[player_angle % 360u] * 24 / 256,
                  rgb565(255, 255, 255), 2u);
    }
    for (unsigned i = 0; i < PICKUP_MAX; ++i) {
        if (!pickups[i].active) continue;
        int x, y;
        world_to_screen(render_interpolate(pickups[i].prev_x, pickups[i].x),
                        render_interpolate(pickups[i].prev_y, pickups[i].y), &x, &y);
        fill_rect(x - 3, y - 3, 6u, 6u, rgb565(88, 224, 162));
    }
    draw_boss();
    if (damage_flash_ticks && ((frame_tick >> 1) & 1u))
        outline_rect(4, 4, USURPER_DESIGN_WIDTH - 8u, USURPER_DESIGN_HEIGHT - 8u,
                     rgb565(240, 58, 63), 3u);
    if (defeat_pending && defeat_tick > DEFEAT_HOLD_TICKS) {
        int tx, ty;
        world_to_screen(previous_player_x, previous_player_y, &tx, &ty);
        line_rect(tx, ty, px, py, rgb565(57, 189, 255), 3u);
        line_rect(tx, ty, px, py, rgb565(184, 239, 255), 1u);
    }
    draw_gunner_reticles();
    draw_ship_reflection(SPECIES_NONE, 1u, render_player_angle, px, py);
    draw_ship_glow(px, py, 1u, render_player_angle);
    if(phase!=USURPER_PHASE_MENU&&selected_loadout.form_index>=0)draw_player_form();
    else draw_ship(SPECIES_NONE, 0u, render_player_angle, px, py);
    if (rammer_charge.remaining_q16) {
        int dx, dy;
        direction_q8(rammer_charge.angle, &dx, &dy);
        line_rect(px - dx * 44 / Q8, py - dy * 44 / Q8, px, py, rgb565(57, 189, 255), 3u);
        if (tech_effect("A4")) {
            int reach = (int)min_u(39u, 25u + (unsigned)((uint64_t)14u * rammer_charge.age_q16 * 5u / (18u * 65536u)));
            line_rect(px - dy * reach / Q8, py + dx * reach / Q8,
                      px + dy * reach / Q8, py - dx * reach / Q8, rgb565(165, 227, 255), 2u);
        }
        if(!rammer_charge.form_owned)draw_ship(SPECIES_RAMMER, 1u, rammer_charge.angle, px, py);
    }
    /* The original release hull sits above the player ship (projectile z=3).
     * Draw the launched Dart hull after the player so its launch is visible
     * on the first frame instead of being covered at the shared origin. */
    for (unsigned i = 0; i < PROJECTILE_MAX; ++i) {
        if (!projectiles[i].active || projectiles[i].kind != SHOT_DART || !projectiles[i].hull) continue;
        int x, y;
        world_to_screen(projectiles[i].x, projectiles[i].y, &x, &y);
        unsigned heading = angle_to(projectiles[i].vx, projectiles[i].vy);
        draw_ship(SPECIES_DART, 1u, heading, x, y);
        draw_ship_glow(x, y, 1u, heading);
    }
    if (capture_flash_ticks) {
        int x, y;
        world_to_screen(capture_flash_x, capture_flash_y, &x, &y);
        unsigned radius = 8u + (18u - capture_flash_ticks) * 3u;
        small_ring(x, y, radius, rgb565(57, 189, 255));
        if (capture_flash_ticks > 9u)
            small_ring(x, y, radius > 6u ? radius - 6u : 3u, rgb565(184, 239, 255));
    }
    if (defeat_pending && defeat_tick <= DEFEAT_HOLD_TICKS && ((defeat_tick >> 1) & 1u))
        outline_rect(4, 4, USURPER_DESIGN_WIDTH - 8u, USURPER_DESIGN_HEIGHT - 8u,
                     rgb565(240, 58, 63), 4u);
    if (boss_warning_ticks && !boss_spawned) {
        fill_rect(420, 18, 120u, 4u, rgb565(255, 145, 77));
        fill_rect(420, 18, 120u * boss_warning_ticks / 72u, 4u, rgb565(255, 224, 102));
    }
    if (phase != USURPER_PHASE_MENU) draw_hud_reference();
}

static void draw_tutorial(void) {
    draw_battle();
    if (tutorial_complete_delay && tutorial_step < 7u)
        ui_layer(&us_ui_tutorial_done_frames[tutorial_step]);
    else ui_layer(&us_ui_tutorial_frames[min_u(tutorial_step, 8u)]);
}

static void draw_menu(void) {
    draw_battle();
    /* Three centered actions over the live game background. Crop only the
     * retained labels from the atlas; no promotion or removed menu items. */
    ui_layer(&us_ui_menu_title);
    for (unsigned i = 0; i < MENU_ITEM_COUNT; ++i) {
        usurper_ui_region_t button = {(unsigned short)(22u + i * 192u),
                                     7296, 182, 42, 0, 0};
        int left = MENU_LEFT + (int)i * MENU_STEP;
        ui_blit(&button, scale_design_coord(left), scale_design_coord(MENU_TOP));
        if (i == menu_selection && !menu_confirm)
            outline_rect(left, MENU_TOP, MENU_WIDTH, MENU_HEIGHT,
                         rgb565(140, 193, 220), 1u);
    }
    if (menu_confirm) ui_layer(&us_ui_confirm_overlay);
}

static void draw_result(void) {
    draw_background();
    fill_rect(198, 138, 564, 264, rgb565(8, 12, 16));
    outline_rect(198, 138, 564, 264, result_victory ? rgb565(255, 224, 120) : rgb565(237, 89, 103), 3u);
    unsigned card = result_victory ? 1u : 0u;
    blit(0, (int)(USURPER_RESULT_CARD_Y + card * USURPER_RESULT_CARD_HEIGHT),
         USURPER_RESULT_CARD_WIDTH, USURPER_RESULT_CARD_HEIGHT,
         281, 156, USURPER_RESULT_CARD_WIDTH, USURPER_RESULT_CARD_HEIGHT);
    /* Exported cards contain static labels only. Glyph coordinates are pixels,
     * while the card origin is a scaled design coordinate. Keep three 100-pixel
     * columns separated even for the full ten-digit unsigned counter range.
     * last_run_alloy is sortie loot; the wallet separately includes first-clear
     * rewards, whose per-run amount is not retained by the settlement code. */
    const unsigned totals[3] = {captured_total, kills_total, last_run_alloy};
    for (unsigned i = 0; i < 3u; ++i) {
        unsigned digits = 1u, value = totals[i];
        while (value >= 10u) { value /= 10u; ++digits; }
        ui_number(scale_design_coord(281) + 100 + (int)i * 112 - (int)digits * 5,
                  scale_design_coord(156) + 138, totals[i]);
    }
    for (unsigned i = 0; i < 3u; ++i)
        fill_rect(RESULT_BUTTON_X + (int)i * RESULT_BUTTON_STEP,
                  RESULT_BUTTON_Y + (int)RESULT_BUTTON_HEIGHT - 4, RESULT_BUTTON_WIDTH, 4u,
                  i == 0u ? rgb565(88, 224, 162) : i == 1u ? rgb565(59, 185, 214) : rgb565(240, 58, 63));
    outline_rect(RESULT_BUTTON_X + (int)result_selection * RESULT_BUTTON_STEP, RESULT_BUTTON_Y,
                 RESULT_BUTTON_WIDTH, RESULT_BUTTON_HEIGHT, rgb565(255, 224, 120), 2u);
}

static void pointer_rect(int x, int y, unsigned width, unsigned height, uint16_t color) {
    ts2d_command_t command = {.opcode = TS2D_OP_FILL_RECT,
        .flags = TS2D_FLAG_CLIP_TO_VIEWPORT, .dst_base_addr = current_framebuffer,
        .dst_x = (int16_t)x, .dst_y = (int16_t)y, .width = (uint16_t)width,
        .height = (uint16_t)height, .dst_stride_bytes = USURPER_GPU_STRIDE, .parameter = color};
    (void)emit_raw(&command);
}

static void draw_pointer(void) {
    if (!pointer_visible) return;
    const usurper_ui_region_t *pointer = &us_ui_pointer_normal;
    if (controls_held & 16u) pointer = &us_ui_pointer_left;
    else if (controls_held & 32u) pointer = &us_ui_pointer_right;
    else if (phase == USURPER_PHASE_TECH && tech_tooltip_visible && tech_can_buy(selected_node)) pointer = &us_ui_pointer_left;
    ui_blit(pointer, pointer_x, pointer_y);
    if (pointer_click_ticks) {
        uint16_t edge = (controls_held & 16u) ? rgb565(255, 224, 120) : rgb565(119, 219, 255);
        int radius = 8 + (int)(12u - pointer_click_ticks);
        pointer_rect(pointer_x - radius, pointer_y - 1, 4u, 2u, edge);
        pointer_rect(pointer_x + radius - 3, pointer_y - 1, 4u, 2u, edge);
        pointer_rect(pointer_x - 1, pointer_y - radius, 2u, 4u, edge);
        pointer_rect(pointer_x - 1, pointer_y + radius - 3, 2u, 4u, edge);
        return;
    }
    return;
    /* Legacy procedural cursor retained below for reference only. */
    static const char *const rows[] = {
        "WK", "KWWK", "KWCWK", "KWCCWK", "KWCCCWK", "KWCCCCWK",
        "KWCCCCCWK", "KWCCCCCCWK", "KWCCCCCCCWK", "KWCCCCCCCCWK",
        "KWCCCCCCCCCWK", "KWCCCCCCCCCCWK", "KWCCCCWWWWWWWWK",
        "KWCCCWK.KKKKKKK", "KWCCWCK", "KWCWKCCK", "KWWK.KCCK",
        "KWK...KCCK", "KK.....KCCK", "........KCCK", ".........KWWK", "..........KK"
    };
    uint16_t edge = rgb565(57, 189, 255);
    if (controls_held & 16u) edge = rgb565(255, 224, 120);
    else if (controls_held & 32u) edge = rgb565(119, 219, 255);
    else if (phase == USURPER_PHASE_TECH && tech_tooltip_visible && tech_can_buy(selected_node))
        edge = rgb565(104, 223, 136);
    /* Reflect around the hotspot at screen edges so the whole pointer remains visible. */
    int flip_x = pointer_x > (int)USURPER_GPU_WIDTH - 24;
    int flip_y = pointer_y > (int)USURPER_GPU_HEIGHT - 24;
    for (unsigned row = 0; row < sizeof(rows) / sizeof(rows[0]); ++row) {
        unsigned column = 0;
        while (rows[row][column]) {
            char pixel = rows[row][column];
            unsigned end = column + 1u;
            while (rows[row][end] == pixel) ++end;
            if (pixel != '.') {
                uint16_t color = pixel == 'K' ? rgb565(3, 8, 12) :
                    pixel == 'W' ? rgb565(236, 250, 255) : edge;
                int x = pointer_x + (flip_x ? -(int)end + 1 : (int)column);
                int y = pointer_y + (flip_y ? -(int)row : (int)row);
                pointer_rect(x, y, end - column, 1u, color);
            }
            column = end;
        }
    }
    if (pointer_click_ticks) {
        int radius = 8 + (int)(12u - pointer_click_ticks);
        pointer_rect(pointer_x - radius, pointer_y - 1, 4u, 2u, edge);
        pointer_rect(pointer_x + radius - 3, pointer_y - 1, 4u, 2u, edge);
        pointer_rect(pointer_x - 1, pointer_y - radius, 2u, 4u, edge);
        pointer_rect(pointer_x - 1, pointer_y + radius - 3, 2u, 4u, edge);
    }
    pointer_rect(pointer_x, pointer_y, 1u, 1u, rgb565(236, 250, 255));
}

static void draw_settings(void) {
    fill_rect(0, 0, USURPER_DESIGN_WIDTH, USURPER_DESIGN_HEIGHT, rgb565(8, 11, 16));
    outline_rect(154, 38, 652, 496, rgb565(92, 103, 113), 2);
    ui_text(scale_design_coord(190), scale_design_coord(68), "设置");
    ui_text_alpha(scale_design_coord(190), scale_design_coord(98), "W/S SELECT   A/D CHANGE   ESC BACK", 180u);
    static const char *const labels[] = {"控制方式", "音乐", "音效", "屏幕震动", "试听", "恢复默认", "返回"};
    for (unsigned i = 0; i < 7u; ++i) {
        int x=190,y=132+(int)i*50;unsigned width=576u;
        if(i==6u){x=SETTINGS_BACK_X;y=SETTINGS_BACK_Y;width=SETTINGS_BACK_W;}
        uint16_t edge = i == settings_selection ? rgb565(255, 224, 120) : rgb565(57, 65, 72);
        fill_rect(x, y, width, 42u, rgb565(10, 17, 24));
        outline_rect(x, y, width, 42u, edge, 2);
        ui_text(scale_design_coord(x+18),scale_design_coord(y+10),labels[i]);
        if(i==0u){static const char *const modes[]={"鼠标","移动","IJKL 键盘"};ui_text(scale_design_coord(570),scale_design_coord(y+10),modes[scene_settings.control_mode]);}
        else if(i==1u||i==2u)ui_number(scale_design_coord(690),scale_design_coord(y+10),i==1u?scene_settings.music_volume:scene_settings.sfx_volume);
        else if(i==3u)ui_text(scale_design_coord(690),scale_design_coord(y+10),(scene_settings.flags&US_SETTINGS_SHAKE)?"开启":"关闭");
    }
#if defined(USURPER_AUDIO_PC)
    ui_text_alpha(scale_design_coord(190),scale_design_coord(475),"PC 音效",180u);
#elif defined(USURPER_AUDIO_ENABLED)
    ui_text_alpha(scale_design_coord(190),scale_design_coord(475),usurper_audio_stats()->available?"HDMI 音效可用":"HDMI 音效不可用",180u);
#else
    ui_text_alpha(scale_design_coord(190),scale_design_coord(475),"HDMI 音效不可用",180u);
#endif
}

static void draw_loadout_panel(void){
    static const char *const labels[]={"形态","起始舰体","第一支援","第二支援","返回"};
    static const char *const names[]={"无","尖兵","漫游者","突击者","炮手","护卫者"};
    fill_rect(166,70,628,414,rgb565(8,15,21));outline_rect(166,70,628,414,rgb565(140,193,220),2);
    ui_text(scale_design_coord(204),scale_design_coord(88),"编队配置");
    ui_text_alpha(scale_design_coord(204),scale_design_coord(119),"W/S SELECT   A/D CHANGE   L/ESC BACK",180);
    for(unsigned i=0;i<5u;++i){
        int y=150+(int)i*58;unsigned locked=i>=2u&&i<4u&&i-2u>=us_loadout_support_slots(completed_stage_mask);
        fill_rect(204,y,552,46,rgb565(12,22,30));outline_rect(204,y,552,46,i==loadout_selection?rgb565(255,224,120):rgb565(57,65,72),2);
        ui_text(scale_design_coord(222),scale_design_coord(y+9),labels[i]);
        if(i<4u){
            unsigned species=i==0u?us_loadout_form_species(selected_loadout.form_index):i==1u?selected_loadout.starting_species:selected_loadout.support_species[i-2u];
            const char *name=locked?"未解锁":i==0u&&!species?"基础":names[species<=5u?species:0u];
            ui_text_alpha(scale_design_coord(522),scale_design_coord(y+9),name,locked?112u:255u);
        }
    }
    ui_text_alpha(scale_design_coord(204),scale_design_coord(452),"通关后选择   尖兵 漫游者 突击者 炮手 护卫者",180u);
}

static int render_scene(uint32_t framebuffer, usurper_emit_fn emit, void *context) {
    if(!++geometry_frame_serial){geometry_frame_serial=1;for(unsigned bank=0;bank<2;++bank)memset(boundary_palette_cache[bank].frame,0,sizeof(boundary_palette_cache[bank].frame));}
    current_emit = emit;
    current_emit_context = context;
    current_framebuffer = framebuffer;
    if(us_triangle_frame(&triangle_batch,US_TRIANGLE_BANK_BASE+(framebuffer==USURPER_FB_B?US_TRIANGLE_BANK_BYTES:0),US_TRIANGLE_RECORDS))return -1;
    render_failed = 0;
    graph_clip = 0;
    if (phase == USURPER_PHASE_MENU) draw_menu();
    else if (phase == USURPER_PHASE_TECH) draw_tech_screen();
    else if (phase == USURPER_PHASE_TUTORIAL) draw_tutorial();
    else if (phase == USURPER_PHASE_ARRIVAL) draw_battle();
    else if (phase == USURPER_PHASE_RESULT) draw_result();
    else if (phase == USURPER_PHASE_PAUSED) { draw_battle(); ui_layer(&us_ui_pause); }
    else if (phase == USURPER_PHASE_SETTINGS) draw_settings();
    else if (phase == USURPER_PHASE_CREDITS) { fill_rect(0, 0, 960, 540, 0); ui_layer(&us_ui_credits); }
    else draw_battle();
    if(phase==USURPER_PHASE_MENU||phase==USURPER_PHASE_TECH){
        if(loadout_panel)draw_loadout_panel();
        else{
            int bx=phase==USURPER_PHASE_TECH?390:774,by=phase==USURPER_PHASE_TECH?14:34;
            fill_rect(bx,by,165,34,rgb565(12,22,30));outline_rect(bx,by,165,34,rgb565(140,193,220),1);
            ui_text(scale_design_coord(bx+11),scale_design_coord(by+5),"L 编队配置");
            if(phase==USURPER_PHASE_TECH)for(unsigned stage=0;stage<min_u(3u,exhibition_stage_count());++stage) {
                int x=374+(int)stage*44;
                uint16_t color=stage==campaign_stage_id?rgb565(255,224,120):rgb565(98,120,136);
                fill_rect(design_x(x),design_y(10),design_x(40),design_y(24),rgb565(10,23,31));
                outline_rect(design_x(x),design_y(10),design_x(40),design_y(24),color,stage==campaign_stage_id?2u:1u);
                char number[3]={'0',(char)('1'+stage),0};
                ui_text(scale_design_coord(design_x(x+13)),scale_design_coord(design_y(15)),number);
            }
        }
    }
    draw_pointer();
    ts2d_command_t end = {.opcode = TS2D_OP_FRAME_END};
    (void)emit_raw(&end);
    return render_failed ? -1 : 0;
}

int usurper_scene_build(uint32_t framebuffer, usurper_emit_fn emit, void *context) {
    if (!assets_loaded || !emit) return -1;
    return render_scene(framebuffer, emit, context);
}

void usurper_scene_step(void) {
    unsigned previous_boss_phase=boss_phase,previous_armor=boss_armor_left;
    if(settings_shake_ticks)--settings_shake_ticks;
    step_scene();
    usurper_audio_tick(phase);
    if(phase==USURPER_PHASE_BATTLE && boss_active){
        if(previous_boss_phase && boss_phase!=previous_boss_phase)usurper_audio_event(US_AUDIO_BOSS_PHASE);
        if(boss_armor_left<previous_armor)usurper_audio_event(US_AUDIO_ARMOR);
    }
}
const usurper_settings_t *usurper_scene_settings(void){return &scene_settings;}
int usurper_scene_settings_apply(const usurper_settings_t *value){
    if(!us_settings_valid(value))return US_PROFILE_RANGE;
    if(phase!=USURPER_PHASE_MENU&&phase!=USURPER_PHASE_TECH&&phase!=USURPER_PHASE_SETTINGS)return US_PROFILE_BUSY;
    scene_settings=*value;usurper_audio_configure(value);return US_PROFILE_OK;
}
#ifdef USURPER_DIAGNOSTIC_STAGE
void usurper_scene_diagnostic_start(unsigned stage) {
    campaign_stage_id=stage;reset_run();phase=USURPER_PHASE_BATTLE;tutorial_seen=1;
    player_hp=player_hp_max=9999;player_shield=0;player_invincible_ticks=0;
    level_tick=BOSS_TIME_TICK;campaign_next_birth=61;
    boss_spawn();boss_grace_ticks=0;
    message("BOARD DIAGNOSTIC / FORCED BOSS / HP 9999");
}
#endif
unsigned usurper_scene_asset_height(void) { return asset_height; }

#ifdef USURPER_PROFILE_ENABLED
#include "usurper_profile_scene_impl.h"
#endif

unsigned usurper_scene_phase(void) { return phase; }
unsigned usurper_scene_boss_active(void) { return boss_active; }
unsigned usurper_scene_boss_hp(void) { return boss_hp; }
unsigned usurper_scene_result_victory(void) { return result_victory; }
unsigned usurper_scene_wave(void) { return level_tick < BOSS_TIME_TICK ? 1u : 2u; }
unsigned usurper_scene_tick(void) { return frame_tick; }
unsigned usurper_scene_level_tick(void) { return level_tick; }
unsigned usurper_scene_player_hp(void) { return visible_player_health(); }
unsigned usurper_scene_player_max_hp(void) { return player_hp_max; }
unsigned usurper_scene_energy(void) { return visible_player_health(); }
unsigned usurper_scene_captured(void) { return captured_total; }
unsigned usurper_scene_enemy_count(void) { return enemy_count(); }
unsigned usurper_scene_supply_count(void){return supply_stock(NULL,0,0)+supply_stock(NULL,1,0);}
unsigned usurper_scene_supply_pending(void){return supplies.count;}
void usurper_scene_diagnostic_supplies(void){
    reset_run();campaign_stage_id=2;phase=USURPER_PHASE_BATTLE;completed_stage_mask=3;
    player_x=camera_x=400*Q8;player_y=camera_y=240*Q8;player_hp=player_hp_max=9999;player_shield=0;campaign_next_birth=10000;
}
void usurper_scene_diagnostic_warden(void){
    reset_run();campaign_stage_id=2;phase=USURPER_PHASE_BATTLE;completed_stage_mask=7;
    player_x=camera_x=400*Q8;player_y=camera_y=240*Q8;player_hp=player_hp_max=9999;player_shield=0;player_shield_max=8;
    campaign_next_birth=10000;boss_spawned=1;boss_active=0;us_supplies_stop(&supplies,&supply_callbacks);
    message("WARDEN / ISOLATED DIAGNOSTIC");
}
unsigned usurper_scene_warden_active(void){return warden_field.active;}
unsigned usurper_scene_warden_intercepted(void){return warden_field.interceptions;}
unsigned usurper_scene_interceptor_count(void){unsigned count=0;for(unsigned i=0;i<WARDEN_THREAT_COUNT;++i)count+=interceptors[i].motion.active;return count;}
unsigned usurper_scene_interceptor_hits(void){return warden_interceptor_hits;}
unsigned usurper_scene_warden_hull_progress(void){return enemies[0].species==SPECIES_WARDEN?enemies[0].capture_q8:0;}
unsigned usurper_scene_warden_hull_scale(void){return (unsigned)(warden_status[0].scale*1000+0.5f);}
unsigned usurper_scene_weak_count(void) {
    unsigned count = 0;
    for (unsigned i = 0; i < ENEMY_MAX; ++i) count += enemies[i].active && enemies[i].capture_q8 > 0u;
    return count;
}
unsigned usurper_scene_visible(void) { return enemy_count() + (boss_active ? 1u : 0u); }
unsigned usurper_scene_tutorial_step(void) { return tutorial_step; }
unsigned usurper_scene_tutorial_complete(void) { return tutorial_done; }
unsigned usurper_scene_alloy(void) { return alloy_wallet + alloy_run; }
unsigned usurper_scene_released(void) { return released_total; }
unsigned usurper_scene_tech_level(void) {
    unsigned total = 0;
    for (unsigned i = 0; i < USURPER_TECH_NODE_COUNT; ++i) total += tech_rank(i);
    return total;
}
unsigned usurper_scene_boss_armor(void) { return boss_armor_left; }
unsigned usurper_scene_boss_phase(void) { return boss_phase; }
unsigned usurper_scene_stage(void) { return campaign_stage_id; }
unsigned usurper_scene_completed_mask(void) { return completed_stage_mask; }
unsigned usurper_scene_boss_open(void) { return boss_active && (campaign_stage_id==7u?controller_boss.shield<=0:campaign_stage_id == 6u ? hangar_boss.exposed : campaign_stage_id == 5u ? boundary_boss.exposed : campaign_stage_id == 4u ? twin_boss.exposed : campaign_stage_id == 3u ? flame_boss.core_exposed : campaign_stage_id == 2u ? archon_boss.core_exposed : campaign_stage_id == 1u ? mechanic_boss.core_exposed : !boss_armor_left); }
unsigned usurper_scene_cleared(void) { return cleared_sectors; }
unsigned usurper_scene_menu_selection(void) { return menu_selection; }
unsigned usurper_scene_menu_confirm(void) { return menu_confirm; }
unsigned usurper_scene_pointer_x(void) { return (unsigned)pointer_x; }
unsigned usurper_scene_pointer_y(void) { return (unsigned)pointer_y; }
unsigned usurper_scene_pointer_visible(void) { return pointer_visible; }
unsigned usurper_scene_controls_held(void) { return controls_held; }
const usurper_loadout_t *usurper_scene_loadout(void) { return &selected_loadout; }
int usurper_scene_loadout_apply(const usurper_loadout_t *value) {
    if((phase!=USURPER_PHASE_MENU&&phase!=USURPER_PHASE_TECH) ||
       !us_loadout_valid(value,completed_stage_mask))return -1;
    selected_loadout=*value;
    supplies.loadout[0]=value->support_species[0];supplies.loadout[1]=value->support_species[1];
    return 0;
}
int usurper_scene_select_form(int32_t index) {
    usurper_loadout_t value=selected_loadout;value.form_index=index;
    return usurper_scene_loadout_apply(&value);
}
int usurper_scene_select_starting_species(unsigned species) {
    usurper_loadout_t value=selected_loadout;value.starting_species=species;
    return usurper_scene_loadout_apply(&value);
}
int usurper_scene_select_support(unsigned slot,unsigned species) {
    if(slot>=us_loadout_support_slots(completed_stage_mask))return -1;
    usurper_loadout_t value=selected_loadout;value.support_species[slot]=species;
    return usurper_scene_loadout_apply(&value);
}
unsigned usurper_scene_starting_unit_spawned(void) { return starting_unit_spawned; }
const char *usurper_scene_message(void) { return state_message; }


