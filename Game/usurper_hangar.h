#ifndef USURPER_HANGAR_H
#define USURPER_HANGAR_H
#include "usurper_fixed.h"
#define US_HANGAR_PARTS 3u
#define US_HANGAR_CORE 3u
typedef struct {
    float x,y,vx,vy,rotation;
    double hp,max_hp,elapsed,attack_clock,recovery,flight_clock,core_hit_clock,flash,phase_started;
    double part_hp[3],part_flash[3];
    unsigned remaining,breaks;
    uint8_t active,phase,exposed;
} us_hangar_t;
typedef struct { unsigned count; struct {float x,y;unsigned count;} fighters[3]; } us_hangar_output_t;
void us_hangar_init(us_hangar_t *boss,uint32_t hp_q16,int32_t x,int32_t y,int32_t px,int32_t py);
unsigned us_hangar_target_open(const us_hangar_t *boss,unsigned target);
unsigned us_hangar_part_firing(const us_hangar_t *boss,unsigned part);
unsigned us_hangar_fighter_slots(const us_hangar_t *boss,unsigned total,unsigned owned,unsigned requested);
unsigned us_hangar_target_position(const us_hangar_t *boss,unsigned target,int32_t *x,int32_t *y);
void us_hangar_hit(us_hangar_t *boss,unsigned target,uint32_t damage_q16);
void us_hangar_step(us_hangar_t *boss,int32_t px,int32_t py,us_hangar_output_t *output);
void us_hangar_birth(int32_t px,int32_t py,int32_t camera_x,int32_t camera_y,int32_t *x,int32_t *y,unsigned preferred);
#endif
