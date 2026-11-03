#ifndef USURPER_THREAT_SORT_H
#define USURPER_THREAT_SORT_H
typedef struct {float distance_squared;unsigned index;} us_threat_order_t;
void us_threat_sort(us_threat_order_t *,unsigned count);
#endif
