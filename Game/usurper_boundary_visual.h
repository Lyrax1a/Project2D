#ifndef USURPER_BOUNDARY_VISUAL_H
#define USURPER_BOUNDARY_VISUAL_H
#include "usurper_boundary.h"
#include "usurper_flame_visual.h"
typedef struct { float r,g,b,a; } us_boundary_color_t;
typedef struct {
    void *context;
    void (*polygon)(void *, const us_flame_vertex_t *, unsigned, us_boundary_color_t);
    void (*line)(void *, const us_flame_vertex_t *, unsigned, float, us_boundary_color_t);
} us_boundary_painter_t;
void us_boundary_draw(const us_boundary_t *boss, const us_boundary_painter_t *painter);
#endif
