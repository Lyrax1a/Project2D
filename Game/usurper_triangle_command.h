#ifndef USURPER_TRIANGLE_COMMAND_H
#define USURPER_TRIANGLE_COMMAND_H
#include <string.h>
#include "../Engine/Software/platform/board_conformance/firmware/triangle_hw_descriptor.h"

/* Source address whitelist is checked by the caller BEFORE dereferencing.
 * Validate the finite integer descriptor algebra, original coefficient and
 * int32 recurrence range over the full [0,xmax] x [0,ymax] rectangle, which
 * includes the row producer's intermediate offset initialization positions. */
static inline int us_triangle_descriptor_valid(const us_hw_triangle_descriptor_t *d,unsigned xmax,unsigned ymax){
    union{uint32_t u;float f;} paint={.u=d->paint_bits};
    if(!d->area||d->area>0x1000000u || xmax>127 || ymax>127 ||
       (paint.u && (paint.u<0x2f800000u||paint.u>0x3f800000u)))return 0;
    if((int64_t)d->edge[0]+d->edge[1]+d->edge[2]!=d->area ||
       (int64_t)d->step[0]+d->step[1]+d->step[2] ||
       (int64_t)d->down[0]+d->down[1]+d->down[2])return 0;
    unsigned found=0;
    for(unsigned mask=0;mask<8;++mask){
        int64_t n=0,nx=0,ny=0;
        for(unsigned i=0;i<3;++i)if(mask&(1u<<i)){n+=d->edge[i];nx+=d->step[i];ny+=d->down[i];}
        if(n==d->numerator && nx==d->nx && ny==d->ny)found=1;
    }
    if(!found)return 0;
    if(d->coefficient!=(uint64_t)((double)paint.f*255.0*281474976710656.0/(double)d->area))return 0;
    for(unsigned i=0;i<4;++i){
        int64_t base=i<3?d->edge[i]:d->numerator;
        int64_t step=i<3?d->step[i]:d->nx,down=i<3?d->down[i]:d->ny;
        for(unsigned corner=0;corner<4;++corner){
            int64_t value=base+((corner&1)?step*xmax:0)+((corner&2)?down*ymax:0);
            if(value<INT32_MIN||value>INT32_MAX)return 0;
        }
    }
    return 1;
}
static inline int us_triangle_command_valid(const ts2d_command_t *c){
    if(c->opcode!=7 || c->src_format!=3 || c->flags!=9 || c->global_alpha!=255 ||
       c->src_stride_bytes || c->src_x<0 || c->src_y<0 || !c->width || !c->height ||
       (unsigned)c->src_x+c->width>128 || (unsigned)c->src_y+c->height>128 ||
       (c->parameter>>19) || (c->src_base_addr&63u) ||
       c->src_base_addr<0x0ac00000u || c->src_base_addr>0x0ae00000u-64u)return 0;
    return us_triangle_descriptor_valid((const us_hw_triangle_descriptor_t *)(uintptr_t)c->src_base_addr,
        (unsigned)c->src_x+c->width-1u,(unsigned)c->src_y+c->height-1u);
}
static inline uint16_t us_triangle_blend(uint16_t s,uint16_t d,unsigned a){
    unsigned sr=(s>>11)*8+(s>>13),sg=((s>>5)&63)*4+((s>>9)&3),sb=(s&31)*8+((s>>2)&7);
    unsigned dr=(d>>11)*8+(d>>13),dg=((d>>5)&63)*4+((d>>9)&3),db=(d&31)*8+((d>>2)&7);
    unsigned r=(sr*a+dr*(255-a)+127)/255,g=(sg*a+dg*(255-a)+127)/255,b=(sb*a+db*(255-a)+127)/255;
    return (uint16_t)((r>>3)<<11|(g>>2)<<5|(b>>3));
}
static inline int us_triangle_command_execute(ts2d_surface_t *destination,const ts2d_command_t *c){
    us_hw_triangle_t t={0};memcpy(&t.d,(const void *)(uintptr_t)c->src_base_addr,64);
    t.width=t.height=128;t.inclusive=(c->parameter>>16)&7u;
    int x0=c->dst_x<0?0:c->dst_x,y0=c->dst_y<0?0:c->dst_y;
    int x1=c->dst_x+c->width,y1=c->dst_y+c->height;
    if(x1>destination->width)x1=destination->width;
    if(y1>destination->height)y1=destination->height;
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){
        int a=us_hw_triangle_sample(&t,(unsigned)(x-c->dst_x+c->src_x),(unsigned)(y-c->dst_y+c->src_y));
        if(a<0 || a>255)return TS2D_ERR_RANGE;
        if(a){uint16_t *pixel=(uint16_t *)((uint8_t *)destination->pixels+(unsigned)y*destination->stride_bytes)+(unsigned)x;
            *pixel=us_triangle_blend((uint16_t)c->parameter,*pixel,(unsigned)a);}
    }
    return TS2D_OK;
}
#endif
