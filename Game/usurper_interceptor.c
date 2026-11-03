#include "usurper_interceptor.h"
#include "usurper_fixed.h"
#include "usurper_atan.h"
#include "usurper_softfloat.h"
#include <string.h>
void us_interceptor_init(us_interceptor_t *interceptor,float x,float y,uint32_t target_id){memset(interceptor,0,sizeof(*interceptor));interceptor->x=x;interceptor->y=y;interceptor->life=0.6;interceptor->target_id=target_id;interceptor->active=1;}
unsigned us_interceptor_step(us_interceptor_t *interceptor,float tx,float ty,uint32_t target_id,unsigned valid,unsigned active,unsigned queued){
    if(!interceptor->active)return US_INTERCEPTOR_NONE;
    interceptor->life-=1.0/60;
    if(interceptor->life<=0||!valid||!active||queued||target_id!=interceptor->target_id){interceptor->active=0;return US_INTERCEPTOR_EXPIRED;}
    float dx=tx-interceptor->x,dy=ty-interceptor->y,length=us_fx_sqrt_float(dx*dx+dy*dy);interceptor->rotation=us_atan_float(dx,dy);
    const float step=(float)(460.0/60.0);
    if(length<=step||length<0.00001f){interceptor->x=tx;interceptor->y=ty;}
    else{interceptor->x=us_sf_add(interceptor->x,us_sf_mul(dx/length,step));interceptor->y=us_sf_add(interceptor->y,us_sf_mul(dy/length,step));}
    dx=tx-interceptor->x;dy=ty-interceptor->y;
    if(us_fx_sqrt_float(dx*dx+dy*dy)<8){interceptor->active=0;return US_INTERCEPTOR_HIT;}return US_INTERCEPTOR_NONE;
}
