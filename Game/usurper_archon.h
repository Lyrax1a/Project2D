#ifndef USURPER_ARCHON_H
#define USURPER_ARCHON_H
#include "usurper_mechanic.h"
#define US_ARCHON_PARTS 6u
#define US_ARCHON_CORE 6u
#define US_ARCHON_SHOTS 17u

typedef struct {
    float x, y, home_x, home_y, launch_x, launch_y, rotation, firing_x, firing_y;
    double hp, deployment, shot_clock, barrel;
    uint8_t active, rounds, flash;
} us_archon_turret_t;

typedef struct {
    float x_float, y_float, vx_float, vy_float, rotation;
    int32_t x, y, vx, vy, angle;
    double hp, max_hp, shield, shield_max, elapsed, phase_started, attack_clock,
           shot_clock, recovery, exposed_at, opening_left, core_hit_clock;
    uint32_t hp_q16, shield_q16, breaks;
    uint8_t active, phase, core_exposed, remaining, deployed, deploy_pending, flash;
    us_archon_turret_t parts[US_ARCHON_PARTS];
} us_archon_t;
typedef struct { unsigned count; us_boss_shot_t shots[US_ARCHON_SHOTS]; } us_archon_output_t;

void us_archon_init(us_archon_t *boss, uint32_t hp_q16, int32_t x, int32_t y, int32_t px, int32_t py);
void us_archon_step(us_archon_t *boss, int32_t px, int32_t py, us_archon_output_t *output);
/* Mirrors Godot's call_deferred deployment, after the current physics frame. */
void us_archon_flush_deferred(us_archon_t *boss, int32_t px, int32_t py);
unsigned us_archon_target_position(const us_archon_t *boss, unsigned target, int32_t *x, int32_t *y);
unsigned us_archon_target_open(const us_archon_t *boss, unsigned target);
unsigned us_archon_hit(us_archon_t *boss, unsigned target, uint32_t damage_q16);
#endif
