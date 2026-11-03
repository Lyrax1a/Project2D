#ifndef TRIANGLE_HW_ALPHA_REFERENCE_H
#define TRIANGLE_HW_ALPHA_REFERENCE_H
#include <stdint.h>
/* Integer-only specification for a future limited hardware slow path.
 * n/area: 0<=n<=area<=2^24. Paint: zero or binary32 in [2^-32,1].
 * All nonzero intermediate products are normal binary32, so the original
 * ratio -> float multiply -> multiply255 -> add0.5 sequence can be reproduced
 * without a general floating-point unit. Not used by ordinary firmware. */
static inline uint32_t hw_alpha_ratio_bits(uint32_t n,uint32_t area){
    if(!n)return 0;
    uint32_t exponent=127,mantissa=0x800000u;
    uint64_t remainder=n;
    while(remainder<area){remainder<<=1;--exponent;}
    remainder-=area;
    for(int bit=22;bit>=0;--bit){
        remainder<<=1;
        if(remainder>=area){remainder-=area;mantissa|=1u<<bit;}
    }
    if(remainder*2u>area || (remainder*2u==area&&(mantissa&1u)))++mantissa;
    if(mantissa==0x1000000u){mantissa>>=1;++exponent;}
    return exponent<<23|(mantissa&0x7fffffu);
}
static inline uint32_t hw_alpha_mul_bits(uint32_t a,uint32_t b){
    if(!a||!b)return 0;
    uint64_t product=(uint64_t)((a&0x7fffffu)|0x800000u)*((b&0x7fffffu)|0x800000u);
    unsigned shift=(product>>47)?24u:23u;
    unsigned exponent=(a>>23)+(b>>23)-127u+shift-23u;
    uint32_t mantissa=(uint32_t)(product>>shift);
    uint64_t remainder=product&((UINT64_C(1)<<shift)-1u),half=UINT64_C(1)<<(shift-1u);
    if(remainder>half || (remainder==half&&(mantissa&1u)))++mantissa;
    if(mantissa==0x1000000u){mantissa>>=1;++exponent;}
    return exponent<<23|(mantissa&0x7fffffu);
}
static inline uint32_t hw_alpha_add_half_bits(uint32_t a){
    uint32_t b=0x3f000000u;
    if(!a)return b;
    if(a<b){uint32_t swap=a;a=b;b=swap;}
    unsigned exponent=a>>23,shift=exponent-(b>>23);
    uint32_t big=((a&0x7fffffu)|0x800000u)<<3;
    uint32_t aligned_smaller=((b&0x7fffffu)|0x800000u)<<3;
    if(shift>=32)aligned_smaller=1;
    else if(shift)aligned_smaller=(aligned_smaller>>shift)|((aligned_smaller&((1u<<shift)-1u))!=0);
    uint32_t value=big+aligned_smaller;
    if(value&0x8000000u){value=(value>>1)|(value&1u);++exponent;}
    uint32_t mantissa=value>>3,remainder=value&7u;
    if(remainder>4u || (remainder==4u&&(mantissa&1u)))++mantissa;
    if(mantissa==0x1000000u){mantissa>>=1;++exponent;}
    return exponent<<23|(mantissa&0x7fffffu);
}
/* Invalid inputs are explicitly refused, never silently approximated. */
static inline int hw_triangle_alpha_reference(uint32_t n,uint32_t area,uint32_t paint){
    if(!area||area>0x1000000u||n>area ||
       (paint && (paint<0x2f800000u||paint>0x3f800000u)))return -1;
    uint32_t value=hw_alpha_mul_bits(hw_alpha_ratio_bits(n,area),paint);
    if(!value)return 0;
    if(value>=0x3f800000u)return 255;
    value=hw_alpha_add_half_bits(hw_alpha_mul_bits(value,0x437f0000u));
    unsigned exponent=value>>23;
    if(exponent<127)return 0;
    return (int)(((value&0x7fffffu)|0x800000u)>>(150u-exponent));
}
#endif
