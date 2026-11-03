#include "usurper_softfloat.h"
#include <stdint.h>
void us_sf_div_prepare(float b,us_sf_divisor_t *divisor){
    union {float f;uint32_t u;} y={.f=b};
    divisor->bits=y.u;divisor->exponent=(y.u>>23)&255u;
    divisor->mantissa=(y.u&0x7fffffu)|0x800000u;divisor->reciprocal=0;
    if(!divisor->exponent||divisor->exponent==255u)return;
    int64_t reciprocal=(int64_t)(0xffffffffu/(divisor->mantissa>>8))<<8;
    for(unsigned i=0;i<2u;++i){int64_t error=((int64_t)1u<<48)-(int64_t)divisor->mantissa*reciprocal;reciprocal+=(reciprocal*error)>>48;}
    divisor->reciprocal=reciprocal;
}
float us_sf_div_prepared(float a,const us_sf_divisor_t *divisor){
    union {float f;uint32_t u;} x={.f=a},y={.u=divisor->bits},r;
    unsigned ea=(x.u>>23)&255u,eb=divisor->exponent;
    if(!ea||!eb||ea==255u||eb==255u)return a/y.f;
    uint32_t ma=(x.u&0x7fffffu)|0x800000u,mb=divisor->mantissa;
    unsigned shift=ma<mb?24u:23u;int exponent=(int)ea-(int)eb+127-(shift==24u);
    uint32_t mantissa=(uint32_t)(((uint64_t)ma*(uint64_t)divisor->reciprocal)>>(48u-shift));
    uint64_t numerator=(uint64_t)ma<<shift,product=(uint64_t)mantissa*mb;
    while(product>numerator){--mantissa;product-=mb;}
    while(numerator-product>=mb){++mantissa;product+=mb;}
    uint64_t remainder=numerator-product;
    if(remainder*2u>mb||(remainder*2u==mb&&(mantissa&1u)))++mantissa;
    if(mantissa==0x1000000u){mantissa>>=1;++exponent;}
    if(exponent<=0||exponent>=255)return a/y.f;
    r.u=((x.u^y.u)&0x80000000u)|((uint32_t)exponent<<23)|(mantissa&0x7fffffu);return r.f;
}
float us_sf_div(float a,float b) {
    union {float f;uint32_t u;} x={.f=a},y={.f=b},r;
    unsigned ea=(x.u>>23)&255u,eb=(y.u>>23)&255u;
    if(!ea||!eb||ea==255u||eb==255u)return a/b;
    uint32_t ma=(x.u&0x7fffffu)|0x800000u,mb=(y.u&0x7fffffu)|0x800000u;
    unsigned shift=ma<mb?24u:23u;int exponent=(int)ea-(int)eb+127-(shift==24u);
    int64_t reciprocal=(int64_t)(0xffffffffu/(mb>>8))<<8;
    for(unsigned i=0;i<2u;++i){int64_t error=((int64_t)1u<<48)-(int64_t)mb*reciprocal;reciprocal+=(reciprocal*error)>>48;}
    uint32_t mantissa=(uint32_t)(((uint64_t)ma*(uint64_t)reciprocal)>>(48u-shift));
    uint64_t numerator=(uint64_t)ma<<shift,product=(uint64_t)mantissa*mb;
    while(product>numerator){--mantissa;product-=mb;}
    while(numerator-product>=mb){++mantissa;product+=mb;}
    uint64_t remainder=numerator-product;
    if(remainder*2u>mb||(remainder*2u==mb&&(mantissa&1u)))++mantissa;
    if(mantissa==0x1000000u){mantissa>>=1;++exponent;}
    if(exponent<=0||exponent>=255)return a/b;
    r.u=((x.u^y.u)&0x80000000u)|((uint32_t)exponent<<23)|(mantissa&0x7fffffu);return r.f;
}
float us_sf_mul(float a,float b) {
    union {float f;uint32_t u;} x={.f=a},y={.f=b},r;
    unsigned ea=(x.u>>23)&255u,eb=(y.u>>23)&255u;
    if((!(x.u&0x7fffffffu)||!(y.u&0x7fffffffu))&&ea!=255u&&eb!=255u){r.u=(x.u^y.u)&0x80000000u;return r.f;}
    if(!ea||!eb||ea==255u||eb==255u)return a*b;
    uint64_t product=(uint64_t)((x.u&0x7fffffu)|0x800000u)*((y.u&0x7fffffu)|0x800000u);
    unsigned shift=(product>>47)?24u:23u;int exponent=(int)ea+(int)eb-127+(int)shift-23;
    uint32_t mantissa=(uint32_t)(product>>shift);uint64_t remainder=product&(((uint64_t)1u<<shift)-1u),half=(uint64_t)1u<<(shift-1u);
    if(remainder>half||(remainder==half&&(mantissa&1u)))++mantissa;
    if(mantissa==0x1000000u){mantissa>>=1;++exponent;}
    if(exponent<=0||exponent>=255)return a*b;
    r.u=((x.u^y.u)&0x80000000u)|((uint32_t)exponent<<23)|(mantissa&0x7fffffu);return r.f;
}
float us_sf_add(float a,float b) {
    union {float f;uint32_t u;} x={.f=a},y={.f=b},r;
    unsigned ea=(x.u>>23)&255u,eb=(y.u>>23)&255u;
    if(ea!=255u&&eb!=255u){
        if(!(x.u&0x7fffffffu)&&!(y.u&0x7fffffffu)){r.u=x.u&y.u;return r.f;}
        if(!(x.u&0x7fffffffu))return b;
        if(!(y.u&0x7fffffffu))return a;
    }
    if(!ea||!eb||ea==255u||eb==255u)return a+b;
    if((x.u&0x7fffffffu)<(y.u&0x7fffffffu)){uint32_t t=x.u;x.u=y.u;y.u=t;unsigned e=ea;ea=eb;eb=e;}
    uint32_t big=((x.u&0x7fffffu)|0x800000u)<<3,small=((y.u&0x7fffffu)|0x800000u)<<3;
    unsigned shift=ea-eb;
    if(shift>=32u)small=1u;
    else if(shift){uint32_t discarded=small&(((uint32_t)1u<<shift)-1u);small=(small>>shift)|(discarded!=0u);}
    uint32_t value=(x.u^y.u)&0x80000000u?big-small:big+small;int exponent=(int)ea;
    if(!value){r.u=0;return r.f;}
    if(value&0x8000000u){value=(value>>1)|(value&1u);++exponent;}
    else while(value<0x4000000u){value<<=1;--exponent;}
    uint32_t mantissa=value>>3,remainder=value&7u;
    if(remainder>4u||(remainder==4u&&(mantissa&1u)))++mantissa;
    if(mantissa==0x1000000u){mantissa>>=1;++exponent;}
    if(exponent<=0||exponent>=255)return a+b;
    r.u=(x.u&0x80000000u)|((uint32_t)exponent<<23)|(mantissa&0x7fffffu);return r.f;
}
