#ifndef USURPER_TRIANGLE_MATH_H
#define USURPER_TRIANGLE_MATH_H
#include <stdint.h>
#include "usurper_softfloat.h"
/* Exact round-half-away-from-zero of a binary32 coordinate onto the 1/256
 * raster grid. Multiplication by 256 is an exponent shift; its fractional
 * remainder can be rounded in integers without software binary64 calls.
 * The raster accepts [-32768,32767]; preserve the old expression outside
 * the wider finite fast range instead of changing caller range handling. */
static inline int32_t us_triangle_grid(float value){
    union {float value;uint32_t bits;} word={.value=value};
    unsigned exponent=(word.bits>>23)&255u;
    if(exponent<=142u){
        if(exponent<118u)return 0;
        uint32_t mantissa=(word.bits&0x7fffffu)|0x800000u;
        unsigned shift=142u-exponent;
        uint32_t magnitude=shift?(mantissa+(1u<<(shift-1u)))>>shift:mantissa;
        return word.bits>>31?-(int32_t)magnitude:(int32_t)magnitude;
    }
    double scaled=(double)value*256;
    return (int32_t)(scaled+(scaled<0?-0.5:0.5));
}
/* The denominators here are strictly positive edge increments or areas. */
static inline int64_t us_triangle_floor_div(int64_t value,int64_t divisor){
    if(value>=INT32_MIN&&value<=INT32_MAX&&divisor<=INT32_MAX){
        int32_t a=(int32_t)value,b=(int32_t)divisor,q=a/b,r=a%b;
        return (int64_t)q-(r<0);
    }
    /* Raster edge increments contain the exact 256 subpixel factor.
     * floor(n/(256*d)) == floor(floor(n/256)/d), also for negative n.
     * This reduces most wide row numerators to the RV32 hardware divider. */
    if(!(divisor&255)&&divisor<=((int64_t)INT32_MAX*256)){
        int64_t reduced=value/256-(value%256<0);
        if(reduced>=INT32_MIN&&reduced<=INT32_MAX){
            int32_t a=(int32_t)reduced,b=(int32_t)(divisor/256),q=a/b,r=a%b;
            return (int64_t)q-(r<0);
        }
    }
    int64_t q=value/divisor;return q-(value%divisor<0);
}
static inline int64_t us_triangle_ceil_div(int64_t value,int64_t divisor){
    if(value>=INT32_MIN&&value<=INT32_MAX&&divisor<=INT32_MAX){
        int32_t a=(int32_t)value,b=(int32_t)divisor,q=a/b,r=a%b;
        return (int64_t)q+(r>0);
    }
    /* Symmetric ceiling composition; no value+255 expression can overflow. */
    if(!(divisor&255)&&divisor<=((int64_t)INT32_MAX*256)){
        int64_t reduced=value/256+(value%256>0);
        if(reduced>=INT32_MIN&&reduced<=INT32_MAX){
            int32_t a=(int32_t)reduced,b=(int32_t)(divisor/256),q=a/b,r=a%b;
            return (int64_t)q+(r>0);
        }
    }
    int64_t q=value/divisor;return q+(value%divisor>0);
}
/* Exact floor((initial + row*increment)/divisor) by quotient/remainder
 * recurrence. Raster grid bounds keep |initial| below 2^49 and divisor and
 * |increment| below 2^33, so initial - quotient*divisor fits int64_t. */
typedef struct {int64_t quotient,delta;uint64_t remainder,advance,divisor;} us_triangle_edge_walk_t;
static inline us_triangle_edge_walk_t us_triangle_edge_walk(int64_t initial,int64_t increment,int64_t divisor){
    us_triangle_edge_walk_t e;
    e.quotient=us_triangle_floor_div(initial,divisor);e.delta=us_triangle_floor_div(increment,divisor);
    e.remainder=(uint64_t)(initial-e.quotient*divisor);
    e.advance=(uint64_t)(increment-e.delta*divisor);e.divisor=(uint64_t)divisor;
    return e;
}
static inline void us_triangle_edge_next(us_triangle_edge_walk_t *e){
    e->quotient+=e->delta;e->remainder+=e->advance;
    if(e->remainder>=e->divisor){e->remainder-=e->divisor;++e->quotient;}
}
/* The small-triangle path bounds edge values and increments below INT32_MAX.
 * Edge divisors contain a factor of 256, so quotient updates fit int32_t;
 * the sum of two remainders fits uint32_t. The unrestricted raster retains
 * the wide walk. Callers must prove these bounds before using this helper. */
typedef struct {int32_t quotient,delta;uint32_t remainder,advance,divisor;} us_triangle_edge32_t;
static inline us_triangle_edge32_t us_triangle_edge32(int32_t initial,int32_t increment,int32_t divisor){
    us_triangle_edge32_t e;
    int32_t q=initial/divisor,r=initial%divisor;
    e.quotient=q-(r<0);e.remainder=(uint32_t)(r<0?r+divisor:r);
    q=increment/divisor;r=increment%divisor;
    e.delta=q-(r<0);e.advance=(uint32_t)(r<0?r+divisor:r);e.divisor=(uint32_t)divisor;
    return e;
}
static inline void us_triangle_edge32_next(us_triangle_edge32_t *e){
    e->quotient+=e->delta;e->remainder+=e->advance;
    if(e->remainder>=e->divisor){e->remainder-=e->divisor;++e->quotient;}
}
static inline float us_triangle_binary_ratio(int64_t numerator,int64_t area){
    /* Both integers are exact binary32 values in this range. A non-tie
     * quotient is at least 1/(2*area) of a binary32 ulp from its midpoint,
     * greater than the binary64 rounding error. Thus single division and
     * the prior double-then-single division round to the same bits. */
    if(area<=0x1000000&&numerator>=0&&numerator<=area)
        return us_sf_div((float)(uint32_t)numerator,(float)(uint32_t)area);
    return (float)((double)numerator/(double)area);
}
#endif
