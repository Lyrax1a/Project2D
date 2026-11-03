#include "usurper_warden_visual.h"
#include "usurper_softfloat.h"
#include "usurper_atan.h"
#include "usurper_circle_generated.h"
#include "usurper_warden_directions_generated.h"
static us_boundary_color_t tint(float alpha){return (us_boundary_color_t){57/255.0f,189/255.0f,1,alpha};}
static void line(const us_warden_field_t *field,const us_controller_painter_t *p,const us_flame_vertex_t *local,unsigned count,float width,us_boundary_color_t color){
    us_flame_vertex_t points[64];for(unsigned i=0;i<count;++i)points[i]=(us_flame_vertex_t){us_sf_add(field->x,local[i].x),us_sf_add(field->y,local[i].y)};
    p->line(p->context,points,count,width,color,0);
}
static void arc(const us_warden_field_t *field,const us_controller_painter_t *p,float radius,float from,float to,unsigned count,float width,us_boundary_color_t color){
    us_flame_vertex_t points[64];const float tau=6.28318530717958647692f;float difference=us_sf_add(to,-from);if(difference>tau)difference=tau;else if(difference<-tau)difference=-tau;
    for(unsigned i=0;i<count;++i){float angle=us_sf_add(us_sf_mul(us_sf_div((float)i,(float)(count-1)),difference),from),x,y;us_fx_direction_float(angle,&x,&y);points[i]=(us_flame_vertex_t){us_sf_mul(x,radius),us_sf_mul(y,radius)};}
    line(field,p,points,count,width,color);
}
void us_warden_draw(const us_warden_field_t *field,const us_controller_painter_t *p){
    if(!field->active)return;
    us_flame_vertex_t circle[64];for(unsigned i=0;i<64;++i)circle[i]=(us_flame_vertex_t){us_sf_add(field->x,us_sf_mul(usurper_circle_vertices[i][0],field->radius)),us_sf_add(field->y,us_sf_mul(usurper_circle_vertices[i][1],field->radius))};
    p->polygon(p->context,circle,64,tint(0.035f));
    us_flame_vertex_t border[64];for(unsigned i=0;i<64;++i)border[i]=(us_flame_vertex_t){us_sf_mul(us_warden_border_directions[i][0],field->radius),us_sf_mul(us_warden_border_directions[i][1],field->radius)};
    line(field,p,border,64,1.5f,tint(0.5f));
    double progress=1-field->age/field->duration;if(progress<0)progress=0;
    arc(field,p,11,-1.57079632679489661923f,(float)(-1.57079632679489661923+6.28318530717958647692*progress),24,2,tint(1));
    if(field->block_flash>0){float angle=us_atan_float(field->block_dx,field->block_dy);arc(field,p,field->radius,(float)(angle-0.6),(float)(angle+0.6),18,5,(us_boundary_color_t){0.75f,0.95f,1,(float)(field->block_flash/0.24)});}
    const us_flame_vertex_t shield[6]={{-6,-5},{6,-5},{5,3},{0,8},{-5,3},{-6,-5}};line(field,p,shield,6,2,tint(1));
    for(unsigned i=0;i<12;++i){float x=us_warden_rim_directions[i][0],y=us_warden_rim_directions[i][1];us_flame_vertex_t mark[2]={{us_sf_mul(x,field->radius-3),us_sf_mul(y,field->radius-3)},{us_sf_mul(x,field->radius+3),us_sf_mul(y,field->radius+3)}};line(field,p,mark,2,1,tint(0.6f));}
}
