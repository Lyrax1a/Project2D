#ifndef USURPER_FLAME_H
#define USURPER_FLAME_H
#include "usurper_fixed.h"
typedef struct {
    float x_float, y_float, rotation, locked_x, locked_y, destination_x, destination_y;
    int32_t x, y, angle;
    double hp, max_hp, elapsed, attack_clock, recovery, phase_started, core_hit_clock,
           pattern_clock, pattern_shot_clock, shot_clock, flash;
    uint32_t hp_q16, attack_index;
    uint8_t active, phase, core_exposed, state, jumps;
} us_flame_t;
typedef struct { unsigned flame_damage, teleported; } us_flame_output_t;
void us_flame_init(us_flame_t *boss, uint32_t hp_q16, int32_t x, int32_t y, int32_t px, int32_t py);
void us_flame_step(us_flame_t *boss, int32_t px, int32_t py, us_flame_output_t *output);
void us_flame_hit(us_flame_t *boss, uint32_t damage_q16, float source_x, float source_y);
unsigned us_flame_contains(const us_flame_t *boss, float px, float py);
unsigned us_flame_active(const us_flame_t *boss);
#endif
