#include "usurper_mechanic.h"
#include <string.h>

static float clamp_float(float value, float low, float high) { return value < low ? low : value > high ? high : value; }
static int32_t position_q16(float value) { return (int32_t)((double)value * US_FX_ONE); }
static void sync_pose(us_mechanic_t *boss) {
    boss->x = position_q16(boss->x_float); boss->y = position_q16(boss->y_float);
    boss->vx = position_q16(boss->vx_float); boss->vy = position_q16(boss->vy_float);
    boss->locked_x = position_q16(boss->locked_x_float); boss->locked_y = position_q16(boss->locked_y_float);
    int32_t angle = (int32_t)((double)boss->rotation * 180.0 / 3.14159265358979323846 * US_FX_ONE);
    angle %= US_FX_TURN; boss->angle = angle < 0 ? angle + US_FX_TURN : angle;
}
static void break_part(us_mechanic_t *boss, unsigned part) {
    if (!boss->part_hp_q16[part]) return;
    boss->part_hp_q16[part] = 0;
    --boss->remaining; ++boss->breaks;
    unsigned any = 0;
    for (unsigned i = boss->phase * 3u; i < boss->phase * 3u + 3u; ++i) any |= boss->part_hp_q16[i];
    if (!any) { boss->core_exposed = 1u; boss->exposed_at = boss->elapsed; boss->recovery = 2.0; }
}

void us_mechanic_init(us_mechanic_t *boss, uint32_t hp_q16, int32_t x, int32_t y, int32_t px, int32_t py) {
    memset(boss, 0, sizeof(*boss));
    boss->x = x; boss->y = y;
    boss->x_float = x / 65536.0f; boss->y_float = y / 65536.0f;
    boss->max_hp_q16 = boss->hp_q16 = hp_q16;
    boss->active = 1; boss->remaining = US_MECHANIC_PARTS;
    boss->shot_clock = 1.0; boss->exposed_at = -1.0;
    boss->angle = us_fx_angle(px - x, py - y);
    uint32_t length = us_fx_length(px - x, py - y);
    if (length) {
        boss->locked_x = (int32_t)((int64_t)(px - x) * US_FX_ONE / length);
        boss->locked_y = (int32_t)((int64_t)(py - y) * US_FX_ONE / length);
    }
    float dx = (px - x) / 65536.0f, dy = (py - y) / 65536.0f;
    float distance = us_fx_sqrt_float(dx * dx + dy * dy);
    boss->rotation = us_fx_angle_float(dx, dy);
    boss->locked_x_float = distance ? dx / distance : 0;
    boss->locked_y_float = distance ? dy / distance : 0;
    sync_pose(boss);
    for (unsigned i = 0; i < US_MECHANIC_PARTS; ++i) boss->part_hp_q16[i] = 90u * US_FX_ONE;
}

unsigned us_mechanic_target_present(const us_mechanic_t *boss, unsigned target) {
    if (!boss->active || target > US_MECHANIC_CORE) return 0;
    if (target == US_MECHANIC_CORE) return 1;
    return boss->part_hp_q16[target] && target / 3u == boss->phase;
}

unsigned us_mechanic_target_open(const us_mechanic_t *boss, unsigned target) {
    if (!us_mechanic_target_present(boss, target)) return 0;
    if (target == US_MECHANIC_CORE) return boss->core_exposed;
    return !boss->core_exposed && boss->attack_clock >= 1.23 && boss->attack_clock < 2.65;
}

unsigned us_mechanic_target_position(const us_mechanic_t *boss, unsigned target, int32_t *x, int32_t *y) {
    if (!us_mechanic_target_present(boss, target)) return 0;
    *x = boss->x; *y = boss->y;
    if (target == US_MECHANIC_CORE) return 1;
    float cx, sy; us_fx_direction_float(boss->rotation, &cx, &sy);
    int home_y = ((int)(target % 3u) - 1) * 30;
    *x = position_q16(boss->x_float + (-48 * cx - home_y * sy));
    *y = position_q16(boss->y_float + (-48 * sy + home_y * cx));
    return 1;
}

void us_mechanic_hit(us_mechanic_t *boss, unsigned target, uint32_t damage_q16, unsigned tags) {
    if (!boss->active || !damage_q16 || target > US_MECHANIC_CORE) return;
    if (target == US_MECHANIC_CORE && !boss->core_exposed && (tags & US_MECHANIC_HIT_GUARD)) {
        for (unsigned i = boss->phase * 3u; i < boss->phase * 3u + 3u; ++i)
            if (boss->part_hp_q16[i]) {
                uint32_t damage = damage_q16 / 3u;
                if (damage >= boss->part_hp_q16[i]) break_part(boss, i);
                else boss->part_hp_q16[i] -= damage;
                boss->part_flash[i] = 8u;
            }
        return;
    }
    if (!us_mechanic_target_open(boss, target)) return;
    if (target < US_MECHANIC_CORE) {
        if (damage_q16 >= boss->part_hp_q16[target]) break_part(boss, target);
        else boss->part_hp_q16[target] -= damage_q16;
        boss->part_flash[target] = 8u;
        return;
    }
    if (tags & US_MECHANIC_HIT_FRAGMENT) {
        if (boss->elapsed - boss->fragment_window > 0.2) { boss->fragment_window = boss->elapsed; boss->fragment_hits = 0; }
        ++boss->fragment_hits;
        if (boss->fragment_hits > 4u) damage_q16 /= 4u;
    }
    uint32_t floor = boss->phase == 0u ? boss->max_hp_q16 / 2u : 0u;
    uint64_t scaled = (uint64_t)damage_q16 * 4u;
    uint32_t available = boss->hp_q16 - floor;
    boss->hp_q16 -= scaled < available ? (uint32_t)scaled : available;
    if (!boss->hp_q16) { boss->active = 0u; return; }
    if (boss->phase == 0u && boss->hp_q16 <= floor) {
        boss->phase = 1u; boss->phase_started = boss->elapsed; boss->core_exposed = 0;
        boss->attack_clock = 0; boss->shot_clock = 1; boss->recovery = 0.8;
    }
}

