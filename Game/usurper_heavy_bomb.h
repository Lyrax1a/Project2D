#ifndef USURPER_HEAVY_BOMB_H
#define USURPER_HEAVY_BOMB_H
#include <stdint.h>
typedef struct {
    float x,y,origin_x,origin_y,destination_x,destination_y;
    double lifetime,initial_lifetime,flight,speed,intercept_health;
    uint32_t serial;uint8_t active;
} us_heavy_bomb_t;
void us_heavy_bomb_init(us_heavy_bomb_t *bomb,float x,float y,float destination_x,float destination_y,uint32_t serial);
unsigned us_heavy_bomb_step(us_heavy_bomb_t *bomb,float player_x,float player_y);
unsigned us_heavy_bomb_intercept(us_heavy_bomb_t *bomb,double damage);
#endif
