#ifndef USURPER_RASTER_H
#define USURPER_RASTER_H
#include "usurper_flame_visual.h"
void us_raster_polygon(const us_flame_vertex_t *points,unsigned count,unsigned color,unsigned alpha,
    unsigned width,unsigned height,void (*span)(void *,int,int,unsigned,unsigned,unsigned),void *context);
#endif
