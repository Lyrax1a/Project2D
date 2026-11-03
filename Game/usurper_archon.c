#include "usurper_archon.h"
#include <string.h>

static float clamp_float(float value, float low, float high) { return value < low ? low : value > high ? high : value; }
static double max_zero(double value) { return value < 0 ? 0 : value; }
static int32_t pose(float value) { return (int32_t)((double)value * US_FX_ONE); }
static int32_t direction_q16(float value) {
    double scaled = (double)value * US_FX_ONE;
    return (int32_t)(scaled + (value < 0 ? -0.5 : 0.5));
}
static void sync(us_archon_t *boss) {
    boss->x = pose(boss->x_float); boss->y = pose(boss->y_float);
    boss->vx = pose(boss->vx_float); boss->vy = pose(boss->vy_float);
    int32_t angle = (int32_t)((double)boss->rotation * 180.0 / 3.14159265358979323846 * US_FX_ONE);
    angle %= US_FX_TURN; boss->angle = angle < 0 ? angle + US_FX_TURN : angle;
    boss->hp_q16 = (uint32_t)(boss->hp * US_FX_ONE);
    boss->shield_q16 = (uint32_t)(boss->shield * US_FX_ONE);
}
static void expose(us_archon_t *boss) {
    boss->core_exposed = 1; boss->exposed_at = boss->elapsed; boss->recovery = 1.2;
}
static void recharge(us_archon_t *boss) {
    boss->shield_max = 400 + boss->phase * 180; boss->shield = boss->shield_max;
    boss->core_exposed = 0; boss->opening_left = 0;
}
static void fan(us_archon_t *boss, us_archon_output_t *output, float x, float y,
                float dx, float dy, unsigned count, double spread, unsigned speed) {
    /* Vector2.rotated uses real_t (float32), unlike GDScript timer arithmetic. */
    for (unsigned i = 0; i < count && output->count < US_ARCHON_SHOTS; ++i) {
        float angle = (float)(((double)i - (count - 1u) * 0.5) * spread), cx, sy;
        us_fx_direction_float(angle, &cx, &sy);
        us_boss_shot_t *shot = &output->shots[output->count++];
        shot->x = pose(x); shot->y = pose(y);
        shot->direction_x = direction_q16(cx * dx - sy * dy);
        shot->direction_y = direction_q16(sy * dx + cx * dy);
        shot->damage = boss->phase ? 16u : 13u; shot->speed = (uint16_t)speed;
    }
}
void us_archon_init(us_archon_t *boss, uint32_t hp_q16, int32_t x, int32_t y, int32_t px, int32_t py) {
    memset(boss, 0, sizeof(*boss));
    boss->x_float = x / 65536.0f; boss->y_float = y / 65536.0f;
    boss->rotation = us_fx_angle_float((px - x) / 65536.0f, (py - y) / 65536.0f);
    boss->hp = boss->max_hp = hp_q16 / 65536.0;
    boss->active = 1; boss->shot_clock = 0.6; boss->exposed_at = -1;
    recharge(boss); sync(boss);
}
void us_archon_flush_deferred(us_archon_t *boss, int32_t px, int32_t py) {
    if (!boss->deploy_pending) return;
    boss->deploy_pending = 0;
    if (!boss->active || boss->deployed) return;
    for (unsigned i = 0; i < US_ARCHON_PARTS; ++i) {
        us_archon_turret_t *part = &boss->parts[i];
        float cx, sy;
        us_fx_direction_float((float)(boss->rotation + i * (6.28318530717958647692 / 6)), &cx, &sy);
        part->home_x = clamp_float(boss->x_float + cx * 115, 28, 772);
        part->home_y = clamp_float(boss->y_float + sy * 115, 28, 452);
        part->x = part->launch_x = boss->x_float + cx * 40;
        part->y = part->launch_y = boss->y_float + sy * 40;
        part->rotation = us_fx_angle_float(cx, sy);
        part->barrel = us_fx_angle_float(px / 65536.0f - part->x, py / 65536.0f - part->y);
        part->firing_x = 1; part->hp = 640; part->active = 1;
        part->shot_clock = 0.5 + i * 0.5;
    }
    boss->deployed = 1; boss->remaining = US_ARCHON_PARTS;
}
unsigned us_archon_target_open(const us_archon_t *boss, unsigned target) {
    return boss->active && (target == US_ARCHON_CORE || (target < US_ARCHON_PARTS && boss->parts[target].active));
}
unsigned us_archon_target_position(const us_archon_t *boss, unsigned target, int32_t *x, int32_t *y) {
    if (!us_archon_target_open(boss, target)) return 0;
    *x = target == US_ARCHON_CORE ? boss->x : pose(boss->parts[target].x);
    *y = target == US_ARCHON_CORE ? boss->y : pose(boss->parts[target].y);
    return 1;
}
unsigned us_archon_hit(us_archon_t *boss, unsigned target, uint32_t damage_q16) {
    if (!damage_q16 || !us_archon_target_open(boss, target)) return 0;
    double damage = damage_q16 / 65536.0;
    if (target != US_ARCHON_CORE) {
        us_archon_turret_t *part = &boss->parts[target];
        part->hp = max_zero(part->hp - damage); part->flash = 10;
        if (part->hp > 0) return 0;
        part->active = 0; --boss->remaining; ++boss->breaks;
        if (!boss->remaining) expose(boss);
        return 1000u;
    }
    if (boss->shield > 0) {
        boss->shield = max_zero(boss->shield - damage); boss->flash = 6;
        if (boss->core_hit_clock <= 0) boss->core_hit_clock = 0.08;
        if (boss->shield <= 0) { boss->opening_left = 6.5; expose(boss); }
        sync(boss); return 0;
    }
    double floor = boss->phase == 0 ? boss->max_hp * (2.0 / 3.0) : 0;
    double before = boss->hp, applied = damage * (boss->phase ? 1.0 : 1.6);
    if (applied > before - floor) applied = before - floor;
    boss->hp = max_zero(before - applied);
    if (boss->core_hit_clock <= 0 && before > boss->hp) boss->core_hit_clock = 0.08;
    boss->flash = 8;
    if (boss->hp <= 0) boss->active = 0;
    if (boss->active && boss->phase == 0 && boss->hp <= floor + 0.001) {
        boss->phase = 1; boss->phase_started = boss->elapsed; boss->attack_clock = 0;
        boss->recovery = 1; boss->shield = boss->shield_max = 0; boss->core_exposed = 1;
        boss->vx_float = boss->vy_float = 0; boss->deploy_pending = 1;
    }
    sync(boss); return 0;
}
static void turret_step(us_archon_t *boss, us_archon_turret_t *part, int32_t px, int32_t py, us_archon_output_t *output) {
    if (!part->active || !boss->active) return;
    const double delta = 1.0 / 60.0;
    if (part->flash) --part->flash;
    part->deployment += delta; if (part->deployment > 1) part->deployment = 1;
    double age = 1 - part->deployment;
    float weight = (float)(1 - age * age * age);
    part->x = part->launch_x + (part->home_x - part->launch_x) * weight;
    part->y = part->launch_y + (part->home_y - part->launch_y) * weight;
    if (part->deployment < 1 || boss->recovery > 0) return;
    part->shot_clock -= delta;
    if (!part->rounds) {
        float aim = us_fx_angle_float(px / 65536.0f - part->x, py / 65536.0f - part->y);
        part->barrel = us_fx_turn_double(part->barrel, aim, delta * 2.2);
    }
    if (part->shot_clock > 0) return;
    if (!part->rounds) {
        us_fx_direction_float((float)part->barrel, &part->firing_x, &part->firing_y); part->rounds = 3;
    }
    fan(boss, output, part->x + part->firing_x * 20, part->y + part->firing_y * 20,
        part->firing_x, part->firing_y, 2, 0.1, 215);
    --part->rounds; part->shot_clock = part->rounds ? 0.18 : 3.24;
}
void us_archon_step(us_archon_t *boss, int32_t px, int32_t py, us_archon_output_t *output) {
    output->count = 0;
    if (!boss->active) return;
    const double delta = 1.0 / 60.0;
    if (boss->flash) --boss->flash;
    boss->elapsed += delta;
    boss->core_hit_clock = max_zero(boss->core_hit_clock - delta);
    if (boss->recovery > 0) boss->recovery = max_zero(boss->recovery - delta);
    else {
        boss->attack_clock += delta; boss->shot_clock -= delta;
        float dx = px / 65536.0f - boss->x_float, dy = py / 65536.0f - boss->y_float;
        float distance = us_fx_sqrt_float(dx * dx + dy * dy);
        float ax = distance ? dx / distance : 0, ay = distance ? dy / distance : 0;
        float pace = clamp_float((distance - 175) * 0.5f, -28, 45);
        boss->vx_float = boss->phase ? 0 : ax * pace; boss->vy_float = boss->phase ? 0 : ay * pace;
        if (!boss->phase) boss->rotation = us_fx_turn_float(boss->rotation, us_fx_angle_float(ax, ay), delta * 0.6);
        if (!boss->phase && boss->shield <= 0) {
            boss->opening_left -= delta;
            if (boss->opening_left <= 0) recharge(boss);
        }
        if (boss->shot_clock <= 0) {
            boss->shot_clock = boss->phase ? 1.1 : 0.52;
            fan(boss, output, boss->x_float + ax * 34, boss->y_float + ay * 34,
                ax, ay, boss->phase ? 3u : 5u, 0.14, boss->phase ? 195u : 145u);
        }
        boss->x_float = clamp_float(boss->x_float + boss->vx_float * (float)delta, 145, 655);
        boss->y_float = clamp_float(boss->y_float + boss->vy_float * (float)delta, 145, 335);
    }
    sync(boss);
    for (unsigned i = 0; i < US_ARCHON_PARTS; ++i) turret_step(boss, &boss->parts[i], px, py, output);
}
