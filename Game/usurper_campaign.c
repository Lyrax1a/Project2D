#include "usurper_campaign.h"
#include "../Assets/Generated/usurper_campaign_content.h"

double us_campaign_birth_seconds(unsigned stage, unsigned boss_phase) {
    if (stage >= US_CAMPAIGN_STAGE_COUNT) return 1.0;
    return us_campaign_birth_seconds_data[stage][boss_phase != 0u];
}

/* IDs are the existing board ABI: Dart, Skimmer, Rammer, Gunner, Warden,
 * Splitter, Bomber, Leech, Lancer, Seeker, PhaseHunter, Carrier, Fragment. */
enum { DART = 1, SKIMMER, RAMMER, GUNNER, WARDEN, SPLITTER, BOMBER, LEECH,
       LANCER, SEEKER, PHASE_HUNTER, CARRIER, FRAGMENT };

const us_campaign_stage_t *us_campaign_stage(unsigned stage) {
    return stage < US_CAMPAIGN_STAGE_COUNT ? &us_campaign_stages[stage] : 0;
}

const us_campaign_unit_t *us_campaign_unit(unsigned species) {
    return species && species <= US_CAMPAIGN_UNIT_COUNT ? &us_campaign_units[species - 1u] : 0;
}
float us_campaign_enemy_speed_float(unsigned stage,unsigned species) {
    return stage<US_CAMPAIGN_STAGE_COUNT&&species&&species<=US_CAMPAIGN_UNIT_COUNT?
        us_campaign_enemy_speed_float_data[stage][species-1u]:0;
}

const us_campaign_form_t *us_campaign_form(unsigned index) {
    return index < US_CAMPAIGN_FORM_COUNT ? &us_campaign_forms[index] : 0;
}

const us_campaign_power_t *us_campaign_power(unsigned index) {
    return index < US_CAMPAIGN_FORM_COUNT ? &us_campaign_powers[index] : 0;
}

unsigned us_campaign_stage_available(unsigned stage) {
    /* The user explicitly requested direct access to all eight stages.
     * Earned rewards/forms still depend on completed_mask. */
    return stage < US_CAMPAIGN_STAGE_COUNT;
}

unsigned us_campaign_roster(unsigned stage, unsigned beat, unsigned boss_phase,
                            uint32_t seconds_ticks, uint8_t output[US_CAMPAIGN_ROSTER_MAX]) {
    const us_campaign_stage_t *data = us_campaign_stage(stage);
    if (!data || !output) return 0;
    unsigned count = 0;
    uint8_t first = data->basic[0], second = data->basic[1];
    if (stage == 7u) {
        output[count++] = GUNNER; output[count++] = GUNNER;
        if (beat % 4u == 0u) { output[count++] = WARDEN; output[count++] = WARDEN; }
        return count;
    }
    if (stage == 6u) {
        output[count++] = SPLITTER; output[count++] = SPLITTER;
        if (seconds_ticks >= 25u * 60u) output[count++] = SPLITTER;
        if (beat % 4u == 0u) output[count++] = CARRIER;
        return count;
    }
    if (stage == 5u) {
        if (beat % 3u == 0u) output[count++] = WARDEN;
        output[count++] = LANCER;
        if (seconds_ticks >= 20u * 60u && beat % 14u == 7u) output[count++] = LEECH;
        return count;
    }
    if (stage == 2u) {
        output[count++] = GUNNER;
        if (beat % 5u == 0u) output[count++] = WARDEN;
        return count;
    }
    if (stage == 3u) {
        output[count++] = RAMMER;
        if (boss_phase) {
            if (beat % 2u == 0u) output[count++] = GUNNER;
        } else {
            if (beat % 3u == 0u) output[count++] = GUNNER;
            if (seconds_ticks >= 30u * 60u && beat % 3u == 1u) output[count++] = RAMMER;
            if (seconds_ticks >= 40u * 60u && beat % 14u == 4u) output[count++] = PHASE_HUNTER;
        }
        return count;
    }
    if (stage == 4u) {
        output[count++] = SPLITTER;
        if (beat % 3u == 0u) output[count++] = BOMBER;
        if (seconds_ticks >= 18u * 60u && beat % 14u == 7u) output[count++] = LEECH;
        return count;
    }
    if (boss_phase) {
        if (stage == 0u) { first = data->tools[0]; second = data->tools[1]; }
        if (stage == 1u) { first = DART; second = RAMMER; }
        output[count++] = first;
        output[count++] = beat % 3u == 0u ? second : first;
        return count;
    }
    output[count++] = first; output[count++] = first;
    if (stage > 0u) {
        if (seconds_ticks >= 12u * 60u && beat % 3u == 0u) output[1] = second;
        if (seconds_ticks >= 20u * 60u) output[count++] = beat % 3u == 0u ? second : first;
        if (seconds_ticks >= 40u * 60u) output[count++] = first;
        if (seconds_ticks >= 40u * 60u && beat % 14u == 4u) output[count++] = data->elite;
        return count;
    }
    if (seconds_ticks >= 12u * 60u && beat % 3u == 0u) output[1] = second;
    if (seconds_ticks >= 20u * 60u) output[count++] = first;
    if (seconds_ticks >= 40u * 60u) output[count++] = second;
    if (seconds_ticks >= 40u * 60u && beat % 7u == 4u) output[count++] = data->elite;
    return count;
}

