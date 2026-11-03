#ifndef TRIANGLE_HW_DESCRIPTOR_H
#define TRIANGLE_HW_DESCRIPTOR_H
#include <stddef.h>
#include <stdint.h>
#include "../../../../../Game/usurper_triangle.h"
#include "../../../../../Game/usurper_triangle_math.h"
#include "triangle_hw_alpha_reference.h"

/* Experimental descriptor only; not consumed by ordinary firmware/RTL yet.
 * Coordinates retain the original grid/half-open boundary conventions. */
typedef struct {
    int32_t edge[3],step[3],down[3];
    int32_t numerator,nx,ny;
    uint64_t coefficient;
    uint32_t area,paint_bits;
} us_hw_triangle_descriptor_t;
_Static_assert(sizeof(us_hw_triangle_descriptor_t)==64,"triangle descriptor must be64bytes");
_Static_assert(offsetof(us_hw_triangle_descriptor_t,coefficient)==48,"coefficient word offset");
typedef struct {
    us_hw_triangle_descriptor_t d;
    int left,top;
    unsigned width,height,inclusive;
} us_hw_triangle_t;
typedef struct {int32_t x,y;unsigned alpha;} us_hw_triangle_vertex_t;
enum {US_HW_TRI_UNSUPPORTED=-1,US_HW_TRI_EMPTY=0,US_HW_TRI_ACCEPTED=1};
static inline int64_t us_hw_tri_edge(us_hw_triangle_vertex_t a,us_hw_triangle_vertex_t b,int32_t x,int32_t y){
    return (int64_t)(b.x-a.x)*(y-a.y)-(int64_t)(b.y-a.y)*(x-a.x);
}
/* Exact replacement for trunc(binary64(paint * 255 * 2^48 / area)).
 * Admission guarantees normal positive binary32 paint in [2^-32,1] and
 * 1 <= area <= 2^24. The product before division is exact binary64.
 * Reduce it to n/d, then reproduce nearest-even binary64 rounding BEFORE
 * truncation. Merely taking n/d is wrong near integer and large-ULP ties. */
static inline uint64_t us_hw_triangle_coefficient_uncached(uint32_t paint,uint32_t area){
    uint32_t mantissa=((paint&0x7fffffu)|0x800000u)*255u;
    int shift=(int)(paint>>23)-102;
    uint64_t numerator=mantissa;
    uint32_t denominator=area;
    if(shift>=0)numerator<<=(unsigned)shift;
    else denominator<<=(unsigned)-shift;
    uint64_t quotient=numerator/denominator;
    uint32_t remainder=(uint32_t)(numerator-quotient*denominator);
    unsigned exponent=0;uint32_t bits=(uint32_t)(quotient>>32);
    if(bits)exponent=32;else bits=(uint32_t)quotient;
    if(bits>=0x10000u){bits>>=16;exponent+=16;}
    if(bits>=0x100u){bits>>=8;exponent+=8;}
    if(bits>=0x10u){bits>>=4;exponent+=4;}
    if(bits>=4u){bits>>=2;exponent+=2;}
    if(bits>=2u)++exponent;
    if(exponent<52u){
        /* Below an integer boundary the binary64 spacing is 2^(e-52),
         * including the boundary that is itself a power of two.
         * Integer boundaries have even significands in this range. */
        unsigned distance=53u-exponent;
        if(distance<=31u &&
           ((uint64_t)(denominator-remainder)<<distance)<=denominator)++quotient;
        return quotient;
    }
    unsigned unit_shift=exponent-52u;
    if(!unit_shift){
        uint64_t twice=(uint64_t)remainder*2u;
        if(twice>denominator || (twice==denominator&&(quotient&1u)))++quotient;
        return quotient;
    }
    uint64_t unit=UINT64_C(1)<<unit_shift,tail=quotient&(unit-1u);
    uint64_t rounded=quotient&~(unit-1u);
    if(tail>unit/2u || (tail==unit/2u&&(remainder||(quotient&unit))))rounded+=unit;
    return rounded;
}
/* The exact coefficient depends only on area and paint, not screen position.
 * Repeated core/feather triangles commonly share both. Full keys avoid collisions. */
