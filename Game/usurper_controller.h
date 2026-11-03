#ifndef USURPER_CONTROLLER_H
#define USURPER_CONTROLLER_H
#include "usurper_fixed.h"
#define US_CONTROLLER_NESTS 6u
#define US_CONTROLLER_FIGHTERS 12u
#define US_CONTROLLER_CORE 6u
/* Species match the campaign/scene board ABI. */
enum { US_CONTROLLER_DART=1,US_CONTROLLER_GUNNER=4,US_CONTROLLER_CARRIER=12 };
enum { US_CONTROLLER_CLEAR_ARRIVALS=1,US_CONTROLLER_CLEAR_ENEMIES=2,US_CONTROLLER_SUPPLY_NEXT=4,US_CONTROLLER_CLEAR_SHOTS=8 };
typedef struct {
    float x,y,rotation,heading_x,heading_y,destination_x,destination_y;
    double hp,max_hp,age,duration,fire_left,fire_age,shot_clock,drone_clock,flash,hit_kick;
    uint32_t serial;unsigned volleys,species;
    uint8_t active,hatched,firing,capture_immune;
} us_controller_nest_t;
typedef struct {
    float x,y,vx,vy,rotation;
    double age,shot_clock,hp,flash,hit_kick;
    uint32_t carrier_serial,serial;unsigned wave_index;
    uint8_t present,queued,capture_immune;
} us_controller_fighter_t;
typedef struct {
    double clock,age,hit_clock;unsigned index,volleys;
    uint8_t started,retiring;
} us_controller_waves_t;
/* Independent development model. Scene linkage, drawing and hardware
 * acceptance are separate from source-state comparisons. */
typedef struct {
    float x,y,vx,vy,rotation,locked_x,locked_y,point_x,point_y;
    double elapsed,attack_clock,shot_clock,recovery,form_clock,hp,max_hp,shield,
           core_hit_clock,phase_started,flash,damage_received,launch_clock,cooldowns[6];
    unsigned form,phase,defense_layer,generation,hatched_count,destroyed_bubbles,destroyed_carriers,activated_count;
    uint32_t next_serial;int activating;
    unsigned nest_order[6],nest_count,fighter_order[12],fighter_count;
    us_controller_nest_t nests[6];us_controller_fighter_t fighters[12];us_controller_waves_t waves;
    uint8_t active,finishing,pending_wave_begin,pending_flags,pending_bubble_count;
    void *guard_context;
    us_boss_guard_sweep_fn guard_sweep;
    struct {float x,y;unsigned species;} pending_bubbles[6];
} us_controller_t;
typedef struct {
    unsigned count;
    struct {float x,y,dx,dy;unsigned damage,speed;} shots[32];
    unsigned ordnance_count;
    struct {float x,y,dx,dy;unsigned damage,speed,mode;double lifetime;} ordnance[18];
    unsigned flags,bubble_count,charge_damage,flame_damage,blade_damage;
    struct {float x,y;unsigned species;} bubbles[6];
    unsigned bomb_count;struct {float x,y,landing_x,landing_y;double speed;} bombs[1];
} us_controller_output_t;
void us_controller_init(us_controller_t *boss,int32_t x,int32_t y,int32_t px,int32_t py);
void us_controller_birth(int32_t px,int32_t py,int32_t camera_x,int32_t camera_y,int32_t *x,int32_t *y,unsigned preferred);
void us_controller_forms_step(us_controller_t *boss,int32_t px,int32_t py,us_controller_output_t *output);
void us_controller_hit(us_controller_t *boss,double damage,unsigned nursery_break);
void us_controller_hit_nest(us_controller_t *boss,unsigned slot,double damage,unsigned rail);
void us_controller_hit_fighter(us_controller_t *boss,unsigned slot,double damage);
unsigned us_controller_living_nests(const us_controller_t *boss);
unsigned us_controller_wave_slots(const us_controller_t *boss,unsigned external_enemies,unsigned scheduled);
unsigned us_controller_wave_roster(const us_controller_t *boss,unsigned external_enemies,unsigned scheduled,unsigned carriers);
void us_controller_step(us_controller_t *boss,int32_t px,int32_t py,int32_t pvx,int32_t pvy,float max_speed,
                        unsigned external_enemies,unsigned scheduled,unsigned heavy_bombs,us_controller_output_t *output);
void us_controller_finish(us_controller_t *boss,int32_t px,int32_t py,us_controller_output_t *output);
#endif
