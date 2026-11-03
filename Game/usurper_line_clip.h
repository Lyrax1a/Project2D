#ifndef USURPER_LINE_CLIP_H
#define USURPER_LINE_CLIP_H

/* Trim a monotone integer stamp sequence without moving any retained stamp.
 * Division has exactly the original C truncation. Bounds are inclusive stamp
 * centres, derived from intersection with the research viewport. */
static inline void us_line_clip_axis(int origin,int delta,int steps,int minimum,
                                    int maximum,int *first,int *last) {
    if (*first>*last) return;
    if (delta<0) {
        origin=-origin;delta=-delta;
        int prior_minimum=minimum;minimum=-maximum;maximum=-prior_minimum;
    }
    if (origin+delta*(*last)/steps<minimum ||
        origin+delta*(*first)/steps>maximum) { *first=*last+1;return; }
    int low=*first,high=*last;
    if (origin+delta*low/steps<minimum) {
        while(low<high) {
            int middle=low+(high-low)/2;
            if(origin+delta*middle/steps<minimum)low=middle+1;else high=middle;
        }
        *first=low;
    }
    low=*first;high=*last;
    if(origin+delta*high/steps>maximum) {
        while(low<high) {
            int middle=low+(high-low+1)/2;
            if(origin+delta*middle/steps>maximum)high=middle-1;else low=middle;
        }
        *last=low;
    }
}
#endif
