/* Included after boss damage helpers, inside the native scene translation unit. */
static double form_technology_scale(unsigned species,unsigned active) {
    /* A base of 100 retains every existing integer percentage and multiplier
     * exactly in Q8. The new form shot itself remains fractional thereafter. */
    return tech_damage_q8_roster(10000u,species,active,0u)/25600.0;
}
static void damage_boss_form(unsigned target,double damage,unsigned rail) {
    if(!boss_active||(target==BOSS_TARGET_CORE&&boss_grace_ticks))return;
    uint32_t amount=(uint32_t)(damage*65536.0+0.5);
    if(campaign_stage_id==1u) {
        if(target!=BOSS_TARGET_CORE&&target-ENEMY_MAX>=US_MECHANIC_PARTS)return;
        us_mechanic_hit(&mechanic_boss,target==BOSS_TARGET_CORE?US_MECHANIC_CORE:target-ENEMY_MAX,amount,0);
        sync_mechanic_boss();
    } else if(campaign_stage_id==2u) {
        if(target!=BOSS_TARGET_CORE&&target-ENEMY_MAX>=US_ARCHON_PARTS)return;
        unsigned local=target==BOSS_TARGET_CORE?US_ARCHON_CORE:target-ENEMY_MAX;
        int32_t x=0,y=0;us_archon_target_position(&archon_boss,local,&x,&y);
        unsigned reward=us_archon_hit(&archon_boss,local,amount);
        if(reward)spawn_pickup(x/256,y/256,reward,0);
        sync_archon_boss();
    } else if(campaign_stage_id==0u&&target==BOSS_TARGET_CORE) {
        if(boss_armor_left)return;
        double total=form_core_damage_fraction+damage;
        unsigned whole=(unsigned)total;form_core_damage_fraction=total-whole;
        if(whole)boss_expose_damage(whole);
    } else damage_boss_target(target,(unsigned)(damage*Q8+0.5),rail);
}
static int form_gunner_target(void) {
    int chosen=-1;uint64_t nearest=(uint64_t)(320u*Q8)*(320u*Q8);
    for(unsigned visit=0;visit<ATTACK_TARGET_MAX;++visit) {
        unsigned i=visit<ENEMY_MAX?visit:visit==ENEMY_MAX?BOSS_TARGET_CORE:visit-1u;
        int32_t x,y;
        if(!attack_target_selectable(i)||!attack_target_position(i,&x,&y))continue;
        int64_t dx=(int64_t)x-player_x,dy=(int64_t)y-player_y;
        uint64_t squared=(uint64_t)(dx*dx+dy*dy);
        if(squared<nearest){nearest=squared;chosen=(int)i;}
    }
    return chosen;
}
static void form_gun_round(double base_damage) {
    int target=form_gunner_target();if(target<0)return;
    int32_t x,y;if(!attack_target_position((unsigned)target,&x,&y))return;
    unsigned heading=angle_to(x-player_x,y-player_y),split=0;
    if(tech_effect("J2"))for(unsigned i=0;i<unit_count;++i)split|=unit_species[i]==SPECIES_SPLITTER;
    unsigned shielded=tech_effect("J4")&&warden_guard_left>0;
    double damage=base_damage*(split?0.45:1.0)*(shielded?1.2:1.0)*form_technology_scale(SPECIES_GUNNER,0);
    unsigned count=split?3u:1u;
    for(unsigned i=0;i<count;++i) {
        /* Native scene directions are whole degrees; original spread is .19 rad. */
        unsigned angle=angle_offset(heading,((int)i-(int)(count/2u))*11);
        int shot=spawn_ally_projectile(player_x,player_y,angle,1u,420u,0u);
        if(shot>=0)projectiles[shot].form_damage=damage;
    }
}
static void ensure_form_sweep(void) {
    SkimmerSweep *scan=&skimmer_sweeps[SKIMMER_SWEEP_MAX];
    if(scan->ticks_left)return;
    memset(scan,0,sizeof(*scan));
    scan->form_owned=1;scan->ticks_left=1;
    scan->start_q16=scan->angle_q16=player_angle*65536u;
    unsigned omega=1152u+tech_effect("S3")*Q8;
    scan->step_q16=(unsigned)((uint64_t)omega*3754936u/(60u*Q8));
    scan->radius=(uint16_t)tech_capture_radius();scan->echo=tech_effect("S8")!=0;
    scan->repeat_ticks=(uint16_t)((360u*65536u+scan->step_q16-1u-scan->step_q16*6u/100u)/scan->step_q16);
    scan->form_damage=(10.0+2*tech_effect("S4"))*form_technology_scale(SPECIES_SKIMMER,1)*0.5;
}
static int start_form_dash(void) {
    if(selected_loadout.form_index!=1||phase!=USURPER_PHASE_BATTLE||defeat_pending||aim_is_held||rammer_charge.remaining_q16)return 0;
    if(!us_form_dash_begin(&form_combat))return 0;
    memset(&rammer_charge,0,sizeof(rammer_charge));rammer_body_instance_id=0;
    rammer_charge.form_owned=1;
    rammer_charge.duration_q16=us_form_dash_ticks_q16(tech_effect("A2"),tech_effect("ZA"),release_charge_q16);
    rammer_charge.remaining_q16=rammer_charge.duration_q16;
    int direction_x,direction_y;
    direction_q8(player_angle,&direction_x,&direction_y);
    rammer_charge.direction_x=direction_x;rammer_charge.direction_y=direction_y;
    rammer_charge.angle=player_angle;player_vx=player_vy=0;
    if(tech_effect("A7"))player_invincible_ticks=max_u(player_invincible_ticks,(rammer_charge.duration_q16+65535u)/65536u);
    return 1;
}
void usurper_scene_form_aim(unsigned pressed) {
    if(pressed) {
        if(selected_loadout.form_index!=1||phase!=USURPER_PHASE_BATTLE||defeat_pending||aim_is_held||form_combat.dash_cooldown>0||rammer_charge.remaining_q16)return;
        aim_is_form=aim_is_held=1;release_charge_q16=0;
        aim_capture_target_index=candidate_index;
        aim_charge_ticks=aim_slow_phase=aim_wall_ticks=0;
    } else if(aim_is_held&&aim_is_form)commit_combat_aim();
}
static void form_combat_step(void) {
    if(selected_loadout.form_index!=0&&selected_loadout.form_index!=1&&selected_loadout.form_index!=2)return;
    unsigned gunner=selected_loadout.form_index==2;
    int target=selected_loadout.form_index==2?form_gunner_target():-1;
    us_form_rounds_t rounds=us_form_combat_tick(&form_combat,gunner,target>=0,
        gunner?tech_effect("F4"):0,gunner?tech_effect("G2"):0,gunner?tech_effect("G4"):0,gunner?tech_effect("G8"):0);
    for(unsigned i=0;i<rounds.count;++i)form_gun_round(rounds.damage[i]);
    if(selected_loadout.form_index==0)ensure_form_sweep();
}