static inline uint64_t us_hw_triangle_coefficient(uint32_t paint,uint32_t area){
    static struct {uint32_t paint,area;uint64_t coefficient;} cache[16];
    unsigned slot=((area>>4)^area^paint^(paint>>16))&15u;
    if(cache[slot].area==area&&cache[slot].paint==paint)return cache[slot].coefficient;
    uint64_t result=us_hw_triangle_coefficient_uncached(paint,area);
    cache[slot].paint=paint;cache[slot].area=area;cache[slot].coefficient=result;return result;
}
static inline int us_hw_triangle_prepare(const us_polyline_vertex_t points[3],unsigned width,unsigned height,float paint,us_hw_triangle_t *out){
    union{float f;uint32_t u;} value={.f=paint};
    if(!width||!height||width>1024||height>576 ||
       (value.u && (value.u<0x2f800000u||value.u>0x3f800000u)))return US_HW_TRI_UNSUPPORTED;
    us_hw_triangle_vertex_t v[3];unsigned any=0;
    for(unsigned i=0;i<3;++i){
        if(!(points[i].x>=-32768 && points[i].x<=32767 && points[i].y>=-32768 && points[i].y<=32767) ||
           !(points[i].alpha==0 || points[i].alpha==1))return US_HW_TRI_UNSUPPORTED;
        v[i]=(us_hw_triangle_vertex_t){us_triangle_grid(points[i].x),us_triangle_grid(points[i].y),points[i].alpha==1};any|=v[i].alpha;
    }
    if(!value.u || !any)return US_HW_TRI_EMPTY;
    int64_t area=us_hw_tri_edge(v[0],v[1],v[2].x,v[2].y);
    if(!area)return US_HW_TRI_EMPTY;
    if(area<0){us_hw_triangle_vertex_t swap=v[1];v[1]=v[2];v[2]=swap;area=-area;}
    int32_t minx=v[0].x,maxx=v[0].x,miny=v[0].y,maxy=v[0].y;
    for(unsigned i=1;i<3;++i){if(v[i].x<minx)minx=v[i].x;if(v[i].x>maxx)maxx=v[i].x;if(v[i].y<miny)miny=v[i].y;if(v[i].y>maxy)maxy=v[i].y;}
    if(maxx-minx>32000 || maxy-miny>32000 || area>0x1000000)return US_HW_TRI_UNSUPPORTED;
    int left=(minx+127)>>8,right=(maxx+127)>>8,top=(miny+127)>>8,bottom=(maxy+127)>>8;
    if(left<0)left=0;
    if(top<0)top=0;
    if(right>(int)width)right=(int)width;
    if(bottom>(int)height)bottom=(int)height;
    if(left>=right||top>=bottom)return US_HW_TRI_EMPTY;
    us_hw_triangle_t t={0};t.left=left;t.top=top;t.width=(unsigned)(right-left);t.height=(unsigned)(bottom-top);
    int64_t numerator=0,nx=0,ny=0;
    for(unsigned i=0;i<3;++i){
        us_hw_triangle_vertex_t a=v[(i+1)%3],b=v[(i+2)%3];
        int32_t dx=b.x-a.x,dy=b.y-a.y;int64_t row=us_hw_tri_edge(a,b,left*256+128,top*256+128);
        if(row<INT32_MIN||row>INT32_MAX)return US_HW_TRI_UNSUPPORTED;
        t.d.edge[i]=(int32_t)row;t.d.step[i]=-dy*256;t.d.down[i]=dx*256;
        if(dy<0 || (dy==0&&dx>0))t.inclusive|=1u<<i;
        if(v[i].alpha){numerator+=row;nx+=t.d.step[i];ny+=t.d.down[i];}
    }
    if(numerator<INT32_MIN||numerator>INT32_MAX || nx<INT32_MIN||nx>INT32_MAX || ny<INT32_MIN||ny>INT32_MAX)return US_HW_TRI_UNSUPPORTED;
    t.d.numerator=(int32_t)numerator;t.d.nx=(int32_t)nx;t.d.ny=(int32_t)ny;
    t.d.area=(uint32_t)area;t.d.paint_bits=value.u;
    t.d.coefficient=us_hw_triangle_coefficient(value.u,(uint32_t)area);
    *out=t;return US_HW_TRI_ACCEPTED;
}
/* Integer descriptor reference. x/y are relative to its clipped bbox.
 * Outside pixels and zero-alpha coverage produce0; invalid descriptors/n
 * produce-1. Callers must not use unsupported input as an empty primitive. */
static inline int us_hw_triangle_sample(const us_hw_triangle_t *t,unsigned x,unsigned y){
    if(x>=t->width||y>=t->height)return -1;
    for(unsigned i=0;i<3;++i){
        int64_t e=t->d.edge[i]+(int64_t)t->d.step[i]*x+(int64_t)t->d.down[i]*y;
        if(e<(t->inclusive&(1u<<i)?0:1))return 0;
    }
    int64_t n=t->d.numerator+(int64_t)t->d.nx*x+(int64_t)t->d.ny*y;
    if(n<0||n>t->d.area)return -1;
    if(!n)return 0;
    uint64_t rounded=(uint64_t)n*t->d.coefficient+(UINT64_C(1)<<47);
    uint64_t fraction=rounded&((UINT64_C(1)<<48)-1);
    if(fraction>=(UINT64_C(1)<<36) && fraction<=((UINT64_C(1)<<48)-(UINT64_C(1)<<36)))return (int)(rounded>>48);
    return hw_triangle_alpha_reference((uint32_t)n,t->d.area,t->d.paint_bits);
}
#endif
