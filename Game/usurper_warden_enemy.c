#include "usurper_warden_enemy.h"
#include "usurper_fixed.h"
#include "usurper_softfloat.h"
#include <string.h>
static float distance(float x,float y){return us_fx_sqrt_float(us_sf_add(us_sf_mul(x,x),us_sf_mul(y,y)));}
static void direction(float x,float y,float *dx,float *dy){float length=distance(x,y);*dx=length?us_sf_div(x,length):0;*dy=length?us_sf_div(y,length):0;}
void us_warden_enemy_init(us_warden_ai_t *warden,float x,float y,float radius){memset(warden,0,sizeof(*warden));warden->x=x;warden->y=y;warden->radius=radius;}
void us_warden_enemy_step(us_warden_ai_t *warden,float px,float py,float speed,double action_rate,unsigned alive,unsigned target_valid,
    us_ward_actor_t *actors,unsigned count,const us_ward_actor_t *leader,void (*move)(void *,us_warden_ai_t *),void *context){
    if(!alive||!target_valid)return;
    const double delta=1.0/60;warden->disruption=warden->disruption>delta?warden->disruption-delta:0;warden->aura_tick-=delta*action_rate;
    if(warden->aura_tick<=0){
        warden->escort_valid=0;
        if(leader){warden->escort_id=leader->id;warden->escort_generation=leader->generation;warden->escort_valid=1;}
        else{float nearest=3.40282346638528859812e38f;
            for(unsigned i=0;i<count;++i){const us_ward_actor_t *actor=&actors[i];if(!actor->alive||actor->kind!=US_WARD_ACTOR_NORMAL)continue;
                float dx=actor->x-warden->x,dy=actor->y-warden->y,squared=us_sf_add(us_sf_mul(dx,dx),us_sf_mul(dy,dy));
                if(squared<nearest){nearest=squared;warden->escort_id=actor->id;warden->escort_generation=actor->generation;warden->escort_valid=1;}}
        }
    }
    float destination_x=px,destination_y=py;
    if(warden->escort_valid){
        const us_ward_actor_t *escort=leader&&leader->id==warden->escort_id&&leader->generation==warden->escort_generation?leader:NULL;
        if(!escort)for(unsigned i=0;i<count;++i)if(actors[i].id==warden->escort_id&&actors[i].generation==warden->escort_generation){escort=&actors[i];break;}
        if(escort&&escort->alive){float dx,dy;direction(px-escort->x,py-escort->y,&dx,&dy);destination_x=us_sf_add(escort->x,us_sf_mul(dx,24));destination_y=us_sf_add(escort->y,us_sf_mul(dy,24));}
    }
    float dx,dy;direction(destination_x-warden->x,destination_y-warden->y,&dx,&dy);
    double weight=distance(destination_x-warden->x,destination_y-warden->y)/24.0;if(weight>1)weight=1;
    float vx=us_sf_mul(us_sf_mul(dx,speed),(float)weight),vy=us_sf_mul(us_sf_mul(dy,speed),(float)weight);
    const float gain=0.036002587527036666870f; /* float32(1-exp(-2.2/60)). */
    warden->vx=us_sf_add(warden->vx,us_sf_mul(gain,us_sf_add(vx,-warden->vx)));
    warden->vy=us_sf_add(warden->vy,us_sf_mul(gain,us_sf_add(vy,-warden->vy)));
    if(move)move(context,warden);
    if(warden->aura_tick<=0){warden->aura_tick=0.18;warden->protected_count=0;
        for(unsigned i=0;i<count;++i){us_ward_actor_t *actor=&actors[i];if(warden->disruption>0||!actor->alive||actor->kind==US_WARD_ACTOR_WARDEN||actor->kind==US_WARD_ACTOR_BOSS)continue;
            if(distance(actor->x-warden->x,actor->y-warden->y)<warden->radius){if(actor->ward<0.3)actor->ward=0.3;++warden->protected_count;}}
    }
}
void us_warden_enemy_counter(us_warden_ai_t *warden,uint32_t self_id,us_ward_actor_t *actors,unsigned count,
    unsigned has_parent,unsigned rail_tag,unsigned source_rail,unsigned source_other_rail,double duration){
    unsigned upgraded=duration>0;if(source_rail&&duration<1.2)duration=1.2;
    if(!(rail_tag||source_rail||source_other_rail)||duration<=0)return;
    if(!has_parent){if(warden->disruption<duration)warden->disruption=duration;for(unsigned i=0;i<count;++i)if(actors[i].id==self_id){actors[i].ward=0;actors[i].disruption=warden->disruption;}return;}
    float radius=warden->radius;if(upgraded&&radius<96)radius=96;
    for(unsigned i=0;i<count;++i){us_ward_actor_t *actor=&actors[i];if(distance(actor->x-warden->x,actor->y-warden->y)>radius)continue;actor->ward=0;
        if(actor->kind==US_WARD_ACTOR_WARDEN&&(upgraded||actor->id==self_id)){
            if(actor->disruption<duration)actor->disruption=duration;
            actor->protected_count=0;
            if(actor->id==self_id){if(warden->disruption<duration)warden->disruption=duration;warden->protected_count=0;}}
    }
}

