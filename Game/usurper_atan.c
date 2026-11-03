#include "usurper_atan.h"
#include <stdint.h>
/* Generated with Decimal precision 95, nearest-even Q60 atan(2^-i). */
static const int64_t angles[61]={
    905502432259640355LL,534549298976576474LL,282441168888798124LL,143371547418228444LL,71963988336308046LL,
    36017075762092179LL,18012932708689205LL,9007016009513623LL,4503576721087964LL,2251796950380271LL,
    1125899548928887LL,562949908682076LL,281474971118251LL,140737487656277LL,70368744090283LL,
    35184372077909LL,17592186043051LL,8796093022037LL,4398046511083LL,2199023255549LL,
    1099511627776LL,549755813888LL,274877906944LL,137438953472LL,68719476736LL,
    34359738368LL,17179869184LL,8589934592LL,4294967296LL,2147483648LL,
    1073741824LL,536870912LL,268435456LL,134217728LL,67108864LL,
    33554432LL,16777216LL,8388608LL,4194304LL,2097152LL,
    1048576LL,524288LL,262144LL,131072LL,65536LL,
    32768LL,16384LL,8192LL,4096LL,2048LL,
    1024LL,512LL,256LL,128LL,64LL,
    32LL,16LL,8LL,4LL,2LL,
    1LL
};
static double small_atan(double ratio){
    double square=ratio*ratio;
    return ratio*(1.0+square*(-1.0/3.0+square*(1.0/5.0+square*(-1.0/7.0+square/9.0))));
}
float us_atan_float(float x,float y){
    union {float f;uint32_t u;} xb={x},yb={y},result;
    unsigned xe=(xb.u>>23)&255u,ye=(yb.u>>23)&255u,sx=xb.u>>31,syb=yb.u>>31;
    const float pi=3.14159265358979323846f,half_pi=1.57079632679489661923f;
    if((xe==255u&&(xb.u&0x7fffffu))||(ye==255u&&(yb.u&0x7fffffu))){result.u=0x7fc00000u;return result.f;}
    if(xe==255u&&ye==255u){float value=sx?2.35619449019234492885f:0.78539816339744830962f;return syb?-value:value;}
    if(ye==255u)return syb?-half_pi:half_pi;
    if(xe==255u){if(sx)return syb?-pi:pi;result.u=yb.u&0x80000000u;return result.f;}
    if(!y){if(sx)return syb?-pi:pi;result.u=yb.u&0x80000000u;return result.f;}
    if(!x)return syb?-half_pi:half_pi;
    unsigned ex=xe?xe:1u,ey=ye?ye:1u;
    if(ex>=ey+8u){double angle=small_atan((double)y/(double)x);if(sx)angle+=syb?-3.14159265358979323846:3.14159265358979323846;return (float)angle;}
    if(ey>=ex+8u)return (float)((syb?-1.57079632679489661923:1.57079632679489661923)-small_atan((double)x/(double)y));
    unsigned maximum=ex>ey?ex:ey;
    uint64_t xm=(xb.u&0x7fffffu)|(xe?0x800000u:0u),ym=(yb.u&0x7fffffu)|(ye?0x800000u:0u);
    int xs=(int)ex+33-(int)maximum,ys=(int)ey+33-(int)maximum;
    int64_t cx=(int64_t)(xs>=0?xm<<xs:xm>>(-xs)),cy=(int64_t)(ys>=0?ym<<ys:ym>>(-ys));
    if(sx)cx=-cx;
    if(syb)cy=-cy;
    int64_t angle=0;
    if(cx<0){angle=cy<0?-3622009729038561421LL:3622009729038561421LL;cx=-cx;cy=-cy;}
    for(unsigned i=0;i<61u&&cy;++i){int64_t previous=cx;
        if(cy>0){cx+=cy>>i;cy-=previous>>i;angle+=angles[i];}
        else{cx-=cy>>i;cy+=previous>>i;angle-=angles[i];}
    }
    return (float)angle*0x1p-60f;
}
