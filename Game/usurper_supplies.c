#include "usurper_supplies.h"
#include "usurper_fixed.h"
#include "usurper_softfloat.h"
#include <string.h>
static float clamp(float value,float low,float high){return value<low?low:value>high?high:value;}
void us_supplies_init(us_supplies_t *s){memset(s,0,sizeof(*s));s->next_delivery=6;s->next_mechanic=61;s->loadout[0]=2;s->loadout[1]=6;}
unsigned us_supplies_species_unlocked(unsigned species,unsigned mask){
    /* Campaign species IDs: dart/skimmer/rammer/gunner/warden/splitter/bomber/
     * leech/lancer/seeker/phase/carrier/fragment. Seeker's source boss 99
     * has no campaign bit; fragments have no support unlock. */
    static const uint8_t boss[13]={0,0,1,2,2,4,4,5,5,99,3,6,99};
    return species>0&&species<=13&&boss[species-1]<8&&(mask&(1u<<boss[species-1]))!=0;
}
unsigned us_supplies_hull_health(unsigned species){
    /* Hull health for the thirteen enemy species at level 1.
     * These values differ from the controlled-unit health table. */
    static const uint8_t health[13]={26,32,64,55,78,65,72,65,70,68,85,110,15};
    return species>0&&species<=13?health[species-1]:0;
}
unsigned us_supplies_stock(const us_supplies_t *s,const us_supply_callbacks_t *api,unsigned mechanic,unsigned species){
    unsigned count=api->stock(api->context,mechanic,species);
    for(unsigned i=0;i<s->count;++i)if(s->pending[i].mechanic==mechanic&&(!species||s->pending[i].species==species))++count;
    return count;
}
void us_supplies_position(const us_supplies_t *s,const us_supply_context_t *c,const us_supply_callbacks_t *api,float *x,float *y){
    float left=clamp(c->camera_x-296,28,772),top=clamp(c->camera_y-156,28,452),right=clamp(c->camera_x+296,28,772),bottom=clamp(c->camera_y+156,28,452);
    if(c->stage==7&&c->boss_present){
        float dx=c->boss_x-c->px,dy=c->boss_y-c->py,length=us_fx_sqrt_float(dx*dx+dy*dy),ax=length?dx/length:0,ay=length?dy/length:0;
        float side=s->contested_index%2?-85.0f:85.0f;
        *x=clamp(us_sf_add(us_sf_add(c->px,us_sf_mul(dx,0.5f)),us_sf_mul(ay,side)),left,right);
        *y=clamp(us_sf_add(us_sf_add(c->py,us_sf_mul(dy,0.5f)),us_sf_mul(-ax,side)),top,bottom);return;
    }
    float angle=api->random_angle(api->context);
    for(unsigned side=0;side<4;++side){
        float ax,ay;us_fx_direction_float((float)((double)angle+side*1.57079632679489661923),&ax,&ay);
        *x=clamp(us_sf_add(c->px,us_sf_mul(ax,150)),left,right);*y=clamp(us_sf_add(c->py,us_sf_mul(ay,150)),top,bottom);
        float dx=*x-c->px,dy=*y-c->py;if(us_fx_sqrt_float(dx*dx+dy*dy)>=96)return;
    }
    *x=left+(right-left)*0.5f;*y=top+(bottom-top)*0.5f;
}
void us_supplies_spawn(const us_supply_callbacks_t *api,unsigned species,float x,float y,unsigned mechanic){api->constrain(api->context,&x,&y);api->spawn(api->context,species,x,y,mechanic);}
unsigned us_supplies_queue(us_supplies_t *s,const us_supply_context_t *c,const us_supply_callbacks_t *api,unsigned species,unsigned mechanic){
    if(!c->showcase&&!us_supplies_species_unlocked(species,c->completed_mask))return 0;
    if(s->count>=US_SUPPLY_PENDING_MAX)return 0;
    us_supply_pending_t *entry=&s->pending[s->count++];memset(entry,0,sizeof(*entry));
    us_supplies_position(s,c,api,&entry->x,&entry->y);api->constrain(api->context,&entry->x,&entry->y);
    entry->species=species;entry->mechanic=(uint8_t)mechanic;entry->left=1;return 1;
}
void us_supplies_step(us_supplies_t *s,const us_supply_context_t *c,const us_supply_callbacks_t *api){
    if(c->blocked)return;
    if(!c->capacity)s->empty_time+=c->delta;else s->empty_time=0;
    if(!s->emergency_used&&s->empty_time>=3&&c->releases&&c->r8&&(c->showcase||us_supplies_species_unlocked(s->loadout[0],c->completed_mask))){
        s->emergency_used=1;float ax,ay;us_fx_direction_float(c->rotation,&ax,&ay);
        us_supplies_spawn(api,s->loadout[0],us_sf_add(c->px,us_sf_mul(ax,60)),us_sf_add(c->py,us_sf_mul(ay,60)),0);
    }
    api->cull_far(api->context,c->px,c->py);
    for(unsigned i=s->count;i>0;--i){unsigned index=i-1;us_supply_pending_t *entry=&s->pending[index];
        api->constrain(api->context,&entry->x,&entry->y);entry->left-=c->delta;
        if(entry->left<=0){us_supplies_spawn(api,entry->species,entry->x,entry->y,entry->mechanic);
            for(unsigned j=index;j+1<s->count;++j){s->pending[j]=s->pending[j+1];}
            --s->count;}
    }
    unsigned slots=c->completed_mask&(1u<<3)?2:c->completed_mask&(1u<<1)?1:0;
    if(slots&&c->elapsed>=s->next_delivery){
        s->next_delivery=c->elapsed+(14-(double)c->r7*2);
        if(us_supplies_stock(s,api,0,0)<2){(void)us_supplies_queue(s,c,api,s->loadout[s->delivery_index%slots],0);++s->delivery_index;}
    }
    unsigned nursery=c->stage==7&&c->boss_present&&c->boss_phase==1;
    if((c->stage<2||nursery)&&c->boss_spawned&&c->elapsed>=s->next_mechanic){
        s->next_mechanic=c->elapsed+6;unsigned species;
        if(nursery)species=s->mechanic_index%2?4:1;
        else{species=c->stage?s->mechanic_index%2?4:3:s->mechanic_index%2?1:2;if(c->stage==1)species=3;if(c->boss_present&&c->core_open)species=4;}
        if(us_supplies_stock(s,api,1,!nursery&&c->boss_present&&c->core_open?species:0)<2){(void)us_supplies_queue(s,c,api,species,1);++s->mechanic_index;}
    }
}
void us_supplies_stop(us_supplies_t *s,const us_supply_callbacks_t *api){s->count=0;api->hide_markers(api->context);}
