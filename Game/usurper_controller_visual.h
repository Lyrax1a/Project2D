#ifndef USURPER_CONTROLLER_VISUAL_H
#define USURPER_CONTROLLER_VISUAL_H
#include "usurper_controller.h"
#include "usurper_boundary_visual.h"
typedef struct {
    void *context;
    void (*polygon)(void *,const us_flame_vertex_t *,unsigned,us_boundary_color_t);
    void (*line)(void *,const us_flame_vertex_t *,unsigned,float,us_boundary_color_t,unsigned);
} us_controller_painter_t;
/* Emits AA intent; coverage must be implemented by the scene painter. */
void us_controller_draw(const us_controller_t *boss,const us_controller_painter_t *painter);
#endif
