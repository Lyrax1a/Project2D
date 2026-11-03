/* Godot 4.6 SortArray algorithm, adapted for threat distances.
 * Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md).
 * Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include "usurper_threat_sort.h"
static unsigned less(us_threat_order_t a,us_threat_order_t b){return a.distance_squared<b.distance_squared;}
static unsigned median(us_threat_order_t *a,unsigned x,unsigned y,unsigned z){
    if(less(a[x],a[y]))return less(a[y],a[z])?y:less(a[x],a[z])?z:x;
    return less(a[x],a[z])?x:less(a[y],a[z])?z:y;
}
static void push(us_threat_order_t *a,unsigned first,unsigned hole,unsigned top,us_threat_order_t value){
    while(hole>top){unsigned parent=(hole-1)/2;if(!less(a[first+parent],value))break;a[first+hole]=a[first+parent];hole=parent;}
    a[first+hole]=value;
}
static void adjust(us_threat_order_t *a,unsigned first,unsigned hole,unsigned length,us_threat_order_t value){
    unsigned top=hole,child=2*hole+2;
    while(child<length){if(less(a[first+child],a[first+child-1]))--child;a[first+hole]=a[first+child];hole=child;child=2*(child+1);}
    if(child==length){a[first+hole]=a[first+child-1];hole=child-1;}push(a,first,hole,top,value);
}
static void heap_sort(us_threat_order_t *a,unsigned first,unsigned last){
    unsigned length=last-first;if(length<2)return;
    unsigned parent=(length-2)/2;
    for(;;){adjust(a,first,parent,length,a[first+parent]);if(!parent)break;--parent;}
    while(last-first>1){--last;us_threat_order_t value=a[last];a[last]=a[first];adjust(a,first,0,last-first,value);}
}
static unsigned partition(us_threat_order_t *a,unsigned first,unsigned last,unsigned pivot){
    unsigned pivot_location=pivot;
    for(;;){
        while(first!=pivot&&less(a[first],a[pivot_location]))++first;
        --last;while(last!=pivot&&less(a[pivot_location],a[last]))--last;
        if(first>=last)return first;
        if(pivot_location==first)pivot_location=last;else if(pivot_location==last)pivot_location=first;
        us_threat_order_t tmp=a[first];a[first]=a[last];a[last]=tmp;++first;
    }
}
static void intro(us_threat_order_t *a,unsigned first,unsigned last,unsigned depth){
    while(last-first>16){
        if(!depth){heap_sort(a,first,last);return;}--depth;
        unsigned cut=partition(a,first,last,median(a,first,first+(last-first)/2,last-1));intro(a,cut,last,depth);last=cut;
    }
}
void us_threat_sort(us_threat_order_t *a,unsigned count){
    if(!count)return;
    unsigned depth=0;for(unsigned n=count;n>1;n>>=1)++depth;intro(a,0,count,depth*2);
    for(unsigned i=1;i<count;++i){us_threat_order_t value=a[i];unsigned at=i;while(at&&less(value,a[at-1])){a[at]=a[at-1];--at;}a[at]=value;}
}
