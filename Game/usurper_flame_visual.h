#ifndef USURPER_FLAME_VISUAL_H
#define USURPER_FLAME_VISUAL_H
#include "usurper_flame.h"
typedef struct { float x, y; } us_flame_vertex_t;
#define US_RASTER_POLYGON_MAX 66u
typedef void (*us_flame_polygon_fn)(void *, const us_flame_vertex_t *, unsigned, unsigned, unsigned);
typedef void (*us_flame_circle_fn)(void *, float, float, float, unsigned, unsigned);
typedef struct { void *context; us_flame_polygon_fn polygon; us_flame_circle_fn circle; } us_flame_painter_t;
void us_flame_circle_polygon(float x, float y, float radius, us_flame_vertex_t points[64]);
/* Palette rows: pink, white mixes .3/.6/.45, bright cyan, gold,
 * core white, marker white. Alpha remains 8-bit command global_alpha. */
void us_flame_plume(float heading_x, float heading_y, double reach, double clock, const us_flame_painter_t *painter);
void us_flame_raster_polygon(const us_flame_vertex_t *points, unsigned count, unsigned color, unsigned alpha,
    unsigned width, unsigned height, void (*span)(void *, int, int, unsigned, unsigned, unsigned), void *context);
void us_flame_raster_circle(float x, float y, float radius, unsigned color, unsigned alpha,
    unsigned width, unsigned height, void (*span)(void *, int, int, unsigned, unsigned, unsigned), void *context);
#endif
