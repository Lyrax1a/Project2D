#ifndef USURPER_TRIANGLE_BATCH_H
#define USURPER_TRIANGLE_BATCH_H
#include <string.h>
#include "../Engine/Software/platform/board_conformance/firmware/triangle_hw_descriptor.h"
#include "usurper_command_pack.h"

#define US_TRIANGLE_BANK_BASE 0x0ac00000u
#define US_TRIANGLE_BANK_BYTES 0x00100000u
#define US_TRIANGLE_RECORDS (US_TRIANGLE_BANK_BYTES/64u)
#define US_TRIANGLE_BATCH_LIMIT 32u
typedef struct {
    us_hw_triangle_t triangles[US_TRIANGLE_BATCH_LIMIT];
    uint32_t base;
    unsigned count,used,capacity,failed;
} us_triangle_batch_t;
/* Caller owns the selected frame bank exclusively until GPU completion.
 * Begin/append never write source DDR or emit a command. Failed admission or
 * insufficient capacity therefore permits one complete original-primitive
 * fallback. A partial submission failure forbids fallback/reusing its records. */
static inline int us_triangle_frame(us_triangle_batch_t *b,uint32_t base,unsigned capacity){
    if(!b || (base!=US_TRIANGLE_BANK_BASE && base!=US_TRIANGLE_BANK_BASE+US_TRIANGLE_BANK_BYTES) ||
       capacity>US_TRIANGLE_RECORDS)return -1;
    b->base=base;b->count=b->used=b->failed=0;b->capacity=capacity;return 0;
}
static inline void us_triangle_begin(us_triangle_batch_t *b){b->count=b->failed=0;}
static inline int us_triangle_append(us_triangle_batch_t *b,const us_polyline_vertex_t points[3],float paint){
    us_hw_triangle_t t;
    if(b->failed)return 1;
    int result=us_hw_triangle_prepare(points,1024,576,paint,&t);
    if(result<0 || (result>0 && b->count==US_TRIANGLE_BATCH_LIMIT)){b->failed=1;return 1;}
    if(result>0)b->triangles[b->count++]=t;
    return 0;
}
static inline int us_triangle_commit(us_triangle_batch_t *b,uint32_t framebuffer,uint16_t color,
                                      int (*emit)(void *,const ts2d_command_t *),void *context){
    if(!b||!emit || b->used>b->capacity || b->capacity>US_TRIANGLE_RECORDS ||
       b->count>US_TRIANGLE_BATCH_LIMIT ||
       (b->base!=US_TRIANGLE_BANK_BASE && b->base!=US_TRIANGLE_BANK_BASE+US_TRIANGLE_BANK_BYTES))return -1;
    if(b->failed || b->count>b->capacity-b->used)return 1;
    unsigned start=b->used;
    for(unsigned i=0;i<b->count;++i){
        volatile uint32_t *target=(volatile uint32_t *)(uintptr_t)(b->base+(start+i)*64u);
        for(unsigned word=0;word<16;++word){
            uint32_t value;memcpy(&value,(const uint8_t *)&b->triangles[i].d+word*4u,4);target[word]=value;
        }
    }
#ifndef USURPER_HOST
    __asm__ volatile("fence rw,rw" ::: "memory");
#endif
    b->used+=b->count;
    for(unsigned i=0;i<b->count;++i){
        const us_hw_triangle_t *t=&b->triangles[i];
        us_command_words_t command=us_command_pack(7,9,3,255,b->base+(start+i)*64u,framebuffer,
            0,0,(int16_t)t->left,(int16_t)t->top,(uint16_t)t->width,(uint16_t)t->height,0,2048,
            (uint32_t)color|(t->inclusive<<16));
        if(emit(context,&command.command))return -1;
    }
    return 0;
}
#endif
