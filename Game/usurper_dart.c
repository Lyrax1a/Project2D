#include "usurper_dart.h"
#include "usurper_softfloat.h"
static float clip(float v,float low,float high){return v<low?low:v>high?high:v;}
void us_dart_init(us_dart_t *b,float x,float y){b->x=x;b->y=y;b->vx=b->vy=b->rotation=0;}
void us_dart_step(us_dart_t *b,int32_t pxq,int32_t pyq,int32_t pvxq,int32_t pvyq,float speed) {
    const float gain=0.06449301540851593018f,delta=0.01666666753590106964f;
    b->x=clip(b->x,14,786);b->y=clip(b->y,14,466);
    float px=us_sf_add(pxq/65536.0f,us_sf_mul(pvxq/65536.0f,0.22f));
    float py=us_sf_add(pyq/65536.0f,us_sf_mul(pvyq/65536.0f,0.22f));
    float dx=us_sf_add(px,-b->x),dy=us_sf_add(py,-b->y),length=us_fx_sqrt_float(us_sf_add(us_sf_mul(dx,dx),us_sf_mul(dy,dy)));
    float vx=length?us_sf_mul(us_sf_div(dx,length),speed):0,vy=length?us_sf_mul(us_sf_div(dy,length),speed):0;
    b->vx=us_sf_add(b->vx,us_sf_mul(us_sf_add(vx,-b->vx),gain));
    b->vy=us_sf_add(b->vy,us_sf_mul(us_sf_add(vy,-b->vy),gain));
    b->rotation=us_fx_angle_float(b->vx,b->vy);
    b->x=us_sf_add(b->x,us_sf_mul(b->vx,delta));b->y=us_sf_add(b->y,us_sf_mul(b->vy,delta));
}
