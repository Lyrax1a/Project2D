#ifndef USURPER_POLYLINE_H
#define USURPER_POLYLINE_H
#include "usurper_flame_visual.h"
#define US_POLYLINE_MAX_POINTS 66u
#define US_POLYLINE_MAX_VERTICES (2u*US_POLYLINE_MAX_POINTS+5u)
typedef struct {float x,y,alpha;} us_polyline_vertex_t;
typedef struct {unsigned count;us_polyline_vertex_t vertices[US_POLYLINE_MAX_VERTICES];} us_polyline_strip_t;
typedef struct {unsigned count,loop;us_polyline_strip_t strips[3];} us_polyline_mesh_t;
/* Godot 4.6.2 canvas_item_add_polyline geometry for uniform RGB/alpha.
 * Mesh order is core, left feather, right feather; opacity is per vertex. */
unsigned us_polyline_mesh(const us_flame_vertex_t *,unsigned count,float width,unsigned aa,us_polyline_mesh_t *);
#endif
