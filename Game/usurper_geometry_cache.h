#ifndef USURPER_GEOMETRY_CACHE_H
#define USURPER_GEOMETRY_CACHE_H
#include <stddef.h>
#define US_GEOMETRY_CACHE_KEY_MAX 576u
#define US_GEOMETRY_CACHE_EVENTS 8192u
void us_geometry_cache_attach(void *,size_t bytes);
void us_geometry_cache_enable(unsigned enabled);
unsigned us_geometry_cache_available(void);
unsigned us_geometry_cache_begin(const void *key,unsigned bytes,void (*span)(void *,int,int,unsigned),void (*flush)(void *),void *context);
void us_geometry_cache_span(int x,int y,unsigned width);
unsigned us_geometry_cache_begin_coverage(const void *key,unsigned bytes,void (*span)(void *,int,int,unsigned,float),void (*flush)(void *),void *context);
/* Key must include the exact paint opacity used to resolve coverage. Color,
 * source addresses and conversion masks remain live in the replay callback. */
unsigned us_geometry_cache_begin_alpha(const void *key,unsigned bytes,void (*span)(void *,int,int,unsigned,unsigned),void (*flush)(void *),void *context);
void us_geometry_cache_alpha(int x,int y,unsigned width,unsigned alpha);
void us_geometry_cache_coverage(int x,int y,unsigned width,float coverage);
void us_geometry_cache_flush(void);
void us_geometry_cache_end(void);
unsigned us_geometry_cache_hits(void);
unsigned us_geometry_cache_misses(void);
size_t us_geometry_cache_size(void);
#endif
