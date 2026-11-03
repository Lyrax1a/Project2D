#ifndef USURPER_FORM_VISUAL_H
#define USURPER_FORM_VISUAL_H
#include "usurper_controller_visual.h"
#include "usurper_fixed.h"
#include "usurper_softfloat.h"

static inline void us_form_arc(const us_controller_painter_t *p,float radius,float from,float to,
        unsigned count,float width,us_boundary_color_t color) {
    us_flame_vertex_t points[64];if(count>64)count=64;
    for(unsigned i=0;i<count;++i){float x,y;
        float angle=us_sf_add(from,us_sf_mul(us_sf_add(to,-from),(float)i/(count-1)));
        us_fx_direction_float(angle,&x,&y);points[i]=(us_flame_vertex_t){us_sf_mul(x,radius),us_sf_mul(y,radius)};}
    p->line(p->context,points,count,width,color,1);
}
/* specialization.draw_hull's first-three-stage forms in local world units. */
static inline void us_form_hull_draw(const us_controller_painter_t *p,int form,int shielded,int hurt,
        double cooldown,double guard_flash,int dashing) {
    const us_boundary_color_t blue={57/255.0f,189/255.0f,1,1};
    us_boundary_color_t edge=hurt?(us_boundary_color_t){1,141/255.0f,152/255.0f,1}:(us_boundary_color_t){212/255.0f,245/255.0f,1,1};
    us_boundary_color_t fill=blue;fill.a=.12f;
    us_flame_vertex_t points[7];unsigned n=0;
    if(form==0){us_flame_vertex_t v[4]={{8,0},{-5,-7},{-1,0},{-5,7}};for(unsigned i=0;i<4;++i)points[i]=v[i];n=4;}
    else if(form==1){us_flame_vertex_t v[5]={{9,0},{0,-8},{-7,-4},{-7,4},{0,8}};for(unsigned i=0;i<5;++i)points[i]=v[i];n=5;}
    else if(form==2){us_flame_vertex_t v[6]={{-8,-7},{3,-7},{7,-3},{7,3},{3,7},{-8,7}};for(unsigned i=0;i<6;++i)points[i]=v[i];n=6;}
    else if(form==6){us_flame_vertex_t v[4]={{10,0},{-5,-5},{-2,0},{-5,5}};for(unsigned i=0;i<4;++i)points[i]=v[i];n=4;}
    if(n){p->polygon(p->context,points,n,fill);points[n]=points[0];p->line(p->context,points,n+1,2.2f,edge,1);}
    if(form==2){us_flame_vertex_t gun[4]={{3,-2},{13,-2},{13,2},{3,2}};p->polygon(p->context,gun,4,blue);}
    if(form==9){
        us_flame_vertex_t circle[64];us_flame_circle_polygon(0,0,10,circle);p->polygon(p->context,circle,64,fill);
        us_form_arc(p,10,0,6.283185307179586f,32,2.5f,edge);
        us_flame_vertex_t diamond[5]={{5,0},{0,-5},{-5,0},{0,5},{5,0}};p->line(p->context,diamond,5,2,blue,1);
    }
    us_flame_vertex_t core[4]={{4,0},{-1,-3},{-4,0},{-1,3}};unsigned count=4;
    if(form==0||form==6){core[0]=(us_flame_vertex_t){4,0};core[1]=(us_flame_vertex_t){0,-2};core[2]=(us_flame_vertex_t){0,2};count=3;}
    if(form==9){core[0]=(us_flame_vertex_t){3,0};core[1]=(us_flame_vertex_t){0,-3};core[2]=(us_flame_vertex_t){-3,0};core[3]=(us_flame_vertex_t){0,3};}
    if((form!=1&&form!=9)||cooldown<=0)p->polygon(p->context,core,count,blue);
    if(form==9){us_boundary_color_t tint=blue;if(cooldown>0)tint.a=.2f;us_form_arc(p,14,-.95f,.95f,12,3,tint);}
    if(shielded){us_boundary_color_t tint=blue;tint.a=.28f;us_form_arc(p,12,0,6.283185307179586f,28,1.8f,tint);}
    if(guard_flash>0){us_boundary_color_t tint={212/255.0f,245/255.0f,1,(float)(guard_flash/.25)};us_form_arc(p,30,-.95f,.95f,20,4,tint);}
    if(dashing){us_boundary_color_t tint=blue;tint.a=.65f;for(int side=-1;side<=1;side+=2){us_flame_vertex_t line[2]={{-5,side*5.0f},{-26,side*5.0f}};p->line(p->context,line,2,3,tint,1);}}
}
#endif
