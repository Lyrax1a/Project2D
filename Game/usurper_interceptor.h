#ifndef USURPER_INTERCEPTOR_H
#define USURPER_INTERCEPTOR_H
#include <stdint.h>
typedef struct {float x,y,rotation;double life;uint32_t target_id;uint8_t active;} us_interceptor_t;
enum {US_INTERCEPTOR_NONE,US_INTERCEPTOR_EXPIRED,US_INTERCEPTOR_HIT};
void us_interceptor_init(us_interceptor_t *,float x,float y,uint32_t target_id);
unsigned us_interceptor_step(us_interceptor_t *,float tx,float ty,uint32_t target_id,unsigned valid,unsigned active,unsigned queued);
#endif
