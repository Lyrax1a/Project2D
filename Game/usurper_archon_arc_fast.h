#ifndef USURPER_ARCHON_ARC_FAST_H
#define USURPER_ARCHON_ARC_FAST_H
#include "usurper_direction_table.h"
static inline int32_t us_arc_multiply(int32_t a,int32_t b){
    return (int32_t)(((int64_t)a*b)>>24);
}
/* Only the final radius-52 Q8 integer is observable in the shield drawing.
 * A cubic table residual has error <= delta^4/24 + delta^5/120,
 * delta <= 1/64. After scaling by 13312 this is < 0.000034.
 * Binary32 direction and radius-product rounding contribute < 0.0013.
 * Q24 residual truncation contributes <0.000794 Q8; Q16 coefficient and
 * multiply rounding contribute <0.0001. The conservative +/-1/128 Q8
 * interval contains these, binary32 rounding and table error.
 * Accept only when BOTH interval endpoints truncate to the same integer;
 * near every pixel-grid boundary keep the original direction calculation.
 * No movement, aiming, collision or other trigonometry uses this helper. */
static unsigned us_archon_arc_offset_fast(float radians,int32_t *x,int32_t *y){
    union {float f;uint32_t u;} input={.f=radians};
    uint32_t magnitude=input.u&0x7fffffffu;
    if(!magnitude){*x=13312;*y=0;return 1;}
    if(magnitude<0x3c000000u||magnitude>0x41800000u)return 0;
    unsigned exponent=(magnitude>>23)-120u;
    uint64_t angle=(uint64_t)((magnitude&0x7fffffu)|0x800000u)<<exponent;
    int index=(int)(angle>>24),delta=(int)((angle&0xffffffu)>>6);
    if(input.u>>31){index=-index;delta=-delta;}
    /* Radius 52*256, represented in Q16: 2^44 * 13312 * 2^16 / 2^44
     * reduces to table * 13 >> 18. Coefficients fit signed 32 bits, avoiding
     * the RV32 software 64-bit /6 used by the earlier Q44 polynomial. */
    int32_t c=(int32_t)((us_direction_table[index+1024].cosine*13)>>18);
    int32_t s=(int32_t)((us_direction_table[index+1024].sine*13)>>18);
    int32_t cx=c+us_arc_multiply(-s+us_arc_multiply(-c/2+us_arc_multiply(s/6,delta),delta),delta);
    int32_t sy=s+us_arc_multiply(c+us_arc_multiply(-s/2+us_arc_multiply(-c/6,delta),delta),delta);
    int32_t lo_x=(cx-512)/65536,hi_x=(cx+512)/65536;
    int32_t lo_y=(sy-512)/65536,hi_y=(sy+512)/65536;
    if(lo_x!=hi_x||lo_y!=hi_y)return 0;
    *x=lo_x;*y=lo_y;return 1;
}
#endif
