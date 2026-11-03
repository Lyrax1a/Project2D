#ifndef USURPER_SUPPLIES_H
#define USURPER_SUPPLIES_H
#include <stdint.h>
/* Stock can contain at most two pending regular and two pending mechanic
 * deliveries. Immediate nursery hulls are external and do not use this pool. */
#define US_SUPPLY_PENDING_MAX 4u
typedef struct {float x,y;double left;unsigned species;uint8_t mechanic;} us_supply_pending_t;
typedef struct {
    double next_delivery,next_mechanic,empty_time;
    unsigned delivery_index,mechanic_index,contested_index,count,loadout[2];
    uint8_t emergency_used;
    us_supply_pending_t pending[US_SUPPLY_PENDING_MAX];
} us_supplies_t;
typedef struct {
    double elapsed,delta;
    float px,py,rotation,camera_x,camera_y,boss_x,boss_y;
    unsigned stage,completed_mask,capacity,releases,r7,r8,boss_phase;
    uint8_t blocked,showcase,boss_spawned,boss_present,core_open;
} us_supply_context_t;
typedef struct {
    void *context;
    unsigned (*stock)(void *,unsigned mechanic,unsigned species);
    void (*spawn)(void *,unsigned species,float x,float y,unsigned mechanic);
    void (*cull_far)(void *,float px,float py);
    void (*constrain)(void *,float *x,float *y);
    float (*random_angle)(void *);
    void (*hide_markers)(void *);
} us_supply_callbacks_t;
void us_supplies_init(us_supplies_t *supplies);
unsigned us_supplies_species_unlocked(unsigned species,unsigned completed_mask);
unsigned us_supplies_hull_health(unsigned species);
unsigned us_supplies_stock(const us_supplies_t *,const us_supply_callbacks_t *,unsigned mechanic,unsigned species);
void us_supplies_position(const us_supplies_t *,const us_supply_context_t *,const us_supply_callbacks_t *,float *x,float *y);
void us_supplies_spawn(const us_supply_callbacks_t *,unsigned species,float x,float y,unsigned mechanic);
unsigned us_supplies_queue(us_supplies_t *,const us_supply_context_t *,const us_supply_callbacks_t *,unsigned species,unsigned mechanic);
void us_supplies_step(us_supplies_t *,const us_supply_context_t *,const us_supply_callbacks_t *);
void us_supplies_stop(us_supplies_t *,const us_supply_callbacks_t *);
#endif
