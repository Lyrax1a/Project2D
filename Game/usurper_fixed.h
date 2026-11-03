#ifndef USURPER_FIXED_H
#define USURPER_FIXED_H
#include <stdint.h>
#define US_FX_ONE 65536
#define US_FX_TURN (360 * US_FX_ONE)
typedef unsigned (*us_boss_guard_sweep_fn)(void *,float,float,float,float,float *,float *);
uint32_t us_fx_length(int32_t x, int32_t y);
int32_t us_fx_angle(int32_t x, int32_t y);
void us_fx_direction(int32_t angle, int32_t *x, int32_t *y);
int32_t us_fx_turn_toward(int32_t angle, int32_t target, int32_t step);
unsigned us_fx_clip_motion(int32_t *x, int32_t *y, int32_t dx, int32_t dy,
                           int32_t left, int32_t top, int32_t right, int32_t bottom);
float us_fx_sqrt_float(float value);
float us_fx_angle_float(float x, float y);
void us_fx_direction_float(float radians, float *x, float *y);
float us_fx_turn_float(float radians, float target, double step);
double us_fx_turn_double(double radians, double target, double step);
double us_fx_sin_double(double radians);
double us_fx_cos_double(double radians);
#endif
