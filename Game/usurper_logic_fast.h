#ifndef USURPER_LOGIC_FAST_H
#define USURPER_LOGIC_FAST_H
#include <stdint.h>
/* Same signed truncation as (int64_t)component * speed / length. Most orbit
 * vectors fit a 32-bit numerator; RV32IM can then use its hardware divider.
 * Full-width products retain the original 64-bit division. length is nonzero. */
static inline int32_t us_logic_scale_component(int32_t component,uint32_t speed,uint32_t length){
    uint32_t magnitude=component<0?0u-(uint32_t)component:(uint32_t)component;
    uint64_t product=(uint64_t)magnitude*speed;
    uint64_t quotient=product<=UINT32_MAX?(uint32_t)product/length:product/length;
    return component<0?(int32_t)-(int64_t)quotient:(int32_t)quotient;
}
/* Highest power of four <= value, for nonzero 32-bit value. Four bounded
 * comparisons replace the leading-zero scan of the restoring square root. */
static inline uint32_t us_logic_power4_u32(uint32_t value){
    unsigned shift=0;
    if(value>=65536u){value>>=16;shift+=16;}
    if(value>=256u){value>>=8;shift+=8;}
    if(value>=16u){value>>=4;shift+=4;}
    if(value>=4u)shift+=2;
    return (uint32_t)1u<<shift;
}
/* Exact floor sqrt over all uint64_t values. Both branches retain the original
 * restoring algorithm; only operand width and initial zero-bit skips change.
 * Values in the 32-bit branch need no 64-bit adds/shifts on RV32IM. */
static inline uint32_t us_logic_sqrt_u64(uint64_t value){
    if(!value)return 0;
    uint32_t high=(uint32_t)(value>>32);
    if(!high){
        uint32_t remaining=(uint32_t)value,result=0,bit=us_logic_power4_u32(remaining);
        while(bit){
            if(remaining>=result+bit){remaining-=result+bit;result=(result>>1)+bit;}
            else result>>=1;
            bit>>=2;
        }
        return result;
    }
    uint64_t result=0,bit=(uint64_t)us_logic_power4_u32(high)<<32;
    while(bit){
        if(value>=result+bit){value-=result+bit;result=(result>>1)+bit;}
        else result>>=1;
        bit>>=2;
    }
    return (uint32_t)result;
}
/* Exact broad-phase rejection for floor(sqrt(dx*dx+dy*dy)) <= limit.
 * Integer |dx| > limit implies floor(length) >= |dx| > limit. Equal axes
 * are deliberately retained: the original sqrt decides the circular edge.
 * Unsigned subtraction avoids signed overflow for the full int32 range. */
static inline unsigned us_logic_box_excludes(int32_t x0,int32_t y0,int32_t x1,int32_t y1,uint32_t limit){
    uint32_t dx=x1>=x0?(uint32_t)x1-(uint32_t)x0:(uint32_t)x0-(uint32_t)x1;
    uint32_t dy=y1>=y0?(uint32_t)y1-(uint32_t)y0:(uint32_t)y0-(uint32_t)y1;
    return dx>limit||dy>limit;
}
#endif
