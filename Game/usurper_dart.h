#ifndef USURPER_DART_H
#define USURPER_DART_H
#include "usurper_fixed.h"
typedef struct {float x,y,vx,vy,rotation;} us_dart_t;
void us_dart_init(us_dart_t *dart,float x,float y);
void us_dart_step(us_dart_t *dart,int32_t px,int32_t py,int32_t pvx,int32_t pvy,float speed);
#endif
