#ifndef US_DIRECTION_FAST_H
#define US_DIRECTION_FAST_H
#include <stdint.h>
#include "usurper_direction_table.h"

/* Signed Q44 times Q30, truncating toward zero. The actual operands satisfy
 * |a| <= 2^45 and |b| <= 2^30. Split the product before shifting so
 * RV32 needs only ordinary integer multiplies, never binary64 arithmetic. */
static int64_t us_direction_multiply(int64_t a,int32_t b){
    uint64_t magnitude=a<0?(uint64_t)-a:(uint64_t)a;
    uint32_t factor=b<0?(uint32_t)-b:(uint32_t)b;
    uint64_t low=(uint64_t)(uint32_t)magnitude*factor;
    uint64_t high=(magnitude>>32)*factor;
    uint64_t result=(low>>30)+(high<<2);
    return (a<0)!=(b<0)?-(int64_t)result:(int64_t)result;
}

static unsigned us_direction_round_q44(int64_t value,float *out){
    uint64_t magnitude=value<0?(uint64_t)-value:(uint64_t)value;
    if(!magnitude)return 0;
    unsigned top=0;uint32_t scan;
    if(magnitude>>32){top=32;scan=(uint32_t)(magnitude>>32);}else scan=(uint32_t)magnitude;
    if(scan>>16){top+=16;scan>>=16;}
    if(scan>>8){top+=8;scan>>=8;}
    if(scan>>4){top+=4;scan>>=4;}
    if(scan>>2){top+=2;scan>>=2;}
    if(scan>>1)++top;
    /* Tiny results have float32 rounding bins too narrow for our error bound. */
    if(top<32)return 0;
    unsigned shift=top-23;
    uint64_t step=UINT64_C(1)<<shift,half=step>>1,rest=magnitude&(step-1);
    uint64_t distance=rest>half?rest-half:half-rest;
    /* Reference/table/Taylor error <2^-40; Q44 truncation and coefficient
     * errors add <2^-40. A 2^-37 guard (128 Q44 units) leaves 4x margin.
     * The degree-six remainder at |delta|<1/64 is below 2^-53. */
    if(distance<=128)return 0;
    uint32_t mantissa=(uint32_t)(magnitude>>shift);
    if(rest>half||(rest==half&&(mantissa&1u)))++mantissa;
    unsigned exponent=top+83u; /* top - 44 + binary32 bias 127 */
    if(mantissa==0x1000000u){mantissa>>=1;++exponent;}
    union{uint32_t u;float f;} result={.u=(value<0?0x80000000u:0u)|(exponent<<23)|(mantissa&0x7fffffu)};
    *out=result.f;return 1;
}

static unsigned us_direction_fast(float radians,float *x,float *y){
    union{float f;uint32_t u;} input={.f=radians};uint32_t magnitude=input.u&0x7fffffffu;
    if(!magnitude){*x=1;*y=radians;return 1;}
    /* In [1/128,16], every input is an exact multiple of 2^-30. Reject the
     * rest, including NaN/Inf, before any shift or signed conversion. */
    if(magnitude<0x3c000000u||magnitude>0x41800000u)return 0;
    unsigned exponent=(magnitude>>23)-120u;
    uint64_t angle=(uint64_t)((magnitude&0x7fffffu)|0x800000u)<<exponent;
    int index=(int)(angle>>24),delta=(int)(angle&0xffffffu);
    if(input.u>>31){index=-index;delta=-delta;}
    const us_direction_pair_t *table=&us_direction_table[index+1024];
    int64_t c=table->cosine,s=table->sine;
    /* Rounded Q30 reciprocals replace coefficient divisions. Their error
     * is attenuated by delta^3 or higher: <1/16 Q44 unit in the result. */
    int64_t cx=-us_direction_multiply(c,1491308);
    cx=us_direction_multiply(cx,delta)-us_direction_multiply(s,8947849);
    cx=us_direction_multiply(cx,delta)+us_direction_multiply(c,44739243);
    cx=us_direction_multiply(cx,delta)+us_direction_multiply(s,178956971);
    cx=us_direction_multiply(cx,delta)-c/2;
    cx=us_direction_multiply(cx,delta)-s;
    cx=us_direction_multiply(cx,delta)+c;
    int64_t sy=-us_direction_multiply(s,1491308);
    sy=us_direction_multiply(sy,delta)+us_direction_multiply(c,8947849);
    sy=us_direction_multiply(sy,delta)+us_direction_multiply(s,44739243);
    sy=us_direction_multiply(sy,delta)-us_direction_multiply(c,178956971);
    sy=us_direction_multiply(sy,delta)-s/2;
    sy=us_direction_multiply(sy,delta)+c;
    sy=us_direction_multiply(sy,delta)+s;
    float fx,fy;
    if(!us_direction_round_q44(cx,&fx)||!us_direction_round_q44(sy,&fy))return 0;
    *x=fx;*y=fy;return 1;
}
#endif