void us_mechanic_guard_blocked(us_mechanic_t *boss) { boss->attack_clock = 1.23; boss->recovery = 0.85; boss->vx = boss->vy = 0; boss->vx_float = boss->vy_float = 0; }

void us_mechanic_step(us_mechanic_t *boss, int32_t px, int32_t py, us_mechanic_output_t *output) {
    output->count = 0;
    if (!boss->active) return;
    const double delta = 1.0 / 60.0;
    boss->elapsed += delta;
    for (unsigned i = 0; i < US_MECHANIC_PARTS; ++i) if (boss->part_flash[i]) --boss->part_flash[i];
    if (boss->recovery > 0) {
        boss->recovery -= delta; if (boss->recovery < 0) boss->recovery = 0;
        boss->vx_float = boss->vy_float = 0; sync_pose(boss); return;
    }
    boss->attack_clock += delta; boss->shot_clock -= delta;
    float dx = px / 65536.0f - boss->x_float, dy = py / 65536.0f - boss->y_float;
    float length = us_fx_sqrt_float(dx * dx + dy * dy);
    float ax = length ? dx / length : 0, ay = length ? dy / length : 0;
    float desired = us_fx_angle_float(dx, dy);
    if (boss->attack_clock >= 3.5) { boss->attack_clock -= 3.5; ++boss->attack_index; }
    float pace = (float)(((double)length - 140) * 0.95);
    pace = clamp_float(pace, -55, 130);
    boss->vx_float = ax * pace; boss->vy_float = ay * pace;
    if (boss->attack_clock < 0.42) {
        boss->rotation = us_fx_turn_float(boss->rotation, desired, delta * 6.5);
        us_fx_direction_float(boss->rotation, &boss->locked_x_float, &boss->locked_y_float);
    }
    if (boss->attack_clock < 0.65) boss->vx_float = boss->vy_float = 0;
    else if (boss->attack_clock < 1.23) { boss->vx_float = boss->locked_x_float * 680; boss->vy_float = boss->locked_y_float * 680; }
    else if (boss->attack_clock < 2.65) boss->vx_float = boss->vy_float = 0;
    if (boss->attack_clock >= 2.65) boss->rotation = us_fx_turn_float(boss->rotation, desired, delta * 3.5);
    float motion_x = boss->vx_float * (float)delta, motion_y = boss->vy_float * (float)delta;
    if (boss->attack_clock >= 0.65 && boss->attack_clock < 1.23) {
        if(boss->guard_sweep&&boss->guard_sweep(boss->guard_context,boss->x_float,boss->y_float,boss->x_float+motion_x,boss->y_float+motion_y,&boss->x_float,&boss->y_float)){sync_pose(boss);return;}
        boss->x_float = clamp_float(boss->x_float, 85, 715); boss->y_float = clamp_float(boss->y_float, 85, 395);
        double travel = 1.0, fraction;
        if (motion_x > 0) { fraction = (715.0 - boss->x_float) / motion_x; if (fraction < travel) travel = fraction; }
        else if (motion_x < 0) { fraction = (85.0 - boss->x_float) / motion_x; if (fraction < travel) travel = fraction; }
        if (motion_y > 0) { fraction = (395.0 - boss->y_float) / motion_y; if (fraction < travel) travel = fraction; }
        else if (motion_y < 0) { fraction = (85.0 - boss->y_float) / motion_y; if (fraction < travel) travel = fraction; }
        boss->x_float += motion_x * (float)travel; boss->y_float += motion_y * (float)travel;
        if (travel < 1.0) {
            boss->vx_float = boss->vy_float = 0; boss->attack_clock = 1.23;
        }
    } else {
        boss->x_float = clamp_float(boss->x_float + motion_x, 85, 715);
        boss->y_float = clamp_float(boss->y_float + motion_y, 85, 395);
    }
    if (boss->shot_clock <= 0) {
        boss->shot_clock = boss->core_exposed ? 1.3 : 1.8;
        if (boss->phase > 0u) boss->shot_clock *= 0.85;
        float angle = us_fx_angle_float(ax, ay);
        for (unsigned i = 0; i < 3u; ++i) {
            us_boss_shot_t *shot = &output->shots[output->count++];
            shot->x = position_q16(boss->x_float + ax * 28); shot->y = position_q16(boss->y_float + ay * 28);
            float cx, sy; us_fx_direction_float((float)(angle + ((int)i - 1) * 0.23), &cx, &sy);
            shot->direction_x = position_q16(cx); shot->direction_y = position_q16(sy);
            shot->damage = 9; shot->speed = 121;
        }
    }
    sync_pose(boss);
}
