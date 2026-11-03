#ifndef USURPER_WARDEN_BAKED_H
#define USURPER_WARDEN_BAKED_H
#include <stddef.h>
#include <stdint.h>
#include "usurper_scene.h"
#include "usurper_warden_hull_visual.h"
#include "usurper_warden.h"
#define US_WARDEN_BAKED_BASE 0x0d000000u
#define US_WARDEN_BAKED_LIMIT 0x01800000u
#define US_WARDEN_BAKED_CACHE_BASE 0x0e800000u
#define US_WARDEN_BAKED_CACHE_BYTES 0x00800000u

/* Blob is immutable after attach; caller owns DDR allocation and cache drain.
 * No per-pixel processing occurs in the runtime drawing functions. */
int us_warden_baked_attach(const void *memory,size_t bytes);
unsigned us_warden_baked_ready(void);
size_t us_warden_baked_bytes(void);
uint32_t us_warden_baked_pixel_base(void);
size_t us_warden_baked_pixel_bytes(void);
int us_warden_baked_image(unsigned image,int screen_x,int screen_y,unsigned opacity,
    uint32_t framebuffer,usurper_emit_fn emit,void *context);
/* Returns -2 for unsupported poses, -1 on malformed input/emit failure, 0 on success.
 * Hull/aura omit slow streaks and escort links; the scene must draw those separately. */
int us_warden_baked_hull(const us_warden_hull_view_t *view,int screen_x,int screen_y,
    uint32_t framebuffer,usurper_emit_fn emit,void *context);
int us_warden_baked_aura(const us_warden_hull_view_t *view,int screen_x,int screen_y,
    uint32_t framebuffer,usurper_emit_fn emit,void *context);
int us_warden_baked_field(const us_warden_field_t *field,int screen_x,int screen_y,
    uint32_t framebuffer,usurper_emit_fn emit,void *context);
int us_warden_baked_marker(unsigned front,int screen_x,int screen_y,
    uint32_t framebuffer,usurper_emit_fn emit,void *context);
int us_warden_baked_form(int form,unsigned angle,int shielded,int hurt,
    double cooldown,double guard_flash,int dashing,int screen_x,int screen_y,
    uint32_t framebuffer,usurper_emit_fn emit,void *context);
#endif
