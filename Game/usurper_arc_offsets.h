#ifndef USURPER_ARC_OFFSETS_H
#define USURPER_ARC_OFFSETS_H
#include <stdint.h>
#include <string.h>
typedef struct {uint64_t span_bits;unsigned valid;double points[48];} us_arc_offsets_t;
/* Keep the original multiply-then-divide order and its binary64 rounding.
 * Only the span controls these offsets; rotation/start remain live at use. */
static inline const double *us_arc_offsets(us_arc_offsets_t *cache,double span){
    uint64_t bits;memcpy(&bits,&span,sizeof bits);
    if(!cache->valid||cache->span_bits!=bits){
        for(unsigned point=0;point<48u;++point)cache->points[point]=span*point/47u;
        cache->span_bits=bits;cache->valid=1;
    }
    return cache->points;
}
#endif
