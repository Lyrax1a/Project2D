#ifndef USURPER_TRIANGLE_H
#define USURPER_TRIANGLE_H
#include "usurper_polyline.h"
/* Compatibility-style 1/256 vertex grid and half-open triangle edges. */
void us_triangle_coverage(const us_polyline_vertex_t vertices[3],unsigned width,unsigned height,
    void (*span)(void *,int,int,unsigned,float),void *context);
/* Same raster ownership, with the final paint*coverage integer opacity.
 * Guarded integer arithmetic falls back at every ambiguous rounding edge. */
void us_triangle_resolved_alpha(const us_polyline_vertex_t vertices[3],unsigned width,unsigned height,float paint,
    void (*span)(void *,int,int,unsigned,unsigned),void *context);
#endif
