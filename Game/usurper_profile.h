#ifndef USURPER_PROFILE_H
#define USURPER_PROFILE_H

#include <stdint.h>

/* Board-native campaign interchange, not Godot's JSON format. All RAM stores
 * below are volatile; durable persistence requires host export to a file. */
#define US_PROFILE_VERSION 1u
#define US_PROFILE_BYTES 256u
#define US_PROFILE_TECH_COUNT 44u
#define US_PROFILE_SLOT_NORMAL 0u
#define US_PROFILE_SLOT_DEMO 1u
#define US_PROFILE_SLOT_COUNT 2u
#define US_PROFILE_PROGRESSION 1u
#define US_PROFILE_LOADOUT 2u
#define US_PROFILE_SETTINGS 4u
#define US_PROFILE_FEATURES 7u
#define US_PROFILE_WALLET_MAX 1000000000u

enum us_profile_result {
    US_PROFILE_OK = 0,
    US_PROFILE_ARGUMENT = -1,
    US_PROFILE_FORMAT = -2,
    US_PROFILE_CRC = -3,
    US_PROFILE_SLOT = -4,
    US_PROFILE_RULES = -5,
    US_PROFILE_RANGE = -6,
    US_PROFILE_LOCKED = -7,
    US_PROFILE_EMPTY = -8,
    US_PROFILE_SEQUENCE = -9,
    US_PROFILE_BUSY = -10,
    US_PROFILE_UNSUPPORTED = -11
};

typedef struct {
    uint32_t features;
    uint32_t wallet, completed_mask, species_seen_mask, stage;
    uint32_t tutorial_completed, visited_mask;
    int32_t form_index; /* original forms.gd IDs: -1, 0, 1, 2, 6, 9 */
    uint32_t starting_species, support_species[2]; /* native IDs 0..5 */
    uint32_t music_volume, sfx_volume, settings_flags, control_mode;
    uint8_t tech_owned[US_PROFILE_TECH_COUNT]; /* purchased ranks, excludes base */
} us_profile_t;

typedef struct {
    uint32_t catalog_hash; /* includes stable IDs/order and all relevant rules */
    uint8_t maximum_owned[US_PROFILE_TECH_COUNT];
    uint8_t base_rank[US_PROFILE_TECH_COUNT];
    int8_t parent[US_PROFILE_TECH_COUNT]; /* -1 if none */
    uint8_t parent_rank[US_PROFILE_TECH_COUNT];
    uint16_t required_seen[US_PROFILE_TECH_COUNT];
    uint8_t rank_gate[US_PROFILE_TECH_COUNT][8];
} us_profile_rules_t;

typedef struct {
    uint8_t records[US_PROFILE_SLOT_COUNT][2][US_PROFILE_BYTES];
} us_profile_store_t;

void us_profile_defaults(us_profile_t *profile, uint32_t features);
int us_profile_validate(const us_profile_t *profile, const us_profile_rules_t *rules);
uint32_t us_profile_crc32(const void *bytes, uint32_t count);
int us_profile_encode(uint8_t blob[US_PROFILE_BYTES], unsigned slot, uint32_t sequence,
                      const us_profile_t *profile, const us_profile_rules_t *rules);
/* On any error, profile and sequence remain unchanged. */
int us_profile_decode(const void *blob, uint32_t bytes, unsigned expected_slot,
                      const us_profile_rules_t *rules, us_profile_t *profile,
                      uint32_t *sequence);

/* Two independent tagged slots; each has newest/previous CRC-protected copies.
 * Recovery selects the highest valid sequence, allowing a torn latest write to
 * fall back. Caller is responsible for external storage and cache maintenance. */
void us_profile_store_init(us_profile_store_t *store);
int us_profile_store_save(us_profile_store_t *store, unsigned slot,
                          const us_profile_t *profile, const us_profile_rules_t *rules);
int us_profile_store_load(const us_profile_store_t *store, unsigned slot,
                          const us_profile_rules_t *rules, us_profile_t *profile,
                          uint32_t *sequence);
int us_profile_store_export(const us_profile_store_t *store, unsigned slot,
                            const us_profile_rules_t *rules, uint8_t blob[US_PROFILE_BYTES]);
int us_profile_store_import(us_profile_store_t *store, unsigned slot,
                            const us_profile_rules_t *rules, const void *blob, uint32_t bytes);
int us_profile_store_reset(us_profile_store_t *store, unsigned slot,
                           const us_profile_rules_t *rules, uint32_t features);

#endif
