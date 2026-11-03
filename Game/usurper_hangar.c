#include "usurper_hangar.h"
#include <string.h>
static double positive(double value){return value<0?0:value;}
static float clamp(float value,float low,float high){return value<low?low:value>high?high:value;}
void us_hangar_birth(int32_t pxq,int32_t pyq,int32_t cxq,int32_t cyq,int32_t *xq,int32_t *yq,unsigned preferred) {
    float px=pxq/65536.0f,py=pyq/65536.0f,cx=cxq/65536.0f,cy=cyq/65536.0f;
    const float half_x=320.0f*0.36f;
    float left=cx-half_x,top=cy-55,right=left+half_x*2,bottom=top+110;
    if(left<125)left=125;
    if(top<125)top=125;
    if(right>675)right=675;
    if(bottom>355)bottom=355;
    float center_x=left+(right-left)*0.5f,center_y=top+(bottom-top)*0.5f;
    float best_x=center_x,best_y=center_y,dx=best_x-px,dy=best_y-py;
    double score=us_fx_sqrt_float(dx*dx+dy*dy);if(score>110)score=110;
    if(preferred){best_x=clamp(*xq/65536.0f,left,right);best_y=clamp(*yq/65536.0f,top,bottom);}
    else for(unsigned i=0;i<4;++i) {
        static const float ax[4]={1,-1,0,0},ay[4]={0,0,1,-1};
        float x=clamp(center_x+ax[i]*110,left,right),y=clamp(center_y+ay[i]*110,top,bottom);
        dx=x-px;dy=y-py;double distance=us_fx_sqrt_float(dx*dx+dy*dy);if(distance>110)distance=110;
        dx=x-center_x;dy=y-center_y;double candidate=distance-us_fx_sqrt_float(dx*dx+dy*dy)*0.15;
        if(candidate>score){score=candidate;best_x=x;best_y=y;}
    }
    *xq=(int32_t)((double)best_x*65536);*yq=(int32_t)((double)best_y*65536);
}
unsigned us_hangar_fighter_slots(const us_hangar_t *b,unsigned total,unsigned owned,unsigned requested) {
    unsigned cap=b->remaining*3u;
    if(!b->active||owned>=cap||total>=36u)return 0;
    if(requested>cap-owned)requested=cap-owned;
    if(requested>36u-total)requested=36u-total;
    return requested;
}
unsigned us_hangar_target_open(const us_hangar_t *b,unsigned target) {
    if(!b->active||target>US_HANGAR_CORE)return 0;
    if(target==US_HANGAR_CORE)return b->exposed;
    return b->part_hp[target]>0&&b->recovery<=0;
}
unsigned us_hangar_part_firing(const us_hangar_t *b,unsigned part) {
    if(!us_hangar_target_open(b,part)||!b->remaining)return 0;
    unsigned nth=(unsigned)(b->attack_clock/4.5)%b->remaining;
    for(unsigned i=0;i<US_HANGAR_PARTS;++i)if(b->part_hp[i]>0){if(!nth)return part==i;--nth;}
    return 0;
}
static void part_position(const us_hangar_t *b,unsigned part,float *x,float *y) {
    float hx,hy,cx,sy;us_fx_direction_float((float)(part*(6.28318530717958647692/3)),&hx,&hy);
    hx*=70;hy*=70;us_fx_direction_float(b->rotation,&cx,&sy);
    *x=b->x+cx*hx-sy*hy;*y=b->y+sy*hx+cx*hy;
}
unsigned us_hangar_target_position(const us_hangar_t *b,unsigned target,int32_t *x,int32_t *y) {
    if(!b->active||target>US_HANGAR_CORE||(target<US_HANGAR_PARTS&&b->part_hp[target]<=0))return 0;
    float px=b->x,py=b->y;if(target<US_HANGAR_PARTS)part_position(b,target,&px,&py);
    *x=(int32_t)((double)px*US_FX_ONE);*y=(int32_t)((double)py*US_FX_ONE);return 1;
}
void us_hangar_init(us_hangar_t *b,uint32_t hp_q16,int32_t x,int32_t y,int32_t px,int32_t py) {
    memset(b,0,sizeof(*b));b->x=x/65536.0f;b->y=y/65536.0f;
    b->rotation=us_fx_angle_float((px-x)/65536.0f,(py-y)/65536.0f);
    b->hp=b->max_hp=hp_q16/65536.0;b->active=1;b->remaining=3;b->flight_clock=2;
    for(unsigned i=0;i<3;++i)b->part_hp[i]=420;
}
void us_hangar_hit(us_hangar_t *b,unsigned target,uint32_t damage_q16) {
    if(!us_hangar_target_open(b,target))return;
    double damage=damage_q16/65536.0;
    if(target<US_HANGAR_PARTS) {
        if(us_hangar_part_firing(b,target))damage*=1.5;
        b->part_hp[target]=positive(b->part_hp[target]-damage);b->part_flash[target]=0.16;
        if(b->part_hp[target]<=0) {
            --b->remaining;++b->breaks;b->hp=positive(b->hp-b->max_hp*0.08);
            b->exposed=1;b->recovery=1.2;
            if(b->hp<=0){b->active=0;b->vx=b->vy=0;}
        }
        return;
    }
    double floor=b->phase?0:b->max_hp*0.5,dealt=damage*1.7*0.9;
    if(dealt>b->hp-floor)dealt=b->hp-floor;
    b->hp=positive(b->hp-positive(dealt));b->flash=0.12;
    if(dealt>0&&b->core_hit_clock<=0)b->core_hit_clock=0.08;
    if(b->hp<=0){b->active=0;b->vx=b->vy=0;}
    if(b->active&&!b->phase&&b->hp<=floor+0.001) {b->phase=1;b->phase_started=b->elapsed;b->attack_clock=0;b->recovery=1;}
}
void us_hangar_step(us_hangar_t *b,int32_t pxq,int32_t pyq,us_hangar_output_t *out) {
    memset(out,0,sizeof(*out));if(!b->active)return;
    const double delta=1.0/60;b->flash=positive(b->flash-delta);b->elapsed+=delta;b->attack_clock+=delta;
    b->core_hit_clock=positive(b->core_hit_clock-delta);b->recovery=positive(b->recovery-delta);
    float px=pxq/65536.0f,py=pyq/65536.0f,dx=px-b->x,dy=py-b->y,distance=us_fx_sqrt_float(dx*dx+dy*dy);
    float ax=distance?dx/distance:0,ay=distance?dy/distance:0;
    float pace=clamp((float)((distance-210)*0.7),-70,28);
    b->vx=ax*pace+ay*20;b->vy=ay*pace-ax*20;
    b->x=clamp(b->x+b->vx*(float)delta,90,710);b->y=clamp(b->y+b->vy*(float)delta,90,390);
    b->rotation=us_fx_turn_float(b->rotation,us_fx_angle_float(ax,ay),delta*0.9);
    b->flight_clock-=delta;
    if(b->flight_clock<=0&&b->remaining>0) {
        b->flight_clock=b->phase?2.6:3.2;
        for(unsigned i=0;i<3;++i)if(b->part_hp[i]>0) {
            part_position(b,i,&out->fighters[out->count].x,&out->fighters[out->count].y);
            out->fighters[out->count++].count=2;
        }
    }
    for(unsigned i=0;i<3;++i)if(b->part_hp[i]>0)b->part_flash[i]=positive(b->part_flash[i]-delta);
}
