#ifndef USURPER_PROFILE_CATALOG_H
#define USURPER_PROFILE_CATALOG_H
#include "usurper_profile.h"
#include "../Assets/Generated/usurper_generated_content.h"
#include <string.h>

_Static_assert(US_PROFILE_TECH_COUNT == USURPER_TECH_NODE_COUNT,
               "Profile version needs updating when the technology catalog changes");

static inline uint32_t us_profile_hash_u32(uint32_t hash,uint32_t value) {
    for(unsigned b=0;b<4;++b){hash=(hash^(value&255u))*16777619u;value>>=8;}
    return hash;
}
static inline uint32_t us_profile_hash_string(uint32_t hash,const char *text) {
    do {hash=(hash^(uint8_t)*text)*16777619u;} while(*text++);
    return hash;
}
/* Hashes explicit little-endian values and UTF-8 IDs, never host pointers,
 * structure padding or localized display names. */
static inline void us_profile_catalog_rules(us_profile_rules_t *rules) {
    uint32_t hash=2166136261u;
    memset(rules,0,sizeof(*rules));
    hash=us_profile_hash_string(hash,"TileStorm-native-profile-v1");
    hash=us_profile_hash_u32(hash,US_PROFILE_TECH_COUNT);
    for(unsigned i=0;i<US_PROFILE_TECH_COUNT;++i) {
        const usurper_tech_node_t *node=&usurper_tech_nodes[i];
        rules->base_rank[i]=node->base_rank;
        rules->maximum_owned[i]=(uint8_t)(node->ranks-node->base_rank);
        rules->parent[i]=-1;
        if(node->parent[0])for(unsigned j=0;j<US_PROFILE_TECH_COUNT;++j)
            if(!strcmp(node->parent,usurper_tech_nodes[j].id)){rules->parent[i]=(int8_t)j;break;}
        rules->parent_rank[i]=node->parent_rank;
        rules->required_seen[i]=(uint16_t)(node->requires_seen_mask|node->species_mask);
        memcpy(rules->rank_gate[i],node->rank_gates,8);
        hash=us_profile_hash_string(hash,node->id);
        hash=us_profile_hash_string(hash,node->parent);
        hash=us_profile_hash_u32(hash,node->base_rank);
        hash=us_profile_hash_u32(hash,node->ranks);
        hash=us_profile_hash_u32(hash,node->parent_rank);
        hash=us_profile_hash_u32(hash,rules->required_seen[i]);
        for(unsigned j=0;j<8;++j) {
            hash=us_profile_hash_u32(hash,node->costs[j]);
            hash=us_profile_hash_u32(hash,node->rank_gates[j]);
            hash=us_profile_hash_u32(hash,node->effective_ranks[j]);
        }
    }
    rules->catalog_hash=hash;
}
#endif
