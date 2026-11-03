#ifndef USURPER_LOADOUT_H
#define USURPER_LOADOUT_H
#include <stdint.h>

/* Original forms.gd indices and native scene species IDs are intentionally
 * distinct. This exhibition exposes only the five first-three-stage rewards. */
typedef struct {
    int32_t form_index;
    uint32_t starting_species;
    uint32_t support_species[2];
} usurper_loadout_t;

static inline unsigned us_loadout_species_unlocked(unsigned species,unsigned mask) {
    unsigned boss;
    if(species<1u||species>5u)return 0;
    boss=species<=2u?0u:species==3u?1u:2u;
    return (mask&(1u<<boss))!=0;
}
static inline unsigned us_loadout_form_species(int32_t index) {
    return index==0?2u:index==1?3u:index==2?4u:index==6?1u:index==9?5u:0u;
}
static inline unsigned us_loadout_form_unlocked(int32_t index,unsigned mask) {
    return us_loadout_species_unlocked(us_loadout_form_species(index),mask);
}
static inline unsigned us_loadout_support_slots(unsigned mask) {
    return mask&8u?2u:mask&2u?1u:0u;
}
static inline int us_loadout_valid(const usurper_loadout_t *value,unsigned mask) {
    if(!value || (value->form_index!=-1 && !us_loadout_form_unlocked(value->form_index,mask)))return 0;
    if(value->starting_species && !us_loadout_species_unlocked(value->starting_species,mask))return 0;
    for(unsigned i=0;i<2u;++i)
        if(value->support_species[i] && !us_loadout_species_unlocked(value->support_species[i],mask))return 0;
    return 1;
}
static inline void us_loadout_reset(usurper_loadout_t *value) {
    *value=(usurper_loadout_t){-1,0,{0,0}};
}
/* Mirrors forms.from_saved / starting_from_saved and growth_session fallback
 * order for the exhibition subset. Explicit setters can still choose base/none.
 * Called when a new reward is earned, never while importing a valid selection. */
static inline void us_loadout_reward_defaults(usurper_loadout_t *value,unsigned mask) {
    const int32_t order[]={0,1,2,6,9};
    if(!us_loadout_form_unlocked(value->form_index,mask)) {
        value->form_index=-1;
        for(unsigned i=0;i<5u;++i)if(us_loadout_form_unlocked(order[i],mask)){value->form_index=order[i];break;}
    }
    if(!us_loadout_species_unlocked(value->starting_species,mask)) {
        unsigned species=us_loadout_form_species(value->form_index);
        value->starting_species=us_loadout_species_unlocked(species,mask)?species:us_loadout_species_unlocked(1,mask)?1u:0u;
    }
    for(unsigned i=0;i<2u;++i)
        if(!us_loadout_species_unlocked(value->support_species[i],mask))
            value->support_species[i]=us_loadout_species_unlocked(2,mask)?2u:0u;
}
#endif
