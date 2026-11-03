#ifndef USURPER_BOUNDARY_H
#define USURPER_BOUNDARY_H
#include "usurper_fixed.h"
#define US_BOUNDARY_PARTS 12u
#define US_BOUNDARY_CORE 12u
#define US_BOUNDARY_UNITS 16u
#define US_BOUNDARY_GUARDS 8u
enum { US_BOUNDARY_WAIT, US_BOUNDARY_WARN, US_BOUNDARY_REACH, US_BOUNDARY_HOLD, US_BOUNDARY_RETURN };
typedef struct {
    float x, y, mount_x, mount_y, destination_x, destination_y, rotation, heading_x, heading_y, beam_x, beam_y;
    double hp, clock, reach_age, flash, crush_progress;
    int tracked_unit, captive;
    uint8_t active, side, role, state, fast;
} us_boundary_part_t;
typedef struct { float x, y; uint32_t id; uint8_t active, valid, pending_free, order; } us_boundary_unit_t;
typedef struct {
    float x,y,block_x,block_y;
    double radius,age,duration,block_flash;
    uint8_t is_warden,strengthened;
} us_boundary_guard_t;
typedef struct {
    us_boundary_part_t parts[US_BOUNDARY_PARTS];
    us_boundary_unit_t units[US_BOUNDARY_UNITS];
    us_boundary_guard_t guards[US_BOUNDARY_GUARDS];
    double insets[4], hp, max_hp, elapsed, beat_clock, shot_clock, core_hit_clock, flash, delta, crush_left;
    float px, py, vx, vy;
    unsigned remaining, breaks, beat, crush_pending, guard_count;
    uint8_t broken[4], phase, exposed, active, presentation_enabled, crush_holding;
} us_boundary_t;
typedef struct {
    unsigned charge_hits, beam_hits, crushed, shot_count, grabbed_units;
    struct { float x, y, dx, dy; unsigned damage, speed; } shots[3];
} us_boundary_output_t;
void us_boundary_init(us_boundary_t *boss, uint32_t hp_q16, int32_t px, int32_t py, unsigned units);
void us_boundary_step(us_boundary_t *boss, int32_t px, int32_t py, int32_t vx, int32_t vy, us_boundary_output_t *output);
void us_boundary_step_delta(us_boundary_t *boss, int32_t px, int32_t py, int32_t vx, int32_t vy, double delta, us_boundary_output_t *output);
void us_boundary_finish_crush(us_boundary_t *boss);
void us_boundary_cinematic_tick(us_boundary_t *boss, double real_delta);
void us_boundary_flush_deferred(us_boundary_t *boss);
void us_boundary_hit(us_boundary_t *boss, unsigned target, uint32_t damage_q16, unsigned beam);
void us_boundary_safe_rect(const us_boundary_t *boss, float rect[4]);
void us_boundary_guard_block(us_boundary_t *boss, unsigned side);
#endif
