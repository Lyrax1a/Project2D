#include "usurper_heavy_bomb.h"
#include "usurper_fixed.h"
#include "usurper_softfloat.h"
#include <string.h>
static double distance(const us_heavy_bomb_t *b){float dx=b->destination_x-b->origin_x,dy=b->destination_y-b->origin_y;return us_fx_sqrt_float(dx*dx+dy*dy);}
void us_heavy_bomb_init(us_heavy_bomb_t *b,float x,float y,float dx,float dy,uint32_t serial){
    memset(b,0,sizeof(*b));b->x=b->origin_x=x;b->y=b->origin_y=y;b->destination_x=dx;b->destination_y=dy;
    b->lifetime=b->initial_lifetime=1.8;b->flight=1.8-0.65;b->speed=2*distance(b)/b->flight;b->intercept_health=1;b->serial=serial;b->active=1;
}
unsigned us_heavy_bomb_step(us_heavy_bomb_t *b,float px,float py){
    if(!b->active)return 0;
    b->lifetime-=1.0/60;double progress=(b->initial_lifetime-b->lifetime)/b->flight;if(progress<0)progress=0;else if(progress>1)progress=1;
    float weight=(float)(1-(1-progress)*(1-progress));
    b->x=us_sf_add(b->origin_x,us_sf_mul(us_sf_add(b->destination_x,-b->origin_x),weight));
    b->y=us_sf_add(b->origin_y,us_sf_mul(us_sf_add(b->destination_y,-b->origin_y),weight));b->speed=2*distance(b)/b->flight*(1-progress);
    float x=px-b->x,y=py-b->y;
    /* The source Area2D explodes on player contact even before its fuse ends.
     * Projectile radius 2 plus the player's hurtbox radius 5.5. */
    if(b->lifetime>0&&x*x+y*y>=7.5f*7.5f)return 0;
    double half=60/1.4142135623730950488;unsigned hit=(x<0?-x:x)<=half&&(y<0?-y:y)<=half;
    b->active=0;return hit?24:0;
}
unsigned us_heavy_bomb_intercept(us_heavy_bomb_t *b,double amount){
    if(!b->active||b->intercept_health<=0)return 0;
    if(amount>0)b->intercept_health-=amount;
    if(b->intercept_health<=0)b->active=0;
    return 1;
}
