#include "usurper_geometry_cache.h"
#include <stdint.h>
#include <string.h>
#ifdef USURPER_WARDEN_BAKED
#define SLOTS 64u
#else
#define SLOTS 256u
#endif
#define EVENTS US_GEOMETRY_CACHE_EVENTS
typedef struct {uint16_t x,y,width,flush;union {float coverage;uint32_t alpha;} value;} event_t;
typedef struct {uint32_t hash,stamp,key_bytes,event_count;uint8_t valid,overflow,alpha_mode;uint8_t key[US_GEOMETRY_CACHE_KEY_MAX];event_t events[EVENTS];} entry_t;
static entry_t *entries,*recording;
static unsigned enabled=1,hits,misses;static uint32_t clock;
/* A new resolved-alpha shape is rendered normally but first only remembered
 * as an admission hint. Recording starts on a repeated hint. Hash collisions
 * can cause extra recording; actual replay still requires full key equality. */
static uint32_t alpha_seen_hash[256];
static uint16_t alpha_seen_size[256];
static unsigned equal(const uint8_t *a,const uint8_t *b,unsigned bytes){for(unsigned i=0;i<bytes;++i)if(a[i]!=b[i])return 0;return 1;}
size_t us_geometry_cache_size(void){return SLOTS*sizeof(entry_t);}
void us_geometry_cache_attach(void *memory,size_t bytes){
    entries=bytes>=us_geometry_cache_size()?memory:NULL;recording=NULL;hits=misses=clock=0;
    memset(alpha_seen_size,0,sizeof(alpha_seen_size));
    if(entries)for(unsigned i=0;i<SLOTS;++i){entries[i].valid=0;entries[i].stamp=0;}
}
void us_geometry_cache_enable(unsigned value){enabled=value;recording=NULL;}
unsigned us_geometry_cache_available(void){return enabled&&entries!=NULL;}
unsigned us_geometry_cache_hits(void){return hits;}
unsigned us_geometry_cache_misses(void){return misses;}
static unsigned begin(const void *key,unsigned bytes,void (*span)(void *,int,int,unsigned),void (*coverage)(void *,int,int,unsigned,float),void (*alpha)(void *,int,int,unsigned,unsigned),void (*flush)(void *),void *context){
    recording=NULL;if(!entries||!enabled||bytes>US_GEOMETRY_CACHE_KEY_MAX)return 0;
    unsigned mode=alpha?2u:coverage?1u:0u;
    const uint8_t *data=key;uint32_t hash=2166136261u;for(unsigned i=0;i<bytes;++i)hash=(hash^data[i])*16777619u;
    if(mode==2u){
        unsigned slot=(hash^(hash>>16))&255u;
        if(alpha_seen_size[slot]!=bytes+1u||alpha_seen_hash[slot]!=hash){
            alpha_seen_size[slot]=(uint16_t)(bytes+1u);alpha_seen_hash[slot]=hash;
            ++misses;return 0;
        }
    }
    entry_t *oldest=NULL;
    for(unsigned i=0;i<SLOTS;++i){entry_t *entry=&entries[i];
        if(entry->valid&&entry->alpha_mode==mode&&entry->hash==hash&&entry->key_bytes==bytes&&equal(entry->key,key,bytes)){
            ++hits;entry->stamp=++clock;
            for(unsigned event=0;event<entry->event_count;++event){const event_t *e=&entry->events[event];if(e->flush)flush(context);else if(alpha)alpha(context,e->x,e->y,e->width,e->value.alpha);else if(coverage)coverage(context,e->x,e->y,e->width,e->value.coverage);else span(context,e->x,e->y,e->width);}
            return 1;
        }
        if(!oldest||(!entry->valid&&oldest->valid)||(entry->valid==oldest->valid&&entry->stamp<oldest->stamp))oldest=entry;
    }
    ++misses;oldest->valid=0;oldest->overflow=0;oldest->alpha_mode=(uint8_t)mode;oldest->hash=hash;oldest->key_bytes=bytes;oldest->event_count=0;oldest->stamp=++clock;memcpy(oldest->key,key,bytes);recording=oldest;return 0;
}
unsigned us_geometry_cache_begin(const void *key,unsigned bytes,void (*span)(void *,int,int,unsigned),void (*flush)(void *),void *context){return begin(key,bytes,span,NULL,NULL,flush,context);}
unsigned us_geometry_cache_begin_coverage(const void *key,unsigned bytes,void (*span)(void *,int,int,unsigned,float),void (*flush)(void *),void *context){return begin(key,bytes,NULL,span,NULL,flush,context);}
unsigned us_geometry_cache_begin_alpha(const void *key,unsigned bytes,void (*span)(void *,int,int,unsigned,unsigned),void (*flush)(void *),void *context){return begin(key,bytes,NULL,NULL,span,flush,context);}
static void record(event_t event){if(!recording)return;if(recording->event_count==EVENTS){recording->overflow=1;return;}recording->events[recording->event_count++]=event;}
void us_geometry_cache_span(int x,int y,unsigned width){
    if(x<0||y<0||x>65535||y>65535||width>65535){if(recording)recording->overflow=1;return;}
    record((event_t){(uint16_t)x,(uint16_t)y,(uint16_t)width,0,{1}});
}
void us_geometry_cache_coverage(int x,int y,unsigned width,float coverage){
    if(x<0||y<0||x>65535||y>65535||width>65535||coverage<0||coverage>1){if(recording)recording->overflow=1;return;}
    record((event_t){(uint16_t)x,(uint16_t)y,(uint16_t)width,0,{coverage}});
}
void us_geometry_cache_alpha(int x,int y,unsigned width,unsigned alpha){
    if(x<0||y<0||x>65535||y>65535||width>65535||alpha>255u){if(recording)recording->overflow=1;return;}
    event_t event={(uint16_t)x,(uint16_t)y,(uint16_t)width,0,{0}};event.value.alpha=alpha;record(event);
}
void us_geometry_cache_flush(void){record((event_t){0,0,0,1,{0}});}
void us_geometry_cache_end(void){if(recording)recording->valid=(uint8_t)!recording->overflow;recording=NULL;}
