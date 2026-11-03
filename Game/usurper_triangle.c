#include "usurper_triangle.h"
#include <stdint.h>
#include "usurper_triangle_math.h"
#include "usurper_triangle_alpha_math.h"
typedef struct {int32_t x,y;float alpha;} vertex_t;
static int32_t grid(float value){return us_triangle_grid(value);}
static int64_t edge(vertex_t a,vertex_t b,int32_t x,int32_t y){return (int64_t)(b.x-a.x)*(y-a.y)-(int64_t)(b.y-a.y)*(x-a.x);}
static unsigned inclusive(vertex_t a,vertex_t b){int32_t dy=b.y-a.y,dx=b.x-a.x;return dy<0||(dy==0&&dx>0);}

#define US_TRIANGLE_RASTER raster_small
#define US_TRIANGLE_INT int32_t
#define US_TRIANGLE_WALK us_triangle_edge32_t
#define US_TRIANGLE_WALK_BEGIN us_triangle_edge32
#define US_TRIANGLE_WALK_NEXT us_triangle_edge32_next
#include "usurper_triangle_raster_impl.h"
#undef US_TRIANGLE_RASTER
#undef US_TRIANGLE_INT
#undef US_TRIANGLE_WALK
#undef US_TRIANGLE_WALK_BEGIN
#undef US_TRIANGLE_WALK_NEXT
#define US_TRIANGLE_RASTER raster_wide
#define US_TRIANGLE_INT int64_t
#define US_TRIANGLE_WALK us_triangle_edge_walk_t
#define US_TRIANGLE_WALK_BEGIN us_triangle_edge_walk
#define US_TRIANGLE_WALK_NEXT us_triangle_edge_next
#include "usurper_triangle_raster_impl.h"
#undef US_TRIANGLE_RASTER
#undef US_TRIANGLE_INT
#undef US_TRIANGLE_WALK
#undef US_TRIANGLE_WALK_BEGIN
#undef US_TRIANGLE_WALK_NEXT
static void raster(const us_polyline_vertex_t points[3],unsigned width,unsigned height,void (*span)(void *,int,int,unsigned,float),void (*alpha_span)(void *,int,int,unsigned,unsigned),float paint,void *context){
    vertex_t v[3];for(unsigned i=0;i<3;++i){if(points[i].x>32767||points[i].x<-32768||points[i].y>32767||points[i].y<-32768)return;v[i]=(vertex_t){grid(points[i].x),grid(points[i].y),points[i].alpha};}
    int64_t area=edge(v[0],v[1],v[2].x,v[2].y);if(!area)return;if(area<0){vertex_t swap=v[1];v[1]=v[2];v[2]=swap;area=-area;}
    int32_t minx=v[0].x,maxx=v[0].x,miny=v[0].y,maxy=v[0].y;for(unsigned i=1;i<3;++i){if(v[i].x<minx)minx=v[i].x;if(v[i].x>maxx)maxx=v[i].x;if(v[i].y<miny)miny=v[i].y;if(v[i].y>maxy)maxy=v[i].y;}
    int left=(minx+127)>>8,right=(maxx+127)>>8,top=(miny+127)>>8,bottom=(maxy+127)>>8;
    if(left<0)left=0;
    if(top<0)top=0;
    if(right>(int)width)right=(int)width;
    if(bottom>(int)height)bottom=(int)height;
    if(left>=right||top>=bottom)return;
    /* A coordinate extent <=32000 grid units (125 pixels) bounds each
     * edge product difference, including pixel-center/rounding margins and
     * the final row advance, below 2*32512*32000 < INT32_MAX. Down/step
     * magnitudes are <=32000*256. At admitted pixels barycentric sums are
     * in [0,area]. Larger/clipped-away inputs retain the original wide path.
     * No coordinates, coverage or paint values are quantized differently. */
    if(maxx-minx<=32000&&maxy-miny<=32000)
        raster_small(v,(int32_t)area,left,right,top,bottom,span,alpha_span,paint,context);
    else raster_wide(v,area,left,right,top,bottom,span,alpha_span,paint,context);
}
void us_triangle_coverage(const us_polyline_vertex_t points[3],unsigned width,unsigned height,void (*span)(void *,int,int,unsigned,float),void *context){
    raster(points,width,height,span,0,0,context);
}
void us_triangle_resolved_alpha(const us_polyline_vertex_t points[3],unsigned width,unsigned height,float paint,void (*span)(void *,int,int,unsigned,unsigned),void *context){
    raster(points,width,height,0,span,paint,context);
}
