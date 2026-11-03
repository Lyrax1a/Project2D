#ifndef USURPER_FORMS_H
#define USURPER_FORMS_H
#include <stdint.h>

/* Shared Dart motion and Warden guard; the other three combat forms live in
 * usurper_form_combat.h and the native scene implementation include. */
typedef struct { double guard_cooldown,guard_flash; } us_form_runtime_t;
static inline void us_form_reset(us_form_runtime_t *s) { s->guard_cooldown=s->guard_flash=0; }
static inline unsigned us_form_dart_quarters(int32_t form,unsigned provider,unsigned d9) {
    return (form==6?4u:0u)+(provider?(d9?2u:1u):0u);
}
static inline unsigned us_form_speed_q8(unsigned base,unsigned quarters) {
    return (unsigned)((uint64_t)base*256u*(10000u+550u*quarters)/10000u);
}
static inline unsigned us_form_accel_step_q8(unsigned quarters) {
    return (unsigned)((uint64_t)23125u*256u*(10000u+1750u*quarters)/(100u*60u*10000u));
}
static inline unsigned us_form_coast_q24(unsigned quarters) {
    /* round(exp(-0.82*(1+0.0875*q)/60)*2^24); keeps no runtime exp on RV32. */
    const uint32_t values[7]={16549487u,16529708u,16509954u,16490222u,16470514u,16450830u,16431169u};
    return values[quarters<=6u?quarters:6u];
}
static inline unsigned us_form_turn_step(unsigned base,unsigned quarters) {
    return (base*(10000u+750u*quarters)+5000u)/10000u;
}
static inline void us_form_guard_step(us_form_runtime_t *s) {
    s->guard_cooldown=s->guard_cooldown>1.0/60?s->guard_cooldown-1.0/60:0;
    s->guard_flash=s->guard_flash>1.0/60?s->guard_flash-1.0/60:0;
}
static inline void us_form_guard_started(us_form_runtime_t *s) { s->guard_cooldown=1.5;s->guard_flash=.25; }
#endif
