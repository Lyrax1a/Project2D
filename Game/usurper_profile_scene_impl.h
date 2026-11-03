/* Included once, at the end of usurper_scene.c with USURPER_PROFILE_ENABLED.
 * Keeps existing host verifiers source-compatible until profile.c is linked. */
#include "usurper_profile_catalog.h"

void usurper_scene_profile_rules(us_profile_rules_t *rules) {
    if(rules)us_profile_catalog_rules(rules);
}

int usurper_scene_profile_export(us_profile_t *out) {
    if(!out)return US_PROFILE_ARGUMENT;
    if(phase!=USURPER_PHASE_MENU && phase!=USURPER_PHASE_TECH)return US_PROFILE_BUSY;
    us_profile_t value;us_profile_defaults(&value,US_PROFILE_PROGRESSION|US_PROFILE_LOADOUT|US_PROFILE_SETTINGS);
    value.music_volume=scene_settings.music_volume;value.sfx_volume=scene_settings.sfx_volume;
    value.settings_flags=scene_settings.flags;value.control_mode=scene_settings.control_mode;
    value.wallet=alloy_wallet;value.completed_mask=completed_stage_mask;
    value.species_seen_mask=species_seen_mask;value.stage=campaign_stage_id;
    value.tutorial_completed=tutorial_seen;value.visited_mask=profile_visited_stage_mask;
    value.form_index=selected_loadout.form_index;value.starting_species=selected_loadout.starting_species;
    value.support_species[0]=selected_loadout.support_species[0];
    value.support_species[1]=selected_loadout.support_species[1];
    for(unsigned i=0;i<US_PROFILE_TECH_COUNT;++i) {
        if(tech_owned[i]>255u)return US_PROFILE_RANGE;
        value.tech_owned[i]=(uint8_t)tech_owned[i];
    }
    us_profile_rules_t rules;us_profile_catalog_rules(&rules);
    int status=us_profile_validate(&value,&rules);
    if(status)return status;
    *out=value;return US_PROFILE_OK;
}

int usurper_scene_profile_import(const us_profile_t *input) {
    if(!input)return US_PROFILE_ARGUMENT;
    if(phase!=USURPER_PHASE_MENU && phase!=USURPER_PHASE_TECH)return US_PROFILE_BUSY;
    us_profile_t value=*input;
    us_profile_rules_t rules;us_profile_catalog_rules(&rules);
    int status=us_profile_validate(&value,&rules);
    if(status)return status;
    usurper_loadout_t loadout={value.form_index,value.starting_species,
                              {value.support_species[0],value.support_species[1]}};
    if(!us_loadout_valid(&loadout,value.completed_mask))return US_PROFILE_LOCKED;
    /* All checks precede mutation. Import never calls finish_run or awards
     * abandoned income; only settled wallet/research state is serialized. */
    alloy_wallet=value.wallet;completed_stage_mask=value.completed_mask;
    cleared_sectors=us_campaign_completed_prefix(completed_stage_mask);
    species_seen_mask=(uint16_t)value.species_seen_mask;campaign_stage_id=value.stage;
    tutorial_seen=value.tutorial_completed;profile_visited_stage_mask=value.visited_mask;
    for(unsigned i=0;i<US_PROFILE_TECH_COUNT;++i)tech_owned[i]=value.tech_owned[i];
    selected_loadout=loadout;
    if(value.features&US_PROFILE_SETTINGS){
        scene_settings=(usurper_settings_t){value.music_volume,value.sfx_volume,value.settings_flags,value.control_mode};
        usurper_audio_configure(&scene_settings);
    }
    reset_run();reset_tech_view();
    last_run_alloy=0;packet_size=packet_resync=0;
    menu_confirm=menu_confirm_selection=0;
    if(phase==USURPER_PHASE_MENU)menu_demo_reset();
    message("PROFILE RESTORED / HOST FILE REQUIRED FOR POWER LOSS");
    return US_PROFILE_OK;
}
