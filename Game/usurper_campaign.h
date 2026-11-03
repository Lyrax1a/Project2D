#ifndef USURPER_CAMPAIGN_H
#define USURPER_CAMPAIGN_H

#include <stdint.h>

#define US_CAMPAIGN_STAGE_COUNT 8u
#define US_CAMPAIGN_UNIT_COUNT 13u
#define US_CAMPAIGN_FORM_COUNT 12u
#define US_CAMPAIGN_ROSTER_MAX 8u
#define US_CAMPAIGN_Q16 65536u

typedef struct {
    const char *name;
    uint32_t hp_q16, interval_q16, support_q16, health_scale_q16;
    uint32_t upkeep_q16, enemy_speed_q16, enemy_damage_q16;
    uint32_t alloy_multiplier, reward, first_reward;
    uint32_t birth_interval_q16, boss_birth_interval_q16;
    uint8_t power, boss_kind, basic[2], tools[2], elite;
} us_campaign_stage_t;

typedef struct {
    const char *species;
    uint32_t health_q16, speed_q16, interval_q16, recovery_q16;
    uint16_t capture_work, alloy;
    uint32_t stage_health_q16[US_CAMPAIGN_STAGE_COUNT];
} us_campaign_unit_t;

typedef struct { const char *species, *name; uint8_t boss; } us_campaign_form_t;
typedef struct { const char *species; uint32_t cost_q16; uint8_t boss; } us_campaign_power_t;

const us_campaign_stage_t *us_campaign_stage(unsigned stage);
const us_campaign_unit_t *us_campaign_unit(unsigned species);
const us_campaign_form_t *us_campaign_form(unsigned index);
const us_campaign_power_t *us_campaign_power(unsigned index);
unsigned us_campaign_stage_available(unsigned stage);
double us_campaign_birth_seconds(unsigned stage, unsigned boss_phase);
unsigned us_campaign_roster(unsigned stage, unsigned beat, unsigned boss_phase,
                            uint32_t seconds_ticks, uint8_t output[US_CAMPAIGN_ROSTER_MAX]);
uint32_t us_campaign_enemy_health_q16(unsigned stage, unsigned species);
uint32_t us_campaign_enemy_speed_q16(unsigned stage, unsigned species);
float us_campaign_enemy_speed_float(unsigned stage,unsigned species);
unsigned us_campaign_form_unlocked(unsigned index, unsigned completed_mask);
unsigned us_campaign_power_unlocked(unsigned index, unsigned completed_mask);
unsigned us_campaign_support_slots(unsigned completed_mask);
unsigned us_campaign_completed_prefix(unsigned completed_mask);
uint32_t us_campaign_first_reward(unsigned stage, unsigned completed_mask);
uint32_t us_campaign_rules_crc32(void);

#endif
