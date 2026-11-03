#ifndef USURPER_WARDEN_HULL_VISUAL_H
#define USURPER_WARDEN_HULL_VISUAL_H
#include "usurper_controller_visual.h"
typedef struct {
    float x,y,rotation,scale,radius,escort_x,escort_y;
    double progress,disruption,status_time;
    unsigned alive,supply,icon,flash,slow,material,escort_valid,escort_alive;
    us_boundary_color_t controlled;
} us_warden_hull_view_t;
typedef struct {double flash,kick,time;float scale;} us_warden_status_t;
void us_warden_status_hit(us_warden_status_t *);
void us_warden_status_step(us_warden_status_t *,double delta);
/* Original draw expressions, including AA intent. Material clipping and pixel
 * coverage are the renderer's responsibility, not encoded as extra outlines. */
void us_warden_hull_draw(const us_warden_hull_view_t *,const us_controller_painter_t *);
void us_warden_aura_draw(const us_warden_hull_view_t *,const us_controller_painter_t *);
/* Emit source local vertices so the renderer tessellates before owner transform. */
void us_warden_hull_draw_local(const us_warden_hull_view_t *,const us_controller_painter_t *);
void us_warden_aura_draw_local(const us_warden_hull_view_t *,const us_controller_painter_t *);
/* Dynamic overlays retained when the static hull/aura layers use baked masks. */
void us_warden_slow_draw_local(const us_warden_hull_view_t *,const us_controller_painter_t *);
void us_warden_escort_draw_local(const us_warden_hull_view_t *,const us_controller_painter_t *);
void us_warden_capture_draw(float x,float y,double progress,const us_controller_painter_t *);
void us_warden_marker_draw(float x,float y,unsigned front,const us_controller_painter_t *);
us_boundary_color_t us_conversion_color(us_boundary_color_t,float local_x,float progress,float low,float high,us_boundary_color_t tint,float brightness_floor);
typedef struct {float a,b,c,cut;unsigned mode;} us_conversion_mask_t;
void us_conversion_mask_init(us_conversion_mask_t *,float x,float y,float rotation,float scale,float camera_x,float camera_y,float progress,float low,float high);
/* Split a horizontal span at its source material's left-to-right fill plane.
 * left_filled describes the first portion; return its width. */
unsigned us_conversion_mask_split(const us_conversion_mask_t *,int x,int y,unsigned width,unsigned *left_filled);
#endif
