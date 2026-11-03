#ifndef USURPER_FORM_COMBAT_H
#define USURPER_FORM_COMBAT_H
#include <stdint.h>

/* specialization.gd's timers use seconds and reset after an elapsed step.
 * Keep double subtraction order, including the original 31st-frame first
 * gun shot and the five-frame separation of 0.08-second followup rounds. */
typedef struct {
    double fire_left,burst_clock,burst_damage,dash_cooldown;
    unsigned burst_left;
} us_form_combat_t;
typedef struct { unsigned count; double damage[2]; } us_form_rounds_t;
static inline void us_form_combat_reset(us_form_combat_t *s) {
    *s=(us_form_combat_t){.fire_left=0.5};
}
static inline double us_form_gun_interval(unsigned f4,unsigned g2) {
    return 1.5/(1.0+0.08*f4+0.15*g2);
}
static inline us_form_rounds_t us_form_combat_tick(us_form_combat_t *s,int gunner,
        int target_available,unsigned f4,unsigned g2,unsigned g4,unsigned g8) {
    const double dt=1.0/60;
    us_form_rounds_t result={0};
    s->dash_cooldown=s->dash_cooldown>dt?s->dash_cooldown-dt:0;
    s->burst_clock-=dt;
    if(s->burst_left&&s->burst_clock<=0) {
        if(target_available)result.damage[result.count++]=s->burst_damage;
        --s->burst_left;s->burst_clock=0.08;
    }
    if(gunner) {
        s->fire_left-=dt;
        if(s->fire_left<=0) {
            s->fire_left=us_form_gun_interval(f4,g2);
            if(!target_available)s->fire_left=0.2;
            else {
                unsigned count=g8?3u:g4?2u:1u;
                s->burst_damage=4.0*(count==3?0.55:count==2?0.7:1.0);
                result.damage[result.count++]=s->burst_damage;
                s->burst_left=count-1u;s->burst_clock=0.08;
            }
        }
    }
    return result;
}
static inline uint32_t us_form_dash_ticks_q16(unsigned a2,unsigned za,uint32_t charge_q16) {
    if(charge_q16>65536u)charge_q16=65536u;
    uint32_t duration=18u*65536u+24u*65536u*a2/5u+(uint32_t)((uint64_t)36u*charge_q16/5u);
    return duration*(za?2u:1u);
}
static inline int us_form_dash_begin(us_form_combat_t *s) {
    if(s->dash_cooldown>0)return 0;
    s->dash_cooldown=3.5;return 1;
}
#endif