uint32_t us_campaign_enemy_health_q16(unsigned stage, unsigned species) {
    const us_campaign_unit_t *unit = us_campaign_unit(species);
    return unit && stage < US_CAMPAIGN_STAGE_COUNT ? unit->stage_health_q16[stage] : 0u;
}

uint32_t us_campaign_enemy_speed_q16(unsigned stage, unsigned species) {
    const us_campaign_stage_t *area = us_campaign_stage(stage);
    const us_campaign_unit_t *unit = us_campaign_unit(species);
    return area && unit ? (uint32_t)((uint64_t)unit->speed_q16 * area->enemy_speed_q16 / US_CAMPAIGN_Q16) : 0u;
}

unsigned us_campaign_form_unlocked(unsigned index, unsigned completed_mask) {
    const us_campaign_form_t *form = us_campaign_form(index);
    return form && form->boss < US_CAMPAIGN_STAGE_COUNT && (completed_mask & (1u << form->boss)) != 0u;
}

unsigned us_campaign_power_unlocked(unsigned index, unsigned completed_mask) {
    const us_campaign_power_t *power = us_campaign_power(index);
    return power && power->boss < US_CAMPAIGN_STAGE_COUNT && (completed_mask & (1u << power->boss)) != 0u;
}

unsigned us_campaign_support_slots(unsigned completed_mask) {
    return completed_mask & (1u << 3u) ? 2u : completed_mask & (1u << 1u) ? 1u : 0u;
}

unsigned us_campaign_completed_prefix(unsigned completed_mask) {
    unsigned count = 0u;
    while (count < US_CAMPAIGN_STAGE_COUNT && (completed_mask & (1u << count))) ++count;
    return count;
}

uint32_t us_campaign_first_reward(unsigned stage, unsigned completed_mask) {
    const us_campaign_stage_t *area = us_campaign_stage(stage);
    return area && !(completed_mask & (1u << stage)) ? area->first_reward : 0u;
}

static uint32_t rule_crc_byte(uint32_t value, uint8_t byte) {
    value ^= byte;
    for (unsigned bit = 0; bit < 8u; ++bit)
        value = (value >> 1) ^ ((value & 1u) ? 0xedb88320u : 0u);
    return value;
}

uint32_t us_campaign_rules_crc32(void) {
    static const uint8_t seconds[] = {1,11,12,17,18,19,20,24,25,29,30,39,40,59,60,120};
    uint32_t crc = 0xffffffffu;
    for (unsigned stage = 0; stage < 8u; ++stage)
        for (unsigned boss = 0; boss < 2u; ++boss)
            for (unsigned beat = 0; beat < 420u; ++beat)
                for (unsigned sample = 0; sample < sizeof(seconds); ++sample) {
                    uint32_t time = seconds[sample] * 60u;
                    uint8_t roster[US_CAMPAIGN_ROSTER_MAX];
                    unsigned count = us_campaign_roster(stage, beat, boss, time, roster);
                    const uint8_t prefix[] = {(uint8_t)stage, (uint8_t)beat, (uint8_t)(beat >> 8),
                                             (uint8_t)boss, (uint8_t)time, (uint8_t)(time >> 8), (uint8_t)count};
                    for (unsigned i = 0; i < sizeof(prefix); ++i) crc = rule_crc_byte(crc, prefix[i]);
                    for (unsigned i = 0; i < count; ++i) crc = rule_crc_byte(crc, roster[i]);
                }
    return crc ^ 0xffffffffu;
}
