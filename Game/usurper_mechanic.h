#ifndef USURPER_MECHANIC_H
#define USURPER_MECHANIC_H
#include "usurper_fixed.h"
#define US_MECHANIC_PARTS 6u
#define US_MECHANIC_CORE 6u
#define US_MECHANIC_HIT_FRAGMENT 1u
#define US_MECHANIC_HIT_GUARD 2u

typedef struct {
    int32_t x, y, vx, vy, angle, locked_x, locked_y;
    float x_float, y_float, vx_float, vy_float, rotation, locked_x_float, locked_y_float;
    uint32_t hp_q16, max_hp_q16, part_hp_q16[US_MECHANIC_PARTS];
    /* GDScript timers use IEEE doubles. Retain their boundary semantics;
     * RV32IM libgcc supplies these operations without a hardware FPU. */
    double elapsed, attack_clock, shot_clock, recovery, phase_started, exposed_at, fragment_window;
    uint32_t attack_index, breaks;
    uint8_t active, phase, core_exposed, remaining, fragment_hits, part_flash[US_MECHANIC_PARTS];
    void *guard_context;
    us_boss_guard_sweep_fn guard_sweep;
} us_mechanic_t;

typedef struct { int32_t x, y, direction_x, direction_y; uint16_t damage, speed; } us_boss_shot_t;
typedef struct { unsigned count; us_boss_shot_t shots[3]; } us_mechanic_output_t;

void us_mechanic_init(us_mechanic_t *boss, uint32_t hp_q16, int32_t x, int32_t y, int32_t px, int32_t py);
void us_mechanic_step(us_mechanic_t *boss, int32_t px, int32_t py, us_mechanic_output_t *output);
unsigned us_mechanic_target_present(const us_mechanic_t *boss, unsigned target);
unsigned us_mechanic_target_open(const us_mechanic_t *boss, unsigned target);
unsigned us_mechanic_target_position(const us_mechanic_t *boss, unsigned target, int32_t *x, int32_t *y);
void us_mechanic_hit(us_mechanic_t *boss, unsigned target, uint32_t damage_q16, unsigned tags);
void us_mechanic_guard_blocked(us_mechanic_t *boss);
#endif
