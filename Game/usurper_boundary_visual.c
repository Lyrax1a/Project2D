#include "usurper_boundary_visual.h"
#include "usurper_softfloat.h"
static void direction(float rotation,float *cx,float *sy) {
    const float pi=3.14159265358979323846f;
    if(!rotation){*cx=1;*sy=0;}
    else if(rotation==pi*0.5f){*cx=-4.371138828673793e-08f;*sy=1;}
    else if(rotation==-pi*0.5f){*cx=-4.371138828673793e-08f;*sy=-1;}
    else if(rotation==pi){*cx=-1;*sy=-8.742277657347586e-08f;}
    else us_fx_direction_float(rotation,cx,sy);
}
typedef struct { const us_boundary_painter_t *p; float x,y,rotation,dx,dy,dr,cx,sy,dc,ds; } canvas_t;
static canvas_t canvas(const us_boundary_painter_t *p,float x,float y,float rotation) {
    canvas_t c={p,x,y,rotation,0,0,0,0,0,1,0};direction(rotation,&c.cx,&c.sy);return c;
}
static float limit(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static us_boundary_color_t color(unsigned rgb) {
    #define KNOWN_RGB(value) case value:return (us_boundary_color_t){((value>>16)&255)/255.0f,((value>>8)&255)/255.0f,(value&255)/255.0f,1};
    switch(rgb) {
        KNOWN_RGB(0x312831) KNOWN_RGB(0x352f42) KNOWN_RGB(0x382934) KNOWN_RGB(0x382c40)
        KNOWN_RGB(0x627888) KNOWN_RGB(0x634252) KNOWN_RGB(0x652d46) KNOWN_RGB(0x7a4c61)
        KNOWN_RGB(0xd6e3e6) KNOWN_RGB(0xff627b) KNOWN_RGB(0xff914d) KNOWN_RGB(0xffe078)
        KNOWN_RGB(0xfff0bd) KNOWN_RGB(0xfff2ba) KNOWN_RGB(0xffffff)
        default:break;
    }
    #undef KNOWN_RGB
    return (us_boundary_color_t){((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, 1};
}
static us_boundary_color_t alpha(us_boundary_color_t c, float a) { c.a = a; return c; }
static us_boundary_color_t dark(us_boundary_color_t c, float amount) { c.r *= 1 - amount; c.g *= 1 - amount; c.b *= 1 - amount; return c; }
static us_boundary_color_t lerp(us_boundary_color_t a, us_boundary_color_t b, float t) {
    return (us_boundary_color_t){a.r + (b.r-a.r)*t, a.g + (b.g-a.g)*t, a.b + (b.b-a.b)*t, a.a + (b.a-a.a)*t};
}
static void transform(canvas_t *c, float dx, float dy, float rotation) {
    c->dx=dx;c->dy=dy;c->dr=rotation;direction(rotation,&c->dc,&c->ds);
}
static void world(const canvas_t *c, const us_flame_vertex_t *v, us_flame_vertex_t *out, unsigned count) {
    unsigned local_identity=c->dc==1&&c->ds==0&&c->dx==0&&c->dy==0;
    unsigned world_identity=c->cx==1&&c->sy==0;
    for (unsigned i=0;i<count;++i) {
        float x=v[i].x,y=v[i].y;
        if(!local_identity) {
            x=us_sf_add(us_sf_add(c->dx,us_sf_mul(c->dc,v[i].x)),-us_sf_mul(c->ds,v[i].y));
            y=us_sf_add(us_sf_add(c->dy,us_sf_mul(c->ds,v[i].x)),us_sf_mul(c->dc,v[i].y));
        }
        if(world_identity){out[i].x=us_sf_add(c->x,x);out[i].y=us_sf_add(c->y,y);}
        else {
            out[i].x=us_sf_add(us_sf_add(c->x,us_sf_mul(c->cx,x)),-us_sf_mul(c->sy,y));
            out[i].y=us_sf_add(us_sf_add(c->y,us_sf_mul(c->sy,x)),us_sf_mul(c->cx,y));
        }
    }
}
static void polygon(canvas_t *c,const us_flame_vertex_t *v,unsigned count,us_boundary_color_t tint) {
    us_flame_vertex_t points[16]; if(count>16) return; world(c,v,points,count); c->p->polygon(c->p->context,points,count,tint);
}
static void line(canvas_t *c,const us_flame_vertex_t *v,unsigned count,float width,us_boundary_color_t tint) {
    us_flame_vertex_t points[16]; if(count>16) return; world(c,v,points,count); c->p->line(c->p->context,points,count,width,tint);
}
static void segment(canvas_t *c,float x,float y,float tx,float ty,float width,us_boundary_color_t tint) {
    us_flame_vertex_t points[2]={{x,y},{tx,ty}}; line(c,points,2,width,tint);
}
static void rect(canvas_t *c,float x,float y,float w,float h,us_boundary_color_t tint) {
    float right=us_sf_add(x,w),bottom=us_sf_add(y,h);us_flame_vertex_t points[4]={{x,y},{right,y},{right,bottom},{x,bottom}}; polygon(c,points,4,tint);
}
static void outline(canvas_t *c,float x,float y,float w,float h,float width,us_boundary_color_t tint) {
    float right=us_sf_add(x,w),bottom=us_sf_add(y,h);us_flame_vertex_t points[5]={{x,y},{right,y},{right,bottom},{x,bottom},{x,y}}; line(c,points,5,width,tint);
}
static us_flame_vertex_t local(const canvas_t *c,float x,float y) {
    float cx=c->cx,sy=c->sy;float determinant=us_sf_add(us_sf_mul(cx,cx),us_sf_mul(sy,sy)),inv=us_sf_div(1,determinant);
    float xx=us_sf_mul(cx,inv),xy=us_sf_mul(sy,inv),yx=us_sf_mul(-sy,inv),yy=us_sf_mul(cx,inv);
    float ox=-us_sf_add(us_sf_mul(xx,c->x),us_sf_mul(xy,c->y)),oy=-us_sf_add(us_sf_mul(yx,c->x),us_sf_mul(yy,c->y));
    return (us_flame_vertex_t){us_sf_add(us_sf_add(us_sf_mul(xx,x),us_sf_mul(xy,y)),ox),us_sf_add(us_sf_add(us_sf_mul(yx,x),us_sf_mul(yy,y)),oy)};
}
static void arm_draw(const us_boundary_t *b,const us_boundary_part_t *a,const us_boundary_painter_t *p) {
    canvas_t c=canvas(p,a->x,a->y,a->rotation);
    us_boundary_color_t tint=color(a->flash>0?0xffffff:0xff627b);
    float tension=a->state==US_BOUNDARY_WARN?limit((float)(1-a->clock/(a->fast?0.28:0.48))):0;
    if(a->fast&&a->state==US_BOUNDARY_WARN&&a->clock<0.08) tint=lerp(tint,color(0xffffff),0.65f);
    us_flame_vertex_t root=local(&c,a->mount_x,a->mount_y), elbow={root.x*0.5f,root.y*0.5f+(18+12*tension)*(a->side%2?-1:1)};
    us_flame_vertex_t bones[3]={root,elbow,{0,0}}; line(&c,bones,3,12,color(0x382c40)); line(&c,bones,3,5,tint);
    rect(&c,root.x-6,root.y-6,12,12,tint); rect(&c,elbow.x-6,elbow.y-6,12,12,tint); rect(&c,-11,-10,12,20,tint);
    for(int sign=-1;sign<=1;sign+=2) {
        float opening=a->captive>=0?6+(1-6)*(float)a->crush_progress:11+(5-11)*tension;
        us_flame_vertex_t points[3]={{-2,(float)(sign*10)},{14,(float)(sign*14)},{21,sign*opening}}; line(&c,points,3,4,tint);
    }
    segment(&c,-13,-22,13,-22,3,color(0x352f42)); segment(&c,-13,-22,(float)(-13+26*a->hp/300),-22,3,color(0xffe078));
    if((a->state==US_BOUNDARY_WARN||a->state==US_BOUNDARY_REACH)&&a->tracked_unit>=0&&b->units[a->tracked_unit].valid) {
        us_flame_vertex_t at=local(&c,b->units[a->tracked_unit].x,b->units[a->tracked_unit].y);
        transform(&c,at.x,at.y,-a->rotation); outline(&c,-14,-14,28,28,1.5f,color(0xff627b));
    }
}
static void module_draw(const us_boundary_t *b,const us_boundary_part_t *m,const us_boundary_painter_t *p) {
    canvas_t c=canvas(p,m->x,m->y,0); us_boundary_color_t tint=color(m->flash>0?0xffffff:0xff627b);
    static const float dx[4]={1,0,-1,0},dy[4]={0,1,0,-1}; transform(&c,0,0,us_fx_angle_float(dx[m->side],dy[m->side]));
    for(int sign=-1;sign<=1;sign+=2) rect(&c,-17,(float)(sign*12-3),7,6,color(0x7a4c61));
    if(m->role==1) {
        us_flame_vertex_t base[6]={{-13,-13},{3,-13},{15,-5},{15,5},{3,13},{-13,13}}; polygon(&c,base,6,color(0x312831));
        for(int sign=-1;sign<=1;sign+=2) {
            us_flame_vertex_t armor[6]={{-13,15.0f*sign},{3,15.0f*sign},{16,6.0f*sign},{8,6.0f*sign},{0,10.0f*sign},{-13,10.0f*sign}}; polygon(&c,armor,6,tint);
        }
        rect(&c,-13,-10,4,20,dark(tint,0.35f));
        float pulse=(float)(0.8+0.2*us_fx_sin_double(b->elapsed*3)); us_boundary_color_t glow=color(0xffe078); glow.r*=pulse;glow.g*=pulse;glow.b*=pulse;
        us_flame_vertex_t core[6]={{-5,-5},{2,-8},{8,-4},{8,4},{2,8},{-5,5}}; polygon(&c,core,6,glow); rect(&c,0,-4,3,8,color(0xfff2ba));
    } else {
        us_flame_vertex_t base[8]={{-13,-11},{-7,-15},{6,-15},{12,-8},{12,8},{6,15},{-7,15},{-13,11}}; polygon(&c,base,8,color(0x634252));
        for(int sign=-1;sign<=1;sign+=2) rect(&c,-6,(float)(sign*12-2),11,4,dark(tint,0.2f));
        transform(&c,0,0,us_fx_angle_float(m->heading_x,m->heading_y));
        float recoil=m->state==2?(float)(3*us_fx_sin_double(limit((float)(1-m->clock/0.25))*3.14159265358979323846)):0;
        rect(&c,-10-recoil,-6,20,12,color(0x382934));
        for(int sign=-1;sign<=1;sign+=2) {
            us_flame_vertex_t barrel[6]={{-9-recoil,8.0f*sign},{14-recoil,8.0f*sign},{22-recoil,4.0f*sign},{22-recoil,2.0f*sign},{-3-recoil,2.0f*sign},{-9-recoil,4.0f*sign}};
            polygon(&c,barrel,6,tint);
        }
        float charge=m->state==1?limit((float)(1-m->clock/0.8)):m->state==2?1:0;
        rect(&c,1-recoil,-2,18,4,lerp(color(0x652d46),color(0xfff0bd),charge));
        transform(&c,0,0,0);
        if(m->state) {
            us_flame_vertex_t beam[2]={{m->heading_x*22,m->heading_y*22},{m->beam_x-m->x,m->beam_y-m->y}};
            us_boundary_color_t warning=color(0xff914d);
            if(m->state==1) { double wave=us_fx_sin_double(m->clock*16); line(&c,beam,2,1,alpha(warning,(float)(0.18+0.42*wave*wave))); }
            else { line(&c,beam,2,8,alpha(warning,(float)(0.22*(0.84+0.16*us_fx_sin_double(m->clock*47))))); line(&c,beam,2,3.4f,alpha(warning,0.88f)); line(&c,beam,2,1.35f,alpha(color(0xd6e3e6),0.98f)); }
        }
    }
    transform(&c,0,0,0);
    double max_hp=m->role==1?420:240;
    if(m->hp/max_hp<1) { rect(&c,-14,-23,28,3,color(0x352f42)); rect(&c,-14,-23,(float)(28*m->hp/max_hp),3,color(0xffe078)); }
}
static void walls(const us_boundary_t *b,const us_boundary_painter_t *p) {
    canvas_t c=canvas(p,400,240,0); float r[4]; us_boundary_safe_rect(b,r);
    us_flame_vertex_t corners[4]={{r[0]-400,r[1]-240},{r[2]-400,r[1]-240},{r[2]-400,r[3]-240},{r[0]-400,r[3]-240}};
    const int sx[4]={-1,1,1,-1},sy[4]={-1,-1,1,1}; const unsigned sides[4]={1,2,3,0};
    us_flame_vertex_t outer[4],backing[4];
    for(unsigned i=0;i<4;++i) { outer[i]=(us_flame_vertex_t){corners[i].x+sx[i]*1600,corners[i].y+sy[i]*1600};backing[i]=(us_flame_vertex_t){corners[i].x+sx[i]*14,corners[i].y+sy[i]*14}; }
    for(unsigned i=0;i<4;++i) {
        unsigned n=(i+1)%4,side=sides[i];us_boundary_color_t tint=color(b->broken[side]?0x627888:0xff627b);
        us_flame_vertex_t area[4]={corners[i],corners[n],outer[n],outer[i]}; polygon(&c,area,4,alpha(tint,0.09f));
        area[2]=backing[n];area[3]=backing[i];polygon(&c,area,4,alpha(tint,0.23f));
        segment(&c,corners[i].x,corners[i].y,corners[n].x,corners[n].y,4,tint);
        segment(&c,corners[i].x,corners[i].y,outer[i].x,outer[i].y,4,color(b->broken[side]&&b->broken[i]?0x627888:0xff627b));
        float dx=corners[n].x-corners[i].x,dy=corners[n].y-corners[i].y,length=us_fx_sqrt_float(dx*dx+dy*dy),nx=length?dx/length:0,ny=length?dy/length:0;
        float ox,oy,cx,ss;direction((float)(-3.14159265358979323846*0.5),&cx,&ss);ox=cx*nx-ss*ny;oy=ss*nx+cx*ny;
        for(unsigned step=0;step<(unsigned)(length/28);++step) {
            float distance=(float)(14+step*28),x=corners[i].x+nx*distance,y=corners[i].y+ny*distance;
            segment(&c,x,y,x+ox*8,y+oy*8,4,alpha(tint,0.8f));
        }
    }
    if(b->exposed) {
        us_flame_vertex_t points[7];for(unsigned i=0;i<6;++i) {float x,y;us_fx_direction_float((float)(i*(6.28318530717958647692/6)+b->elapsed),&x,&y);points[i]=(us_flame_vertex_t){x*23,y*23};}
        polygon(&c,points,6,alpha(color(0xff627b),0.25f));points[6]=points[0];line(&c,points,7,4,color(b->flash>0?0xffffff:0xff627b));rect(&c,-7,-7,14,14,color(0xffe078));
    }
}
void us_boundary_draw(const us_boundary_t *b,const us_boundary_painter_t *p) {
    if(!b->active) return;
    walls(b,p);
    for(unsigned i=0;i<US_BOUNDARY_PARTS;++i) if(b->parts[i].active) {
        if(!b->parts[i].role) arm_draw(b,&b->parts[i],p);else module_draw(b,&b->parts[i],p);
    }
}
