#include "usurper_warden_hull_visual.h"
#include "usurper_softfloat.h"
#include "usurper_fixed.h"
#include "usurper_circle_generated.h"
#include "usurper_warden_directions_generated.h"
void us_warden_status_hit(us_warden_status_t *status){status->flash=0.085;status->kick=1;}
void us_warden_status_step(us_warden_status_t *status,double delta){status->time+=delta;status->flash=status->flash>delta?status->flash-delta:0;status->kick=status->kick>delta*11?status->kick-delta*11:0;status->scale=(float)(1+status->kick*0.1);}
static us_boundary_color_t body(const us_warden_hull_view_t *v){return v->flash?(us_boundary_color_t){1,1,1,1}:(us_boundary_color_t){69/255.0f,122/255.0f,246/255.0f,1};}
static us_boundary_color_t alpha(us_boundary_color_t c,float value){c.a=value;return c;}
typedef struct {float xx,xy,yx,yy;} basis_t;
typedef struct {const us_controller_painter_t *draw;unsigned local;} hull_painter_t;
static basis_t basis(const us_warden_hull_view_t *v){float cx,sy;us_fx_direction_float(v->rotation,&cx,&sy);return (basis_t){us_sf_mul(cx,v->scale),us_sf_mul(sy,v->scale),us_sf_mul(-sy,v->scale),us_sf_mul(cx,v->scale)};}
static us_flame_vertex_t world(const us_warden_hull_view_t *v,basis_t b,us_flame_vertex_t p){return (us_flame_vertex_t){us_sf_add(us_sf_add(us_sf_mul(b.xx,p.x),us_sf_mul(b.yx,p.y)),v->x),us_sf_add(us_sf_add(us_sf_mul(b.xy,p.x),us_sf_mul(b.yy,p.y)),v->y)};}
static void primitive(const us_warden_hull_view_t *v,const hull_painter_t *p,const us_flame_vertex_t *local,unsigned count,float width,us_boundary_color_t color,unsigned aa,unsigned line){
    us_flame_vertex_t points[66];basis_t b=basis(v);for(unsigned i=0;i<count;++i)points[i]=p->local?local[i]:world(v,b,local[i]);
    if(line)p->draw->line(p->draw->context,points,count,p->local?width:us_sf_mul(width,v->scale),color,aa);else p->draw->polygon(p->draw->context,points,count,color);
}
static void circle(const us_warden_hull_view_t *v,const hull_painter_t *p,float radius,us_boundary_color_t color){us_flame_vertex_t points[64];for(unsigned i=0;i<64;++i)points[i]=(us_flame_vertex_t){us_sf_mul(usurper_circle_vertices[i][0],radius),us_sf_mul(usurper_circle_vertices[i][1],radius)};primitive(v,p,points,64,0,color,0,0);}
static void arc(const us_warden_hull_view_t *v,const hull_painter_t *p,float radius,float from,float to,unsigned count,float width,us_boundary_color_t color){
    us_flame_vertex_t points[64];float difference=us_sf_add(to,-from),tau=6.28318530717958647692f;if(difference>tau)difference=tau;else if(difference<-tau)difference=-tau;
    for(unsigned i=0;i<count;++i){float angle=us_sf_add(us_sf_mul(us_sf_div((float)i,(float)(count-1)),difference),from),x,y;us_fx_direction_float(angle,&x,&y);points[i]=(us_flame_vertex_t){us_sf_mul(x,radius),us_sf_mul(y,radius)};}
    primitive(v,p,points,count,width,color,1,1);
}
static us_flame_vertex_t add(us_flame_vertex_t a,us_flame_vertex_t b){return (us_flame_vertex_t){us_sf_add(a.x,b.x),us_sf_add(a.y,b.y)};}
static us_flame_vertex_t mul(us_flame_vertex_t a,float b){return (us_flame_vertex_t){us_sf_mul(a.x,b),us_sf_mul(a.y,b)};}
static us_flame_vertex_t lerp(us_flame_vertex_t a,us_flame_vertex_t b,float t){return add(a,mul((us_flame_vertex_t){us_sf_add(b.x,-a.x),us_sf_add(b.y,-a.y)},t));}
static us_flame_vertex_t rotated(double angle){us_flame_vertex_t p;us_fx_direction_float((float)angle,&p.x,&p.y);return p;}
static void slow(const us_warden_hull_view_t *v,const hull_painter_t *p){
    if(!v->slow)return;
    double value=v->status_time*18;int step=(int)value;if(value<step)--step;
    for(int i=0;i<4;++i){
        double angle=((step*5+i*11)%31)/31.0*6.28318530717958647692;
        us_flame_vertex_t tangent=rotated(angle+1.57079632679489661923),start=mul(mul(rotated(angle),10),0.82f);
        us_flame_vertex_t end=mul(mul(rotated(angle+1.45+0.18*us_fx_sin_double(step+i)),10),0.74f);
        us_flame_vertex_t middle=add(mul(add(start,end),0.5f),mul(mul(tangent,10),i%2==0?0.18f:-0.16f));
        us_flame_vertex_t points[5]={start,add(lerp(start,middle,0.46f),mul(tangent,-1.5f)),middle,add(lerp(middle,end,0.52f),mul(tangent,1.2f)),end};
        us_boundary_color_t color=i%2==0?(us_boundary_color_t){175/255.0f,124/255.0f,1,1}:(us_boundary_color_t){59/255.0f,185/255.0f,214/255.0f,1};
        primitive(v,p,points,5,1,alpha(color,0.7f),0,1);
        if(i<2){us_flame_vertex_t branch[2]={middle,add(middle,mul(mul(tangent,10),0.22f))};primitive(v,p,branch,2,1,alpha(color,0.48f),0,1);}
    }
}
static void hull_draw(const us_warden_hull_view_t *v,const hull_painter_t *p){
    us_boundary_color_t color=body(v);circle(v,p,9,alpha(color,0.14f));arc(v,p,9,0,6.28318530717958647692f,28,1.5f,color);
    const us_flame_vertex_t diamond[5]={{0,-5},{5,0},{0,5},{-5,0},{0,-5}};primitive(v,p,diamond,5,1,color,1,1);
    slow(v,p);
}
void us_warden_capture_draw(float x,float y,double progress,const us_controller_painter_t *p){
    us_flame_vertex_t line[2]={{us_sf_add(x,-9),us_sf_add(y,-15)},{us_sf_add(x,9),us_sf_add(y,-15)}};
    p->line(p->context,line,2,2,(us_boundary_color_t){0.1f,0.15f,0.2f,1},0);
    line[1].x=us_sf_add(line[0].x,(float)(18*progress));p->line(p->context,line,2,2,(us_boundary_color_t){57/255.0f,189/255.0f,1,1},0);
}
void us_warden_marker_draw(float x,float y,unsigned front,const us_controller_painter_t *p){
    us_warden_hull_view_t view={.x=x,.y=y,.scale=1};hull_painter_t draw={p,0};arc(&view,&draw,(float)(front?13:12),0,6.28318530717958647692f,24,front?0.85f:0.65f,front?(us_boundary_color_t){1,0.85f,0.35f,0.95f}:(us_boundary_color_t){1,1,1,1});
}
static void escort_draw(const us_warden_hull_view_t *v,const hull_painter_t *p){
    if(v->disruption>0||!v->alive||v->supply||v->icon)return;
    us_boundary_color_t color=body(v);
    if(v->escort_valid&&v->escort_alive){
        float dx=us_sf_add(v->escort_x,-v->x),dy=us_sf_add(v->escort_y,-v->y);if(us_fx_sqrt_float(us_sf_add(us_sf_mul(dx,dx),us_sf_mul(dy,dy)))<v->radius){
            /* Same inverse/global transform pair as aura.to_local(). */
            basis_t b=basis(v);float determinant=us_sf_add(us_sf_mul(b.xx,b.yy),-us_sf_mul(b.xy,b.yx)),inverse=us_sf_div(1,determinant);
            basis_t inv={us_sf_mul(b.yy,inverse),us_sf_mul(-b.xy,inverse),us_sf_mul(-b.yx,inverse),us_sf_mul(b.xx,inverse)};
            float ox=us_sf_add(us_sf_mul(inv.xx,-v->x),us_sf_mul(inv.yx,-v->y)),oy=us_sf_add(us_sf_mul(inv.xy,-v->x),us_sf_mul(inv.yy,-v->y));
            us_flame_vertex_t link[2]={{0,0},{us_sf_add(us_sf_add(us_sf_mul(inv.xx,v->escort_x),us_sf_mul(inv.yx,v->escort_y)),ox),us_sf_add(us_sf_add(us_sf_mul(inv.xy,v->escort_x),us_sf_mul(inv.yy,v->escort_y)),oy)}};
            primitive(v,p,link,2,1,alpha(color,0.6f),1,1);
        }
    }
}
static void aura_draw(const us_warden_hull_view_t *v,const hull_painter_t *p){
    if(v->disruption>0||!v->alive||v->supply||v->icon)return;
    us_boundary_color_t color=body(v);circle(v,p,v->radius,alpha(color,0.025f));
    us_flame_vertex_t border[64];for(unsigned i=0;i<64;++i)border[i]=(us_flame_vertex_t){us_sf_mul(us_warden_border_directions[i][0],v->radius),us_sf_mul(us_warden_border_directions[i][1],v->radius)};
    primitive(v,p,border,64,1,alpha(color,0.055f),1,1);
    if(v->material&&v->progress>0){
        us_flame_vertex_t sector[66]={{0,0}};for(unsigned i=0;i<65;++i){float x,y;us_fx_direction_float((float)(-1.57079632679489661923+6.28318530717958647692*v->progress*i/64),&x,&y);sector[i+1]=(us_flame_vertex_t){us_sf_mul(x,v->radius),us_sf_mul(y,v->radius)};}
        primitive(v,p,sector,66,0,alpha(v->controlled,0.025f),0,0);
        arc(v,p,v->radius,-1.57079632679489661923f,(float)(-1.57079632679489661923+6.28318530717958647692*v->progress),64,1,alpha(v->controlled,0.45f));
    }
    escort_draw(v,p);
}
void us_warden_hull_draw(const us_warden_hull_view_t *v,const us_controller_painter_t *p){hull_painter_t draw={p,0};hull_draw(v,&draw);}
void us_warden_aura_draw(const us_warden_hull_view_t *v,const us_controller_painter_t *p){hull_painter_t draw={p,0};aura_draw(v,&draw);}
void us_warden_hull_draw_local(const us_warden_hull_view_t *v,const us_controller_painter_t *p){hull_painter_t draw={p,1};hull_draw(v,&draw);}
void us_warden_aura_draw_local(const us_warden_hull_view_t *v,const us_controller_painter_t *p){hull_painter_t draw={p,1};aura_draw(v,&draw);}
void us_warden_slow_draw_local(const us_warden_hull_view_t *v,const us_controller_painter_t *p){hull_painter_t draw={p,1};slow(v,&draw);}
void us_warden_escort_draw_local(const us_warden_hull_view_t *v,const us_controller_painter_t *p){hull_painter_t draw={p,1};escort_draw(v,&draw);}
us_boundary_color_t us_conversion_color(us_boundary_color_t color,float local_x,float progress,float low,float high,us_boundary_color_t tint,float floor){
    unsigned fill=local_x<=us_sf_add(us_sf_mul(low,us_sf_add(1,-progress)),us_sf_mul(high,progress));if(progress>=0.999f)fill=1;if(progress<=0)fill=0;
    if(fill){float brightness=color.r>color.g?color.r:color.g;if(color.b>brightness)brightness=color.b;if(floor>brightness)brightness=floor;color.r=us_sf_mul(tint.r,brightness);color.g=us_sf_mul(tint.g,brightness);color.b=us_sf_mul(tint.b,brightness);}
    return color;
}
void us_conversion_mask_init(us_conversion_mask_t *mask,float x,float y,float rotation,float scale,float camera_x,float camera_y,float progress,float low,float high){
    mask->mode=progress<=0?0:progress>=0.999f?2:1;
    us_warden_hull_view_t view={.x=x,.y=y,.rotation=rotation,.scale=scale};basis_t b=basis(&view);
    float det=us_sf_add(us_sf_mul(b.xx,b.yy),-us_sf_mul(b.xy,b.yx)),inv=us_sf_div(1,det);
    float xx=us_sf_mul(b.yy,inv),yx=us_sf_mul(-b.yx,inv);
    mask->a=us_sf_div(xx,1.6f);mask->b=us_sf_div(yx,1.6f);
    mask->c=us_sf_add(us_sf_add(us_sf_mul(xx,us_sf_add(camera_x,-320)),us_sf_mul(yx,us_sf_add(camera_y,-180))),us_sf_add(us_sf_mul(xx,-x),us_sf_mul(yx,-y)));
    mask->cut=us_sf_add(us_sf_mul(low,us_sf_add(1,-progress)),us_sf_mul(high,progress));
}
static unsigned mask_filled(const us_conversion_mask_t *mask,int x,int y){return us_sf_add(us_sf_add(us_sf_mul(mask->a,x+0.5f),us_sf_mul(mask->b,y+0.5f)),mask->c)<=mask->cut;}
unsigned us_conversion_mask_split(const us_conversion_mask_t *mask,int x,int y,unsigned width,unsigned *left_filled){
    *left_filled=mask->mode==2;if(!mask->mode||mask->mode==2||!width)return width;
    *left_filled=mask_filled(mask,x,y);unsigned last=mask_filled(mask,x+(int)width-1,y);if(*left_filled==last)return width;
    unsigned low=1,high=width;while(low<high){unsigned middle=low+(high-low)/2;if(mask_filled(mask,x+(int)middle,y)==*left_filled)low=middle+1;else high=middle;}
    return low;
}
