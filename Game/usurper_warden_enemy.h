#ifndef USURPER_WARDEN_ENEMY_H
#define USURPER_WARDEN_ENEMY_H
#include <stdint.h>
enum {US_WARD_ACTOR_NORMAL,US_WARD_ACTOR_WARDEN,US_WARD_ACTOR_BOSS,US_WARD_ACTOR_FRAGMENT};
typedef struct {float x,y;double ward,disruption;uint32_t id,generation;unsigned protected_count;uint8_t alive,kind;} us_ward_actor_t;
typedef struct {float x,y,vx,vy,radius;double aura_tick,disruption;uint32_t escort_id,escort_generation;unsigned protected_count;uint8_t escort_valid;} us_warden_ai_t;
void us_warden_enemy_init(us_warden_ai_t *,float x,float y,float radius);
void us_warden_enemy_step(us_warden_ai_t *,float px,float py,float speed,double action_rate,unsigned alive,unsigned target_valid,
    us_ward_actor_t *,unsigned count,const us_ward_actor_t *squad_leader,
    void (*move)(void *,us_warden_ai_t *),void *context);
void us_warden_enemy_counter(us_warden_ai_t *,uint32_t self_id,us_ward_actor_t *,unsigned count,
    unsigned has_parent,unsigned rail_tag,unsigned source_rail,unsigned source_other_rail,double upgraded_duration);
uint32_t us_warden_enemy_conformance_crc(void);
#endif

