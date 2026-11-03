#ifndef USURPER_TWIN_H
#define USURPER_TWIN_H
#include "usurper_fixed.h"
#define US_TWIN_PARTS 6u
#define US_TWIN_SPLITTER 6u
#define US_TWIN_BOMBER 7u
#define US_TWIN_BOMBS 4u
#define US_TWIN_HIT_BOMB 1u
#define US_TWIN_HIT_GUARD 2u
typedef struct {
    float x, y, vx, vy, rotation, locked_x, locked_y, cast_x, cast_y;
    double hp, max_hp, elapsed, recovery, move_clock, shot_clock, cast_left,
           shot_flash, shield_flash, core_hit_clock, flash;
    uint8_t active, phase, shielded, exposed, move_state;
} us_twin_actor_t;
typedef struct {
    float x, y, launch_x, launch_y, landing_x, landing_y;
    double age, lifetime, speed, intercept_hp;
    unsigned serial;
    uint8_t active;
} us_twin_bomb_t;
typedef struct {
    us_twin_actor_t actors[2];
    us_twin_bomb_t bombs[US_TWIN_BOMBS];
    double part_hp[US_TWIN_PARTS], part_flash[US_TWIN_PARTS];
    double elapsed, hp, max_hp, shield_broken_at, armor_broken_at, death_times[2];
    float x, y;
    unsigned bomb_count, shield_breaks, breaks;
    uint8_t active, combat_enabled, remaining, cycle, phase, exposed, shield_source;
} us_twin_t;
typedef struct {
    unsigned launched, exploded, damage_count;
    struct { float x, y; } explosions[US_TWIN_BOMBS];
} us_twin_output_t;
void us_twin_init(us_twin_t *boss, uint32_t hp_q16, int32_t x, int32_t y, int32_t px, int32_t py);
void us_twin_refresh(us_twin_t *boss);
void us_twin_step(us_twin_t *boss, int32_t px, int32_t py, int32_t pvx, int32_t pvy,
                  unsigned max_speed, us_twin_output_t *output);
void us_twin_hit(us_twin_t *boss, unsigned target, uint32_t damage_q16, unsigned tags);
void us_twin_guard_block(us_twin_t *boss);
unsigned us_twin_break_shield(us_twin_t *boss, float at_x, float at_y, double radius, unsigned source);
unsigned us_twin_target_present(const us_twin_t *boss, unsigned target);
unsigned us_twin_target_open(const us_twin_t *boss, unsigned target);
unsigned us_twin_target_position(const us_twin_t *boss, unsigned target, int32_t *x, int32_t *y);
unsigned us_twin_blast_contains(float dx, float dy, double radius);
unsigned us_twin_intercept(us_twin_t *boss, unsigned slot, double damage);
#endif
