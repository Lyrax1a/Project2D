#include "usurper_controller_visual.h"
#include "usurper_softfloat.h"
#include "usurper_atan.h"
#include "usurper_circle_generated.h"
typedef struct {const us_controller_painter_t *p;float x,y,c,s,scale,alpha,dx,dy,dc,ds;} canvas_t;
static us_boundary_color_t color(unsigned rgb){
    return (us_boundary_color_t){((rgb>>16)&255)/255.0f,((rgb>>8)&255)/255.0f,(rgb&255)/255.0f,1};
}
static us_boundary_color_t alpha(us_boundary_color_t c,float a){c.a=a;return c;}
static us_boundary_color_t lerp(us_boundary_color_t a,us_boundary_color_t b,float t){
    return (us_boundary_color_t){us_sf_add(a.r,us_sf_mul(b.r-a.r,t)),us_sf_add(a.g,us_sf_mul(b.g-a.g,t)),us_sf_add(a.b,us_sf_mul(b.b-a.b,t)),us_sf_add(a.a,us_sf_mul(b.a-a.a,t))};
}
static canvas_t canvas(const us_controller_painter_t *p,float x,float y,float rotation,float scale,float opacity){
    canvas_t c={p,x,y,0,0,scale,opacity,0,0,1,0};float cx,sy;us_fx_direction_float(rotation,&cx,&sy);c.c=us_sf_mul(cx,scale);c.s=us_sf_mul(sy,scale);return c;
}
static void transform(canvas_t *c,float x,float y,float rotation){c->dx=x;c->dy=y;us_fx_direction_float(rotation,&c->dc,&c->ds);}
static void world(canvas_t *c,const us_flame_vertex_t *v,us_flame_vertex_t *out,unsigned count){
    for(unsigned i=0;i<count;++i){
        float x=us_sf_add(us_sf_add(c->dx,us_sf_mul(c->dc,v[i].x)),-us_sf_mul(c->ds,v[i].y));
        float y=us_sf_add(us_sf_add(c->dy,us_sf_mul(c->ds,v[i].x)),us_sf_mul(c->dc,v[i].y));
        out[i].x=us_sf_add(us_sf_add(c->x,us_sf_mul(c->c,x)),-us_sf_mul(c->s,y));out[i].y=us_sf_add(us_sf_add(c->y,us_sf_mul(c->s,x)),us_sf_mul(c->c,y));
    }
}
static void poly(canvas_t *c,const us_flame_vertex_t *v,unsigned count,us_boundary_color_t tint){
    us_flame_vertex_t points[65];if(count>65)return;world(c,v,points,count);tint.a=us_sf_mul(tint.a,c->alpha);c->p->polygon(c->p->context,points,count,tint);
}
static void line(canvas_t *c,const us_flame_vertex_t *v,unsigned count,float width,us_boundary_color_t tint,unsigned aa){
    us_flame_vertex_t points[65];if(count>65)return;world(c,v,points,count);tint.a=us_sf_mul(tint.a,c->alpha);c->p->line(c->p->context,points,count,us_sf_mul(width,c->scale),tint,aa);
}
static void segment(canvas_t *c,float x,float y,float tx,float ty,float w,us_boundary_color_t tint,unsigned aa){us_flame_vertex_t v[2]={{x,y},{tx,ty}};line(c,v,2,w,tint,aa);}
static void rect(canvas_t *c,float x,float y,float w,float h,us_boundary_color_t tint,unsigned fill,float width){
    float right=us_sf_add(x,w),bottom=us_sf_add(y,h);us_flame_vertex_t v[5]={{x,y},{right,y},{right,bottom},{x,bottom},{x,y}};
    if(fill)poly(c,v,4,tint);else line(c,v,5,width,tint,0);
}
static void circle(canvas_t *c,float x,float y,float radius,us_boundary_color_t tint){
    us_flame_vertex_t v[64];for(unsigned i=0;i<64;++i)v[i]=(us_flame_vertex_t){us_sf_add(x,us_sf_mul(usurper_circle_vertices[i][0],radius)),us_sf_add(y,us_sf_mul(usurper_circle_vertices[i][1],radius))};poly(c,v,64,tint);
}
static void arc(canvas_t *c,float x,float y,float radius,float from,float to,unsigned count,us_boundary_color_t tint,float width,unsigned aa){
    us_flame_vertex_t v[64];if(count>64||count<2)return;
    const float tau=6.28318530717958647692f;float difference=us_sf_add(to,-from);if(difference>tau)difference=tau;else if(difference<-tau)difference=-tau;
    for(unsigned i=0;i<count;++i){float angle=us_sf_add(us_sf_mul(us_sf_div((float)i,(float)(count-1)),difference),from),cx,sy;us_fx_direction_float(angle,&cx,&sy);v[i]=(us_flame_vertex_t){us_sf_add(x,us_sf_mul(cx,radius)),us_sf_add(y,us_sf_mul(sy,radius))};}line(c,v,count,width,tint,aa);
}
static us_flame_vertex_t local(const canvas_t *c,float x,float y){
    float determinant=us_sf_add(us_sf_mul(c->c,c->c),us_sf_mul(c->s,c->s)),inv=us_sf_div(1,determinant);
    float xx=us_sf_mul(c->c,inv),xy=us_sf_mul(c->s,inv),yx=us_sf_mul(-c->s,inv),yy=us_sf_mul(c->c,inv);
    float ox=-us_sf_add(us_sf_mul(xx,c->x),us_sf_mul(xy,c->y)),oy=-us_sf_add(us_sf_mul(yx,c->x),us_sf_mul(yy,c->y));
    return (us_flame_vertex_t){us_sf_add(us_sf_add(us_sf_mul(xx,x),us_sf_mul(xy,y)),ox),us_sf_add(us_sf_add(us_sf_mul(yx,x),us_sf_mul(yy,y)),oy)};
}
static us_flame_vertex_t rotate(float x,float y,float angle){float cx,sy;us_fx_direction_float(angle,&cx,&sy);return (us_flame_vertex_t){cx*x-sy*y,sy*x+cx*y};}
static void sovereign(canvas_t *c,us_boundary_color_t tint){
    static const us_flame_vertex_t outer[4]={{36,0},{-24,-20},{-12,0},{-24,20}},inner[4]={{17,0},{-11,-8},{-6,0},{-11,8}};
    for(unsigned i=0;i<4;++i){unsigned j=(i+1)%4;us_flame_vertex_t v[5]={outer[i],outer[j],inner[j],inner[i],outer[i]};poly(c,v,4,alpha(tint,i%2?0.7f:0.48f));line(c,v,5,1.3f,tint,1);}circle(c,-4,0,3,color(0xd9f8ff));
}
static void plume(canvas_t *c,float hx,float hy,double clock,us_boundary_color_t tint){
    const double pi=3.14159265358979323846;us_flame_vertex_t v[26];
    for(unsigned layer=0;layer<3;++layer){
        for(unsigned side=0;side<2;++side)for(unsigned step=0;step<13;++step){
            double t=(side?12-step:step)/12.0,wave=us_fx_sin_double(t*pi),width=(3+wave*42)*(1-layer*0.26),curl=us_fx_sin_double(t*18-clock*22+layer)*wave*3;
            float along=(float)(10+t*220*(1-layer*0.14)),across=(float)((side?1:-1)*width+curl);
            v[side*13+step]=(us_flame_vertex_t){hx*along+hy*across,hy*along-hx*across};
        }poly(c,v,26,alpha(lerp(tint,color(0xffffff),(float)(layer*0.3)),(float)(0.14+layer*0.2)));
    }
    for(unsigned i=0;i<18;++i){double t=clock*(1.6+(i%3)*0.15)+i*0.137;t-=(int64_t)t;double wave=us_fx_sin_double(t*pi);float across=(float)(us_fx_sin_double(i*2.4+t*7)*42*t*0.75),along=(float)(12+t*220);
        circle(c,hx*along+hy*across,hy*along-hx*across,(float)(1.5+3*wave),alpha(lerp(tint,color(0xffffff),0.45f),(float)(wave*0.5)));
    }circle(c,hx*12,hy*12,5,alpha(tint,0.4f));circle(c,hx*12,hy*12,2.5f,(us_boundary_color_t){0.9f,1,1,0.95f});
}
static const us_flame_vertex_t wave_hulls[4][6]={
    {{36,0},{-25,-31},{-10,-7},{-6,0},{-10,7},{-25,31}},{{42,0},{-26,-23},{-16,-9},{-11,0},{-16,9},{-26,23}},
    {{31,-12},{14,-29},{-29,-29},{-29,29},{14,29},{31,12}},{{30,-30},{-30,-30},{-30,0},{-30,30},{30,30},{30,0}}
};
static void ring(canvas_t *c,const us_flame_vertex_t *outer,us_boundary_color_t tint){
    us_flame_vertex_t outline[7];for(unsigned i=0;i<6;++i){unsigned j=(i+1)%6;us_flame_vertex_t v[4]={outer[i],outer[j],{outer[j].x*0.52f,outer[j].y*0.52f},{outer[i].x*0.52f,outer[i].y*0.52f}};poly(c,v,4,alpha(tint,(float)(tint.a*0.65)));outline[i]=outer[i];}outline[6]=outer[0];line(c,outline,7,3,tint,1);
}
static void shell(canvas_t *c,unsigned form,us_boundary_color_t tint){
    if(tint.a<=0)return;
    if(form!=4){ring(c,wave_hulls[form],tint);return;}
    for(int sign=-1;sign<=1;sign+=2){us_flame_vertex_t v[6]={{-44,16.0f*sign},{-28,32.0f*sign},{28,32.0f*sign},{44,16.0f*sign},{24,12.0f*sign},{-28,12.0f*sign}};poly(c,v,6,tint);}rect(c,-44,-16,16,32,tint,1,0);
}
static void wave_draw(canvas_t *c,const us_controller_t *b){
    static const unsigned colors[5]={0xa83b52,0xf03a3f,0xc359fb,0xff914d,0xdcdcdc};unsigned form=b->waves.index%5,previous=(form+4)%5;
    double t=b->waves.clock/0.35;if(t<0)t=0;else if(t>1)t=1;double blend=t*t*(3-2*t);
    us_boundary_color_t tint=lerp(color(colors[previous]),color(colors[form]),(float)blend);if(b->flash>0)tint=color(0xffffff);
    if(form==4||previous==4){shell(c,previous,alpha(tint,(float)(1-blend)));shell(c,form,alpha(tint,(float)blend));}
    else{us_flame_vertex_t outer[6];for(unsigned i=0;i<6;++i)outer[i]=(us_flame_vertex_t){us_sf_add(wave_hulls[previous][i].x,us_sf_mul(wave_hulls[form][i].x-wave_hulls[previous][i].x,(float)blend)),us_sf_add(wave_hulls[previous][i].y,us_sf_mul(wave_hulls[form][i].y-wave_hulls[previous][i].y,(float)blend))};ring(c,outer,tint);}
    rect(c,-6,-5,10,10,color(0xc6e5ec),1,0);
    double beat=b->waves.age-(int)(b->waves.age/3.2)*3.2;
    if(form==0){
        if(beat>=0.85&&beat<2.25)for(unsigned slot=0;slot<2;++slot){float hx,hy;us_fx_direction_float((float)(b->waves.age*4.5+slot*3.14159265358979323846),&hx,&hy);us_flame_vertex_t at=local(c,b->x+hx*82,b->y+hy*82);transform(c,at.x,at.y,us_atan_float(at.x,at.y));us_flame_vertex_t v[6]={{-9,-17},{5,-8},{9,0},{5,8},{-9,17},{-3,0}};poly(c,v,6,tint);transform(c,0,0,0);}
        else if(beat<0.85)for(unsigned side=0;side<4;++side)arc(c,0,0,82,(float)(side*3.14159265358979323846*0.5),(float)(side*3.14159265358979323846*0.5+0.8),8,alpha(tint,0.6f),3,0);
    }else if(form==1||form==2){
        us_flame_vertex_t dir=rotate(b->locked_x,b->locked_y,-b->rotation);if(form==2)segment(c,25,0,41,0,7,tint,0);
        if(b->waves.age<(form==1?0.8:0.75))for(int side=form==1?0:-1;side<=(form==1?0:1);++side){us_flame_vertex_t aim=rotate(dir.x,dir.y,(float)(side*0.12));segment(c,aim.x*42,aim.y*42,aim.x*290,aim.y*290,3,alpha(tint,0.65f),0);}
    }else if(form==3){if(b->waves.age>=0.75&&b->waves.age<1.05)rect(c,-8,-8,16,16,color(0xfff0d6),1,0);}
    else for(int side=-1;side<=1;side+=2)rect(c,14,(float)(side*23-3),14,6,color(b->waves.age<1.15?0xffe078:0x7c8d98),1,0);
    transform(c,0,0,-b->rotation);for(unsigned i=0;i<8;++i)rect(c,(float)(-27+(int)i*7),56,5,3,i/8.0<1-b->waves.clock/14?tint:alpha(tint,0.18f),1,0);transform(c,0,0,0);
}
static void core(canvas_t *c,const us_controller_t *b){
    if(b->phase==2){wave_draw(c,b);return;}static const unsigned colors[3]={0xff963e,0xb677ff,0xff477e};us_boundary_color_t tint=color(b->phase?0xc6e5ec:colors[b->form]);sovereign(c,b->flash>0?color(0xffffff):tint);
    if(b->shield>0)arc(c,0,0,53,-1.57079632679489661923f,(float)(-1.57079632679489661923+6.28318530717958647692*b->shield/1500),48,color(0x91d8eb),3,0);
    if(!b->active)return;
    if(!b->phase){us_flame_vertex_t dir=rotate(b->locked_x,b->locked_y,-b->rotation);double clock=b->form_clock-0.6,beat=clock-(int)(clock/2.05)*2.05;
        if(!b->form&&b->form_clock>=0.6&&beat<0.65)segment(c,dir.x*35,dir.y*35,dir.x*378,dir.y*378,3,alpha(tint,0.7f),0);
        if(b->form==1)for(int side=-1;side<=1;side+=2)rect(c,15,(float)(side*26-3),21,6,tint,1,0);
        if(b->form==2&&(b->point_x||b->point_y)&&b->form_clock<1.35){us_flame_vertex_t p=local(c,b->point_x,b->point_y);rect(c,p.x-22,p.y-22,44,44,tint,0,3);}
        if(b->form==2&&b->form_clock>=1.65&&b->form_clock<4.6)plume(c,dir.x,dir.y,b->elapsed,tint);
    }else{
        for(unsigned i=0;i<b->nest_count;++i){const us_controller_nest_t *n=&b->nests[b->nest_order[i]];if(!n->active)continue;us_flame_vertex_t point=local(c,n->x,n->y);float length=us_fx_sqrt_float(point.x*point.x+point.y*point.y),nx=length?point.x/length:0,ny=length?point.y/length:0;segment(c,nx*37,ny*37,point.x-nx*22,point.y-ny*22,3,alpha(color(0x83adc1),0.45f),0);}
        for(unsigned slot=0;slot<6;++slot){float hx,hy;us_fx_direction_float((float)(slot*6.28318530717958647692/6+(b->elapsed-b->phase_started)*0.12-b->rotation),&hx,&hy);arc(c,hx*116,hy*116,22,0,6.28318530717958647692f,24,alpha(color(0x83adc1),0.2f),3,0);}
    }
}
static void preview(canvas_t *c,unsigned species,double flash){
    us_boundary_color_t tint=color(flash>0?(species==US_CONTROLLER_DART?0xd6e3e6:0xffffff):species==US_CONTROLLER_DART?0xf03a3f:species==US_CONTROLLER_GUNNER?0xc359fb:0xdcdcdc);
    if(species==US_CONTROLLER_DART){us_flame_vertex_t v[5]={{7,0},{-4,-5},{-2,0},{-4,5},{7,0}};poly(c,v,4,alpha(tint,0.14f));line(c,v,5,1.5f,tint,1);circle(c,-1,0,2,tint);}
    else if(species==US_CONTROLLER_GUNNER){us_flame_vertex_t v[7]={{-7,-6},{3,-6},{6,-3},{6,3},{3,6},{-7,6},{-7,-6}};poly(c,v,6,alpha(tint,0.14f));line(c,v,7,1.5f,tint,1);segment(c,2,0,9,0,2,tint,0);circle(c,3,0,1,tint);}
    else{for(int side=-1;side<=1;side+=2){us_flame_vertex_t v[6]={{-11,4.0f*side},{-7,8.0f*side},{7,8.0f*side},{11,4.0f*side},{6,3.0f*side},{-7,3.0f*side}};poly(c,v,6,tint);}rect(c,-11,-4,4,8,tint,1,0);rect(c,2,-1,4,2,alpha(tint,0.65f),1,0);}
}
void us_controller_draw(const us_controller_t *b,const us_controller_painter_t *p){
    canvas_t c=canvas(p,b->x,b->y,b->rotation,1,1);core(&c,b);
    for(unsigned i=0;i<b->nest_count;++i){const us_controller_nest_t *n=&b->nests[b->nest_order[i]];if(!n->active)continue;float scale=(float)(1+n->hit_kick*0.1);c=canvas(p,n->x,n->y,n->rotation,scale,1);us_boundary_color_t tint=color(n->flash>0?0xffffff:0x91d8eb);
        if(!n->hatched){circle(&c,0,0,21,alpha(tint,0.1f));arc(&c,0,0,21,0,6.28318530717958647692f,40,alpha(tint,0.25f),3,0);double progress=n->age/n->duration;if(progress>1)progress=1;else if(progress<0)progress=0;arc(&c,0,0,21,-1.57079632679489661923f,(float)(-1.57079632679489661923+6.28318530717958647692*progress),40,tint,3,0);}
        else if(n->firing&&n->fire_age<0.75){segment(&c,16,0,430,0,3,alpha(color(0xffcc80),0.55f),0);double progress=n->fire_age/0.75;if(progress>1)progress=1;else if(progress<0)progress=0;arc(&c,0,0,21,0,(float)(6.28318530717958647692*progress),32,color(0xffcc80),3,0);}
        c=canvas(p,n->x,n->y,n->rotation,(float)(scale*1.3),n->hatched?1:0.45f);preview(&c,n->species,n->flash);
    }
    for(unsigned i=0;i<b->fighter_count;++i){const us_controller_fighter_t *f=&b->fighters[b->fighter_order[i]];if(!f->present||f->queued)continue;float scale=0.65f*(float)(1+f->hit_kick*0.1);c=canvas(p,f->x,f->y,f->rotation,scale,1);preview(&c,US_CONTROLLER_DART,f->flash);}
}
