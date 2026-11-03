#include "usurper_flame.h"
#include <string.h>

static double max_zero(double value) { return value < 0 ? 0 : value; }
static float clamp_float(float value, float low, float high) { return value < low ? low : value > high ? high : value; }
static void direction(float dx, float dy, float *x, float *y) {
    float distance = us_fx_sqrt_float(dx * dx + dy * dy);
    *x = distance ? dx / distance : 0; *y = distance ? dy / distance : 0;
}
static void rotate(float x, float y, float angle, float *rx, float *ry) {
    float cx, sy; us_fx_direction_float(angle, &cx, &sy);
    *rx = cx * x - sy * y; *ry = sy * x + cx * y;
}
static void sync(us_flame_t *boss) {
    boss->x = (int32_t)((double)boss->x_float * US_FX_ONE);
    boss->y = (int32_t)((double)boss->y_float * US_FX_ONE);
    int32_t angle = (int32_t)((double)boss->rotation * 180.0 / 3.14159265358979323846 * US_FX_ONE);
    angle %= US_FX_TURN; boss->angle = angle < 0 ? angle + US_FX_TURN : angle;
    boss->hp_q16 = (uint32_t)(boss->hp * US_FX_ONE);
}
void us_flame_init(us_flame_t *boss, uint32_t hp_q16, int32_t x, int32_t y, int32_t px, int32_t py) {
    memset(boss, 0, sizeof(*boss));
    boss->x_float = x / 65536.0f; boss->y_float = y / 65536.0f;
    boss->rotation = us_fx_angle_float((px - x) / 65536.0f, (py - y) / 65536.0f);
    us_fx_direction_float(boss->rotation, &boss->locked_x, &boss->locked_y);
    boss->hp = boss->max_hp = hp_q16 / 65536.0; boss->active = 1; boss->shot_clock = 0.6;
    sync(boss);
}
unsigned us_flame_active(const us_flame_t *boss) { return boss->active && boss->recovery <= 0 && boss->state == 2; }
unsigned us_flame_contains(const us_flame_t *boss, float px, float py) {
    float dx = px - boss->x_float, dy = py - boss->y_float;
    float dot = dx * boss->locked_x + dy * boss->locked_y;
    double along = (double)dot - 10, reach = 235 + boss->phase * 25;
    if (along < 0 || along > reach) return 0;
    float across = dx * boss->locked_y - dy * boss->locked_x;
    double width = 7 + us_fx_sin_double(along / reach * 3.14159265358979323846) * 44;
    return (across < 0 ? -across : across) <= width;
}
void us_flame_hit(us_flame_t *boss, uint32_t damage_q16, float source_x, float source_y) {
    if (!boss->active || !damage_q16) return;
    float dx, dy; direction(source_x - boss->x_float, source_y - boss->y_float, &dx, &dy);
    unsigned flank = dx * boss->locked_x + dy * boss->locked_y < 0.3;
    double multiplier = boss->core_exposed ? 2.2 : flank ? 1.1 : 0.8;
    double floor = boss->phase ? 0 : boss->max_hp * 0.5;
    double before = boss->hp, damage = damage_q16 / 65536.0 * multiplier;
    if (damage > before - floor) damage = before - floor;
    boss->hp = max_zero(before - damage);
    if (boss->core_hit_clock <= 0 && before > boss->hp) boss->core_hit_clock = 0.08;
    boss->flash = 0.12;
    if (boss->hp <= 0) boss->active = 0;
    if (boss->active && !boss->phase && boss->hp <= floor + 0.001) {
        boss->phase = 1; boss->phase_started = boss->elapsed; boss->attack_clock = 0;
        boss->recovery = 1; boss->core_exposed = 0;
        boss->destination_x = boss->destination_y = 0; boss->pattern_clock = 0;
    }
    sync(boss);
}
static void mark_jump(us_flame_t *boss, float px, float py) {
    float ax, ay, rx, ry; direction(boss->x_float - px, boss->y_float - py, &ax, &ay);
    if (ax > -0.00001f && ax < 0.00001f && ay > -0.00001f && ay < 0.00001f) ax = 1;
    rotate(ax, ay, (boss->attack_index & 1u) ? -1.1f : 1.1f, &rx, &ry);
    boss->destination_x = clamp_float(px + rx * 125, 45, 755);
    boss->destination_y = clamp_float(py + ry * 125, 45, 435);
    float dx = boss->destination_x - px, dy = boss->destination_y - py;
    if (us_fx_sqrt_float(dx * dx + dy * dy) < 90) {
        boss->destination_x = clamp_float(px - ax * 130, 45, 755);
        boss->destination_y = clamp_float(py - ay * 130, 45, 435);
    }
    boss->pattern_clock = boss->phase ? 0.24 : 0.46;
    boss->state = 0; boss->core_exposed = 0; ++boss->attack_index;
}
void us_flame_step(us_flame_t *boss, int32_t px_q16, int32_t py_q16, us_flame_output_t *output) {
    output->flame_damage = output->teleported = 0;
    if (!boss->active) return;
    const double delta = 1.0 / 60.0;
    boss->flash = max_zero(boss->flash - delta); boss->elapsed += delta;
    boss->core_hit_clock = max_zero(boss->core_hit_clock - delta);
    if (boss->recovery > 0) { boss->recovery = max_zero(boss->recovery - delta); return; }
    float px = px_q16 / 65536.0f, py = py_q16 / 65536.0f, aim_x, aim_y;
    direction(px - boss->x_float, py - boss->y_float, &aim_x, &aim_y);
    boss->attack_clock += delta; boss->shot_clock -= delta;
    boss->rotation = us_fx_turn_float(boss->rotation, us_fx_angle_float(aim_x, aim_y), delta * 0.6);
    if (boss->pattern_clock <= 0 && !boss->destination_x && !boss->destination_y) {
        boss->jumps = boss->phase ? (boss->hp / boss->max_hp < 0.25 ? 5u : 3u) : 1u;
        mark_jump(boss, px, py);
    }
    boss->pattern_clock -= delta;
    if (boss->state == 0 && boss->pattern_clock <= 0) {
        boss->x_float = boss->destination_x; boss->y_float = boss->destination_y; output->teleported = 1;
        --boss->jumps;
        if (boss->jumps) mark_jump(boss, px, py);
        else {
            boss->state = 1; boss->pattern_clock = boss->phase ? 0.28 : 0.42;
            direction(px - boss->x_float, py - boss->y_float, &boss->locked_x, &boss->locked_y);
        }
    } else if (boss->state == 1 && boss->pattern_clock <= 0) {
        boss->state = 2; boss->pattern_clock = boss->phase ? 1.6 : 1.4;
    } else if (boss->state == 2) {
        direction(px - boss->x_float, py - boss->y_float, &aim_x, &aim_y);
        float cross = boss->locked_x * aim_y - boss->locked_y * aim_x;
        float dot = boss->locked_x * aim_x + boss->locked_y * aim_y;
        double turn = us_fx_angle_float(dot, cross), limit = (boss->phase ? 0.9 : 0.65) * delta;
        if (turn > limit) turn = limit; else if (turn < -limit) turn = -limit;
        float rx, ry; rotate(boss->locked_x, boss->locked_y, (float)turn, &rx, &ry);
        boss->locked_x = rx; boss->locked_y = ry;
        boss->pattern_shot_clock -= delta;
        if (boss->pattern_shot_clock <= 0) {
            boss->pattern_shot_clock = 0.22;
            if (us_flame_contains(boss, px, py)) output->flame_damage = 10;
        }
        if (boss->pattern_clock <= 0) {
            boss->state = 3; boss->pattern_clock = boss->phase ? 2.8 : 3.4; boss->core_exposed = 1;
        }
    } else if (boss->state == 3 && boss->pattern_clock <= 0) boss->destination_x = boss->destination_y = 0;
    boss->rotation = us_fx_turn_float(boss->rotation, us_fx_angle_float(boss->locked_x, boss->locked_y), delta * 4);
    sync(boss);
}
