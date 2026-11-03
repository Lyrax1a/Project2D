#include "usurper_hangar_visual.h"
#include "usurper_softfloat.h"
typedef struct {const us_boundary_painter_t *p;float x,y,c,s;} canvas_t;
static us_boundary_color_t color(unsigned rgb) {
    #define RGB_CASE(v) case v:return (us_boundary_color_t){((v>>16)&255)/255.0f,((v>>8)&255)/255.0f,(v&255)/255.0f,1};
    switch(rgb){RGB_CASE(0xffffff) RGB_CASE(0xdcdcdc) RGB_CASE(0x7c8d98) RGB_CASE(0xffe078)
        RGB_CASE(0xfff5cb) RGB_CASE(0x71848c) RGB_CASE(0xfff3bd) RGB_CASE(0x788d98)
        RGB_CASE(0x34434b) RGB_CASE(0x677a85) RGB_CASE(0x657780) default:break;}
    #undef RGB_CASE
    return (us_boundary_color_t){((rgb>>16)&255)/255.0f,((rgb>>8)&255)/255.0f,(rgb&255)/255.0f,1};
}
static void poly(canvas_t *c,const us_flame_vertex_t *v,unsigned n,us_boundary_color_t tint) {
    us_flame_vertex_t world[8];
    for(unsigned i=0;i<n;++i){world[i].x=us_sf_add(us_sf_add(c->x,us_sf_mul(c->c,v[i].x)),-us_sf_mul(c->s,v[i].y));world[i].y=us_sf_add(us_sf_add(c->y,us_sf_mul(c->s,v[i].x)),us_sf_mul(c->c,v[i].y));}
    c->p->polygon(c->p->context,world,n,tint);
}
static void rect(canvas_t *c,float x,float y,float w,float h,us_boundary_color_t tint) {
    us_flame_vertex_t v[4]={{x,y},{x+w,y},{x+w,y+h},{x,y+h}};poly(c,v,4,tint);
}
static void hull(canvas_t *c,const us_hangar_t *b) {
    us_boundary_color_t tint=color(b->flash>0?0xffffff:0xdcdcdc);
    for(int sign=-1;sign<=1;sign+=2) {
        us_flame_vertex_t v[6]={{-44,16.0f*sign},{-28,32.0f*sign},{28,32.0f*sign},{44,16.0f*sign},{24,12.0f*sign},{-28,12.0f*sign}};
        poly(c,v,6,tint);rect(c,-10,(float)(sign*23-3),14,6,color(0x7c8d98));
    }
    rect(c,-44,-16,16,32,tint);rect(c,-39,-5,6,10,color(0xffe078));
    rect(c,-4,-6,14,12,color(b->exposed?0xfff5cb:0x71848c));
}
static void bay(canvas_t *c,const us_hangar_t *b,unsigned i) {
    unsigned open=us_hangar_target_open(b,i),firing=us_hangar_part_firing(b,i);
    us_boundary_color_t tint=color(b->part_flash[i]>0?0xffffff:0xdcdcdc),light=color(firing?0xfff3bd:0x788d98);
    us_flame_vertex_t base[6]={{-13,-11},{8,-11},{19,-5},{19,5},{8,11},{-13,11}};poly(c,base,6,color(0x34434b));
    for(int sign=-1;sign<=1;sign+=2) {
        us_flame_vertex_t v[8]={{-15,14.0f*sign},{7,14.0f*sign},{18,6.0f*sign},{9,6.0f*sign},{4,9.0f*sign},{-9,9.0f*sign},{-9,3.0f*sign},{-15,3.0f*sign}};
        poly(c,v,8,tint);rect(c,0,(float)(sign*10-2),6,4,light);
    }
    tint.r*=0.75f;tint.g*=0.75f;tint.b*=0.75f;rect(c,-15,-9,6,18,tint);
    if(!open){rect(c,-8,-7,15,14,color(0x677a85));rect(c,-2,-7,3,14,color(b->part_flash[i]>0?0xffffff:0xdcdcdc));}
    else {us_flame_vertex_t plane[4]={{10,0},{-3,-5},{0,0},{-3,5}};poly(c,plane,4,firing?light:color(0x657780));}
}
void us_hangar_draw(const us_hangar_t *b,const us_boundary_painter_t *p) {
    if(!b->active)return;
    canvas_t c={p,b->x,b->y,0,0};us_fx_direction_float(b->rotation,&c.c,&c.s);hull(&c,b);
    for(unsigned i=0;i<3;++i)if(b->part_hp[i]>0) {
        float hx,hy;us_fx_direction_float((float)(i*(6.28318530717958647692/3)),&hx,&hy);hx*=70;hy*=70;
        canvas_t part={p,b->x+c.c*hx-c.s*hy,b->y+c.s*hx+c.c*hy,c.c,c.s};bay(&part,b,i);
    }
}
