#include "usurper_raster.h"
typedef struct {int first,last;int64_t x,slope;} edge_t;
/* Round the float's exact binary value to the renderer's 1/256 grid.
 * Return failure for large coordinates and retain the general renderer. */
static unsigned vertex(float value,int32_t *out) {
    union {float f;uint32_t u;} bits={.f=value};unsigned exponent=(bits.u>>23)&255u;
    if(exponent>=142u)return 0;
    if(exponent<=117u){*out=0;return 1;}
    unsigned shift=142u-exponent,mantissa=(bits.u&0x7fffffu)|0x800000u;
    int32_t rounded=(int32_t)((mantissa+(1u<<(shift-1u)))>>shift);
    *out=bits.u>>31?-rounded:rounded;return 1;
}
void us_raster_polygon(const us_flame_vertex_t *points,unsigned count,unsigned color,unsigned alpha,
    unsigned width,unsigned height,void (*span)(void *,int,int,unsigned,unsigned,unsigned),void *context) {
    if(!alpha||count<3u||count>US_RASTER_POLYGON_MAX||!width||!height)return;
    int32_t xs[US_RASTER_POLYGON_MAX],ys[US_RASTER_POLYGON_MAX];
    for(unsigned i=0;i<count;++i)if(!vertex(points[i].x,&xs[i])||!vertex(points[i].y,&ys[i])) {
        us_flame_raster_polygon(points,count,color,alpha,width,height,span,context);return;
    }
    edge_t edges[US_RASTER_POLYGON_MAX];unsigned edge_count=0;int first=(int)height,last=0;
    for(unsigned i=0;i<count;++i) {
        unsigned j=(i+1u)%count;int32_t ax=xs[i],ay=ys[i],bx=xs[j],by=ys[j];
        if(ay==by)continue;
        if(ay>by){int32_t t=ax;ax=bx;bx=t;t=ay;ay=by;by=t;}
        int start=(ay+127)>>8,end=(by+127)>>8;
        if(start<0)start=0;
        if(end>(int)height)end=(int)height;
        if(end<=start)continue;
        int32_t dx=bx-ax,dy=by-ay;
        int64_t offset=(int64_t)start*256+128-ay;
        int64_t x=((int64_t)ax*dy+offset*dx)*256/dy;
        edges[edge_count++]=(edge_t){start,end,x,(int64_t)dx*65536/dy};
        if(start<first)first=start;
        if(end>last)last=end;
    }
    for(int y=first;y<last;++y) {
        int64_t intersections[US_RASTER_POLYGON_MAX];unsigned hits=0;
        for(unsigned i=0;i<edge_count;++i)if(y>=edges[i].first&&y<edges[i].last) {
            int64_t x=edges[i].x;edges[i].x+=edges[i].slope;unsigned insert=hits;
            while(insert&&intersections[insert-1u]>x){intersections[insert]=intersections[insert-1u];--insert;}
            intersections[insert]=x;++hits;
        }
        for(unsigned i=0;i+1u<hits;i+=2u) {
            int left=(int)((intersections[i]+32767)>>16),right=(int)((intersections[i+1u]+32767)>>16);
            if(left<0)left=0;
            if(right>(int)width)right=(int)width;
            if(right>left)span(context,left,y,(unsigned)(right-left),color,alpha);
        }
    }
}
