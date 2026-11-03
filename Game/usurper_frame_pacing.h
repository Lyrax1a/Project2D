#ifndef USURPER_FRAME_PACING_H
#define USURPER_FRAME_PACING_H
#include <stdint.h>
/* Called only after at least one logical update. Preserve all accumulator
 * time; defer a catch-up update if it would consume the next draw's budget.
 * At >=2 remaining ticks force catch-up, retaining the existing eight-step
 * cap for severe stalls. This breaks the 1 miss -> 2 updates -> 1 miss loop
 * without changing the 60 Hz step, replay ordering, or discarding time. */
static inline unsigned us_pacing_defer(uint32_t debt,uint32_t spent,
                                     uint32_t step_cost,uint32_t draw_cost,uint32_t period){
    if(!step_cost||!draw_cost||debt>=2u*period)return 0;
    return (uint64_t)spent+step_cost+draw_cost+period/32u>period;
}
static inline uint32_t us_pacing_step_cost(uint32_t previous,uint32_t measured){
    uint32_t decayed=previous-previous/16u;
    return measured>decayed?measured:decayed;
}
#endif
