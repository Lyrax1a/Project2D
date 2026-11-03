/* Geometry adapted from Godot 4.6.2 renderer_canvas_cull.cpp.
 * Copyright (c) 2014-present Godot Engine contributors.
 * Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "usurper_polyline.h"
#include "usurper_fixed.h"
#include "usurper_softfloat.h"
#include "usurper_atan.h"
typedef us_flame_vertex_t point_t;
static float absolute(float value){return value<0?-value:value;}
static unsigned near(float a,float b){if(a==b)return 1;float tolerance=us_sf_mul(0.00001f,absolute(a));if(tolerance<0.00001f)tolerance=0.00001f;return absolute(us_sf_add(a,-b))<tolerance;}
static unsigned zero(point_t p){return absolute(p.x)<0.00001f&&absolute(p.y)<0.00001f;}
static unsigned equal(point_t a,point_t b){return near(a.x,b.x)&&near(a.y,b.y);}
static point_t add(point_t a,point_t b){return (point_t){us_sf_add(a.x,b.x),us_sf_add(a.y,b.y)};}
static point_t sub(point_t a,point_t b){return (point_t){us_sf_add(a.x,-b.x),us_sf_add(a.y,-b.y)};}
static point_t mul(point_t a,float b){return (point_t){us_sf_mul(a.x,b),us_sf_mul(a.y,b)};}
static float length(point_t p){return us_fx_sqrt_float(us_sf_add(us_sf_mul(p.x,p.x),us_sf_mul(p.y,p.y)));}
static point_t normalize(point_t p){float value=length(p);return value?(point_t){us_sf_div(p.x,value),us_sf_div(p.y,value)}:(point_t){0,0};}
static point_t orthogonal(point_t p){return (point_t){p.y,-p.x};}
static point_t segment(const point_t *points,unsigned count,unsigned index,point_t previous){if(index==count-1)return previous;point_t direction=normalize(sub(points[index+1],points[index]));return zero(direction)?previous:direction;}
static point_t offset(point_t direction,point_t previous){
    point_t bisector=normalize(sub(mul(previous,length(direction)),mul(direction,length(previous))));
    float cross=us_sf_add(us_sf_mul(bisector.x,previous.y),-us_sf_mul(bisector.y,previous.x));
    float dot=us_sf_add(us_sf_mul(bisector.x,previous.x),us_sf_mul(bisector.y,previous.y));
    float sine=(float)us_fx_sin_double(us_atan_float(dot,cross)),gain=1;
    if(absolute(sine)>=0.00001f&&!equal(direction,previous)){gain=us_sf_div(1,sine);if(gain>3)gain=3;else if(gain<-3)gain=-3;}else bisector=orthogonal(direction);
    if(zero(bisector))bisector=orthogonal(direction);
    return mul(bisector,gain);
}
static void vertex(us_polyline_strip_t *strip,unsigned index,point_t p,float alpha){strip->vertices[index]=(us_polyline_vertex_t){p.x,p.y,alpha};}
unsigned us_polyline_mesh(const point_t *points,unsigned count,float width,unsigned aa,us_polyline_mesh_t *mesh){
    if(count<2||count>US_POLYLINE_MAX_POINTS||width<0)return 0;
    unsigned loop=equal(points[0],points[count-1]);point_t first={0,0},last={0,0};
    for(unsigned i=1;i<count;++i){first=normalize(sub(points[i],points[i-1]));if(!zero(first))break;}
    for(unsigned i=count-1;i>0;--i){last=normalize(sub(points[i],points[i-1]));if(!zero(last))break;}
    mesh->count=aa?3:1;mesh->loop=loop;mesh->strips[0].count=count*2+(aa&&!loop?4:0);
    mesh->strips[1].count=mesh->strips[2].count=count*2+(loop?0:5);
    float border_size=aa?(width<1?us_sf_mul(1.25f,width):1.25f):0;point_t previous={0,0};
    for(unsigned i=0;i<count;++i){
        unsigned begin=i==0,end=i==count-1;point_t direction=segment(points,count,i,previous);
        if(begin&&loop)previous=last;else if(end&&loop)previous=first;
        point_t base=begin&&!loop?orthogonal(first):end&&!loop?orthogonal(last):offset(direction,previous);
        point_t edge=mul(base,us_sf_mul(width,0.5f)),border=mul(base,border_size),pos=points[i];unsigned j=i*2+(aa&&!loop?2:0);
        vertex(&mesh->strips[0],j,add(pos,edge),1);vertex(&mesh->strips[0],j+1,sub(pos,edge),1);
        if(aa){
            vertex(&mesh->strips[1],j,add(pos,edge),1);vertex(&mesh->strips[1],j+1,add(add(pos,edge),border),0);
            vertex(&mesh->strips[2],j,sub(pos,edge),1);vertex(&mesh->strips[2],j+1,sub(sub(pos,edge),border),0);
            if(begin&&!loop){point_t cap=mul(direction,-border_size);
                vertex(&mesh->strips[0],0,add(add(pos,edge),cap),0);vertex(&mesh->strips[0],1,add(sub(pos,edge),cap),0);
                vertex(&mesh->strips[1],0,add(add(pos,edge),cap),0);vertex(&mesh->strips[1],1,add(add(add(pos,edge),cap),border),0);
                vertex(&mesh->strips[2],0,add(sub(pos,edge),cap),0);vertex(&mesh->strips[2],1,sub(add(sub(pos,edge),cap),border),0);
            }
            if(end&&!loop){point_t cap=mul(previous,border_size);unsigned at=count*2+2;
                vertex(&mesh->strips[0],at,add(add(pos,edge),cap),0);vertex(&mesh->strips[0],at+1,add(sub(pos,edge),cap),0);
                vertex(&mesh->strips[1],at,add(pos,edge),1);vertex(&mesh->strips[1],at+1,add(add(add(pos,edge),cap),border),0);vertex(&mesh->strips[1],at+2,add(add(pos,edge),cap),0);
                vertex(&mesh->strips[2],at,sub(pos,edge),1);vertex(&mesh->strips[2],at+1,sub(add(sub(pos,edge),cap),border),0);vertex(&mesh->strips[2],at+2,add(sub(pos,edge),cap),0);
            }
        }
        previous=direction;
    }
    return 1;
}
