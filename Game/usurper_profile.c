/* SPDX-License-Identifier: MIT */
#include "usurper_profile.h"
#include <string.h>

#define PROFILE_MAGIC 0x31525055u /* UPR1 */
#define PAYLOAD_OFFSET 32u
#define TECH_OFFSET 96u

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); p[2] = (uint8_t)(n >> 16); p[3] = (uint8_t)(n >> 24);
}
uint32_t us_profile_crc32(const void *bytes, uint32_t count) {
    const uint8_t *p = (const uint8_t *)bytes;
    uint32_t crc = ~0u;
    while (count--) {
        crc ^= *p++;
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void us_profile_defaults(us_profile_t *p, uint32_t features) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->features = features;
    p->species_seen_mask = 3u; /* original dart/skimmer initial observations */
    p->form_index = -1;
    p->music_volume = 50u; p->sfx_volume = 80u;
    p->settings_flags = 7u; /* music, effects, screen shake preferences */
}

static unsigned completed_prefix(uint32_t mask) {
    unsigned n = 0;
    while (n < 8u && (mask & (1u << n))) ++n;
    return n;
}
static unsigned species_unlocked(uint32_t species, uint32_t mask) {
    static const uint8_t bosses[5] = {0,0,1,2,2};
    return species == 0u || (species <= 5u && (mask & (1u << bosses[species - 1u])));
}
static unsigned form_unlocked(int32_t form, uint32_t mask) {
    if (form == -1) return 1;
    if (form == 0 || form == 6) return mask & 1u;
    if (form == 1) return mask & 2u;
    if (form == 2 || form == 9) return mask & 4u;
    return 0;
}
static int rules_valid(const us_profile_rules_t *r) {
    if (!r || !r->catalog_hash) return 0;
    for (unsigned i = 0; i < US_PROFILE_TECH_COUNT; ++i) {
        if (r->maximum_owned[i] > 8u || r->base_rank[i] > 8u ||
            r->maximum_owned[i] + r->base_rank[i] > 8u ||
            r->parent[i] < -1 || r->parent[i] >= (int)US_PROFILE_TECH_COUNT ||
            r->parent_rank[i] > 8u || (r->required_seen[i] & ~8191u)) return 0;
        for (unsigned j = 0; j < 8; ++j) if (r->rank_gate[i][j] > 8u) return 0;
    }
    return 1;
}
int us_profile_validate(const us_profile_t *p, const us_profile_rules_t *r) {
    if (!p || !r) return US_PROFILE_ARGUMENT;
    if (!rules_valid(r)) return US_PROFILE_RULES;
    if (!(p->features & US_PROFILE_PROGRESSION) || (p->features & ~US_PROFILE_FEATURES)) return US_PROFILE_UNSUPPORTED;
    if (p->wallet > US_PROFILE_WALLET_MAX || p->completed_mask > 255u || p->species_seen_mask > 8191u ||
        p->stage > 7u || p->tutorial_completed > 1u || p->visited_mask > 255u ||
        p->form_index < -1 || p->form_index > 11 || p->starting_species > 5u ||
        p->support_species[0] > 5u || p->support_species[1] > 5u ||
        p->music_volume > 100u || p->sfx_volume > 100u || p->settings_flags > 7u ||
        p->control_mode > 2u) return US_PROFILE_RANGE;
    if (!(p->features & US_PROFILE_LOADOUT) &&
        (p->form_index != -1 || p->starting_species || p->support_species[0] || p->support_species[1]))
        return US_PROFILE_UNSUPPORTED;
    if (!form_unlocked(p->form_index,p->completed_mask) ||
        !species_unlocked(p->starting_species,p->completed_mask) ||
        !species_unlocked(p->support_species[0],p->completed_mask) ||
        !species_unlocked(p->support_species[1],p->completed_mask)) return US_PROFILE_LOCKED;
    unsigned prefix = completed_prefix(p->completed_mask);
    for (unsigned i = 0; i < US_PROFILE_TECH_COUNT; ++i) {
        if (p->tech_owned[i] > r->maximum_owned[i]) return US_PROFILE_RANGE;
        if (!p->tech_owned[i]) continue;
        unsigned rank = p->tech_owned[i] + r->base_rank[i];
        if ((p->species_seen_mask & r->required_seen[i]) != r->required_seen[i] ||
            prefix < r->rank_gate[i][rank - 1u]) return US_PROFILE_LOCKED;
        int parent = r->parent[i];
        if (parent >= 0 && p->tech_owned[parent] + r->base_rank[parent] < r->parent_rank[i])
            return US_PROFILE_LOCKED;
    }
    return US_PROFILE_OK;
}

int us_profile_encode(uint8_t blob[US_PROFILE_BYTES], unsigned slot, uint32_t sequence,
                      const us_profile_t *p, const us_profile_rules_t *r) {
    if (!blob) return US_PROFILE_ARGUMENT;
    if (slot >= US_PROFILE_SLOT_COUNT) return US_PROFILE_SLOT;
    int status = us_profile_validate(p,r);
    if (status) return status;
    if (!sequence) return US_PROFILE_SEQUENCE;
    uint8_t temporary[US_PROFILE_BYTES] = {0};
    put32(temporary,PROFILE_MAGIC); put32(temporary + 4,US_PROFILE_VERSION);
    put32(temporary + 8,US_PROFILE_BYTES); put32(temporary + 12,slot);
    put32(temporary + 16,sequence); put32(temporary + 20,r->catalog_hash);
    put32(temporary + 24,p->features);
    const uint32_t values[16] = {
        p->wallet,p->completed_mask,p->species_seen_mask,p->stage,p->tutorial_completed,p->visited_mask,
        (uint32_t)p->form_index,p->starting_species,p->support_species[0],p->support_species[1],
        p->music_volume,p->sfx_volume,p->settings_flags,p->control_mode,0,US_PROFILE_TECH_COUNT
    };
    for (unsigned i = 0; i < 16; ++i) put32(temporary + PAYLOAD_OFFSET + i * 4,values[i]);
    memcpy(temporary + TECH_OFFSET,p->tech_owned,US_PROFILE_TECH_COUNT);
    put32(temporary + US_PROFILE_BYTES - 4,us_profile_crc32(temporary,US_PROFILE_BYTES - 4));
    memcpy(blob,temporary,sizeof(temporary));
    return US_PROFILE_OK;
}

int us_profile_decode(const void *data, uint32_t bytes, unsigned expected_slot,
                      const us_profile_rules_t *r, us_profile_t *p, uint32_t *sequence) {
    if (!data || !p || !r) return US_PROFILE_ARGUMENT;
    if (expected_slot >= US_PROFILE_SLOT_COUNT) return US_PROFILE_SLOT;
    if (bytes != US_PROFILE_BYTES) return US_PROFILE_FORMAT;
    const uint8_t *blob = (const uint8_t *)data;
    if (get32(blob) != PROFILE_MAGIC || get32(blob + 4) != US_PROFILE_VERSION ||
        get32(blob + 8) != US_PROFILE_BYTES || get32(blob + 28) || get32(blob + 88) ||
        get32(blob + 92) != US_PROFILE_TECH_COUNT) return US_PROFILE_FORMAT;
    if (get32(blob + 252) != us_profile_crc32(blob,252)) return US_PROFILE_CRC;
    if (get32(blob + 12) != expected_slot) return US_PROFILE_SLOT;
    if (get32(blob + 20) != r->catalog_hash) return US_PROFILE_RULES;
    if (!get32(blob + 16)) return US_PROFILE_SEQUENCE;
    for (unsigned i = TECH_OFFSET + US_PROFILE_TECH_COUNT; i < 252; ++i)
        if (blob[i]) return US_PROFILE_FORMAT;
    us_profile_t temporary;
    us_profile_defaults(&temporary,get32(blob + 24));
    temporary.wallet = get32(blob + 32); temporary.completed_mask = get32(blob + 36);
    temporary.species_seen_mask = get32(blob + 40); temporary.stage = get32(blob + 44);
    temporary.tutorial_completed = get32(blob + 48); temporary.visited_mask = get32(blob + 52);
    uint32_t form = get32(blob + 56);
    if (form != ~0u && form > 11u) return US_PROFILE_RANGE;
    temporary.form_index = form == ~0u ? -1 : (int32_t)form;
    temporary.starting_species = get32(blob + 60);
    temporary.support_species[0] = get32(blob + 64); temporary.support_species[1] = get32(blob + 68);
    temporary.music_volume = get32(blob + 72); temporary.sfx_volume = get32(blob + 76);
    temporary.settings_flags = get32(blob + 80); temporary.control_mode = get32(blob + 84);
    memcpy(temporary.tech_owned,blob + TECH_OFFSET,US_PROFILE_TECH_COUNT);
    int status = us_profile_validate(&temporary,r);
    if (status) return status;
    *p = temporary;
    if (sequence) *sequence = get32(blob + 16);
    return US_PROFILE_OK;
}

void us_profile_store_init(us_profile_store_t *store) { if (store) memset(store,0,sizeof(*store)); }

static int latest(const us_profile_store_t *store, unsigned slot, const us_profile_rules_t *rules,
                  us_profile_t *profile, uint32_t *sequence, unsigned *index) {
    if (!store || !profile || !rules) return US_PROFILE_ARGUMENT;
    if (slot >= US_PROFILE_SLOT_COUNT) return US_PROFILE_SLOT;
    us_profile_t candidates[2]; uint32_t seq[2] = {0,0};
    int a = us_profile_decode(store->records[slot][0],US_PROFILE_BYTES,slot,rules,&candidates[0],&seq[0]);
    int b = us_profile_decode(store->records[slot][1],US_PROFILE_BYTES,slot,rules,&candidates[1],&seq[1]);
    if (a && b) return US_PROFILE_EMPTY;
    unsigned selected = a ? 1u : b ? 0u : seq[1] > seq[0] ? 1u : 0u;
    *profile = candidates[selected];
    if (sequence) *sequence = seq[selected];
    if (index) *index = selected;
    return US_PROFILE_OK;
}
int us_profile_store_load(const us_profile_store_t *store, unsigned slot,
                          const us_profile_rules_t *rules, us_profile_t *profile, uint32_t *sequence) {
    return latest(store,slot,rules,profile,sequence,0);
}
int us_profile_store_save(us_profile_store_t *store, unsigned slot,
                          const us_profile_t *p, const us_profile_rules_t *rules) {
    if (!store) return US_PROFILE_ARGUMENT;
    if (slot >= US_PROFILE_SLOT_COUNT) return US_PROFILE_SLOT;
    int status = us_profile_validate(p,rules);
    if (status) return status;
    us_profile_t old; uint32_t seq = 0; unsigned index = 1;
    status = latest(store,slot,rules,&old,&seq,&index);
    if (status && status != US_PROFILE_EMPTY) return status;
    if (seq == ~0u) return US_PROFILE_SEQUENCE;
    uint8_t temporary[US_PROFILE_BYTES];
    status = us_profile_encode(temporary,slot,seq + 1u,p,rules);
    if (status) return status;
    /* Old copy stays untouched. A torn target copy fails CRC on next read. */
    memcpy(store->records[slot][index ^ 1u],temporary,sizeof(temporary));
    return US_PROFILE_OK;
}
int us_profile_store_export(const us_profile_store_t *store, unsigned slot,
                            const us_profile_rules_t *rules, uint8_t blob[US_PROFILE_BYTES]) {
    if (!blob) return US_PROFILE_ARGUMENT;
    us_profile_t p; unsigned index;
    int status = latest(store,slot,rules,&p,0,&index);
    if (status) return status;
    memcpy(blob,store->records[slot][index],US_PROFILE_BYTES);
    return US_PROFILE_OK;
}
int us_profile_store_import(us_profile_store_t *store, unsigned slot,
                            const us_profile_rules_t *rules, const void *blob, uint32_t bytes) {
    us_profile_t p;
    int status = us_profile_decode(blob,bytes,slot,rules,&p,0);
    return status ? status : us_profile_store_save(store,slot,&p,rules);
}
int us_profile_store_reset(us_profile_store_t *store, unsigned slot,
                           const us_profile_rules_t *rules, uint32_t features) {
    us_profile_t p; us_profile_defaults(&p,features);
    return us_profile_store_save(store,slot,&p,rules);
}
