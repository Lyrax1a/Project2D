#ifndef USURPER_WARDEN_H
#define USURPER_WARDEN_H
#include <stdint.h>
/* Independent deployed-field model. Scene provider integration and visual
 * acceptance are separate; it must not replace guard collision sweeps. */
typedef struct {
    float x,y,radius,block_dx,block_dy;
    double age,duration,block_flash;
    unsigned interceptions;
    uint8_t active;
} us_warden_field_t;
typedef struct {float x,y,vx,vy;uint8_t alive,boss,excluded;} us_warden_enemy_t;
typedef struct {float x,y;uint8_t interceptible,active;} us_warden_shot_t;
typedef struct {unsigned w2,w4,w6,w7,w8,zw;} us_warden_ranks_t;
typedef struct {float x,y,at_x,at_y;double damage;uint8_t hit,stopped;} us_warden_sweep_t;
typedef struct {
    void *context;
    void (*restore_shield)(void *,double amount,unsigned raw_restore);
    void (*recycle)(void *,unsigned shot_index);
    void (*retaliate)(void *,float x,float y,unsigned damage,unsigned pierce);
} us_warden_callbacks_t;
void us_warden_deploy(us_warden_field_t *,float x,float y,const us_warden_ranks_t *);
unsigned us_warden_contains(const us_warden_field_t *,float px,float py);
void us_warden_project(const us_warden_field_t *,us_warden_enemy_t *);
unsigned us_warden_step(us_warden_field_t *,float px,float py,unsigned ending,const us_warden_ranks_t *,
                        us_warden_enemy_t *,unsigned enemy_count,us_warden_shot_t *,unsigned shot_count,const us_warden_callbacks_t *);
us_warden_sweep_t us_warden_sweep(us_warden_field_t *,float sx,float sy,float tx,float ty,double hull_radius,
                                unsigned responds,unsigned boss,double max_health,unsigned strengthened);
#endif
