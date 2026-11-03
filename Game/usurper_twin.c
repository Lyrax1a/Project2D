#include "usurper_twin.h"
#include <string.h>
static double max_zero(double value) { return value < 0 ? 0 : value; }
static float clamp(float value, float lo, float hi) { return value < lo ? lo : value > hi ? hi : value; }
static float length(float x, float y) { return us_fx_sqrt_float(x * x + y * y); }
static void direction(float dx, float dy, float *x, float *y) {
    float distance = length(dx, dy); *x = distance ? dx / distance : 0; *y = distance ? dy / distance : 0;
}
static void move_toward(float *x, float *y, float tx, float ty, float delta) {
    float dx = tx - *x, dy = ty - *y, distance = length(dx, dy);
    if (distance <= delta || distance < 0.00001f) { *x = tx; *y = ty; }
    else { *x += dx / distance * delta; *y += dy / distance * delta; }
}
void us_twin_refresh(us_twin_t *boss) {
    us_twin_actor_t *s = &boss->actors[0], *b = &boss->actors[1];
    boss->remaining = 0;
    for (unsigned i = 0; i < US_TWIN_PARTS; ++i) if (boss->part_hp[i] > 0) ++boss->remaining;
    boss->cycle = s->shielded ? 0 : boss->remaining ? 1 : 2;
    boss->exposed = !s->shielded && !boss->remaining; boss->phase = s->phase;
    boss->hp = s->hp + b->hp;
    if (s->active && b->active) { boss->x = (s->x + b->x) * 0.5f; boss->y = (s->y + b->y) * 0.5f; }
    else if (s->active) { boss->x = s->x; boss->y = s->y; }
    else if (b->active) { boss->x = b->x; boss->y = b->y; }
}
void us_twin_init(us_twin_t *boss, uint32_t hp_q16, int32_t xq, int32_t yq, int32_t pxq, int32_t pyq) {
    memset(boss, 0, sizeof(*boss));
    float x = xq / 65536.0f, y = yq / 65536.0f, px = pxq / 65536.0f, py = pyq / 65536.0f, dx, dy;
    direction(px - x, py - y, &dx, &dy);
    boss->active = boss->combat_enabled = 1; boss->max_hp = hp_q16 / 65536.0;
    boss->shield_broken_at = boss->armor_broken_at = boss->death_times[0] = boss->death_times[1] = -1;
    for (unsigned role = 0; role < 2; ++role) {
        us_twin_actor_t *a = &boss->actors[role]; float scale = role ? 72.0f : -72.0f;
        /* Vector2.orthogonal() is (y, -x), not (-y, x). */
        a->x = clamp(x + dy * scale, 60, 740); a->y = clamp(y - dx * scale, 60, 420);
        a->rotation = us_fx_angle_float(px - a->x, py - a->y);
        us_fx_direction_float(a->rotation, &a->locked_x, &a->locked_y);
        a->active = 1; a->hp = a->max_hp = boss->max_hp * 0.5;
        a->shielded = role == 0; a->move_clock = 1.1; a->shot_clock = 1;
    }
    for (unsigned i = 0; i < US_TWIN_PARTS; ++i) boss->part_hp[i] = 190;
    us_twin_refresh(boss);
}
unsigned us_twin_target_present(const us_twin_t *boss, unsigned target) {
    if (!boss->active || target > US_TWIN_BOMBER) return 0;
    if (target < US_TWIN_PARTS) return boss->actors[0].active && boss->part_hp[target] > 0;
    return boss->actors[target - US_TWIN_SPLITTER].active;
}
unsigned us_twin_target_open(const us_twin_t *boss, unsigned target) {
    if (!us_twin_target_present(boss, target)) return 0;
    if (target == US_TWIN_BOMBER) return 1;
    if (boss->actors[0].shielded) return 0;
    return target < US_TWIN_PARTS || !boss->remaining;
}
unsigned us_twin_target_position(const us_twin_t *boss, unsigned target, int32_t *x, int32_t *y) {
    if (!us_twin_target_present(boss, target)) return 0;
    const us_twin_actor_t *a = &boss->actors[target == US_TWIN_BOMBER ? 1 : 0]; float px = a->x, py = a->y;
    if (target < US_TWIN_PARTS) {
        float hx, hy, cx, sy; us_fx_direction_float((float)(target * (6.28318530717958647692 / 6)), &hx, &hy);
        hx *= 26; hy *= 26; us_fx_direction_float(a->rotation, &cx, &sy);
        px += cx * hx - sy * hy; py += sy * hx + cx * hy;
    }
    *x = (int32_t)((double)px * US_FX_ONE); *y = (int32_t)((double)py * US_FX_ONE); return 1;
}
unsigned us_twin_blast_contains(float dx, float dy, double radius) {
    double half = radius / 1.4142135623730950488;
    return (dx < 0 ? -dx : dx) <= half && (dy < 0 ? -dy : dy) <= half;
}
unsigned us_twin_break_shield(us_twin_t *boss, float x, float y, double radius, unsigned source) {
    us_twin_actor_t *a = &boss->actors[0];
    if (!boss->active || !a->active || !a->shielded) return 0;
    float dx = a->x - x, dy = a->y - y, half = (float)(radius / 1.4142135623730950488);
    float nx = clamp(dx, -half, half), ny = clamp(dy, -half, half);
    if (length(dx - nx, dy - ny) > 42) return 0;
    a->shielded = 0; a->recovery = 0.9; a->shield_flash = 0.5;
    ++boss->shield_breaks; boss->shield_source = (uint8_t)source;
    if (boss->shield_broken_at < 0) boss->shield_broken_at = boss->elapsed;
    us_twin_refresh(boss); return 1;
}
static void part_damage(us_twin_t *boss, unsigned part, double damage) {
    if (boss->part_hp[part] <= 0) return;
    boss->part_hp[part] = max_zero(boss->part_hp[part] - damage);
    if (boss->part_hp[part] <= 0) {
        ++boss->breaks; --boss->remaining;
        if (!boss->remaining) {
            boss->armor_broken_at = boss->elapsed; boss->actors[0].exposed = 1; boss->actors[0].recovery = 0.7;
        }
        us_twin_refresh(boss);
    }
}
void us_twin_hit(us_twin_t *boss, unsigned target, uint32_t damage_q16, unsigned tags) {
    if (!us_twin_target_present(boss, target)) return;
    double damage = damage_q16 / 65536.0;
    if (target < US_TWIN_PARTS) {
        if (!us_twin_target_open(boss, target)) return;
        part_damage(boss, target, damage * ((tags & US_TWIN_HIT_BOMB) ? 1.4 : 0.65));
        boss->part_flash[target] = 0.16; return;
    }
    unsigned role = target - US_TWIN_SPLITTER; us_twin_actor_t *a = &boss->actors[role];
    if (!us_twin_target_open(boss, target)) {
        if (!a->shielded && (tags & US_TWIN_HIT_GUARD))
            for (unsigned i = 0; i < US_TWIN_PARTS; ++i) part_damage(boss, i, damage / 6);
        if (a->shielded && a->shield_flash <= 0) a->shield_flash = 0.12;
        return;
    }
    double dealt = damage * 1.6;
    if (!role && !a->phase && dealt > a->hp - a->max_hp * 0.5) dealt = a->hp - a->max_hp * 0.5;
    a->hp = max_zero(a->hp - dealt);
    if (a->hp <= 0) {
        a->active = 0; a->vx = a->vy = 0;
        boss->death_times[role] = boss->elapsed; boss->x = a->x; boss->y = a->y;
        if (!role) for (unsigned i = 0; i < US_TWIN_PARTS; ++i) boss->part_hp[i] = 0;
        if (!boss->actors[0].active && !boss->actors[1].active) boss->active = 0;
    }
    if (!role && !a->phase && a->hp / a->max_hp <= 0.5) {
        a->phase = 1; a->shielded = 1; a->exposed = 0; a->shield_flash = 0.5; a->move_state = 0; a->move_clock = 0.8;
    }
    if (a->core_hit_clock <= 0) a->core_hit_clock = 0.08;
    a->flash = 0.1; us_twin_refresh(boss);
}
void us_twin_guard_block(us_twin_t *boss) {
    us_twin_actor_t *a = &boss->actors[0];
    if (!a->active) return;
    a->move_state = 3; a->move_clock = a->recovery = 0.9; a->vx = a->vy = 0;
}
unsigned us_twin_intercept(us_twin_t *boss, unsigned slot, double damage) {
    if (slot >= US_TWIN_BOMBS) return 0;
    us_twin_bomb_t *bomb = &boss->bombs[slot];
    if (!bomb->active || bomb->intercept_hp <= 0) return 0;
    bomb->intercept_hp -= max_zero(damage); if (bomb->intercept_hp <= 0) bomb->active = 0; return 1;
}
static void launch_bomb(us_twin_t *boss, float x, float y, float tx, float ty, us_twin_output_t *out) {
    if (!boss->active || !boss->combat_enabled) return;
    for (unsigned i = 0; i < US_TWIN_BOMBS; ++i) {
        us_twin_bomb_t *bomb = &boss->bombs[i]; if (bomb->active) continue;
        memset(bomb, 0, sizeof(*bomb)); bomb->x = bomb->launch_x = x; bomb->y = bomb->launch_y = y;
        bomb->landing_x = tx; bomb->landing_y = ty; bomb->lifetime = 1.05 + 0.55;
        bomb->intercept_hp = 18; bomb->active = 1; bomb->serial = boss->bomb_count++;
        ++out->launched; return;
    }
}
static void pursue(us_twin_t *boss, float px, float py) {
    const double delta = 1.0 / 60; us_twin_actor_t *a = &boss->actors[0];
    float dx, dy; direction(px - a->x, py - a->y, &dx, &dy); a->move_clock -= delta;
    float pace = a->shielded ? 145.0f : boss->remaining ? 125.0f : 140.0f;
    if (!a->move_state) {
        move_toward(&a->vx, &a->vy, dx * pace, dy * pace, (float)(950 * delta));
        a->rotation = us_fx_turn_float(a->rotation, us_fx_angle_float(dx, dy), 7 * delta);
        if (a->move_clock <= 0 && length(px - a->x, py - a->y) < 240) {
            a->move_state = 1; a->move_clock = 0.32; a->locked_x = dx; a->locked_y = dy;
        }
    } else if (a->move_state == 1) {
        move_toward(&a->vx, &a->vy, 0, 0, (float)(1800 * delta));
        if (a->move_clock <= 0) { a->move_state = 2; a->move_clock = 0.34; }
    } else if (a->move_state == 2) {
        a->vx = a->locked_x * 340; a->vy = a->locked_y * 340;
        if (a->move_clock <= 0) { a->move_state = 3; a->move_clock = 0.7; a->vx = a->vy = 0; }
    } else {
        move_toward(&a->vx, &a->vy, 0, 0, (float)(1700 * delta));
        if (a->move_clock <= 0) { a->move_state = 0; a->move_clock = boss->actors[1].active ? 1.65 : 1.1; }
    }
}
static void bombard(us_twin_t *boss, float px, float py, float pvx, float pvy, float max_speed, us_twin_output_t *out) {
    const double delta = 1.0 / 60; us_twin_actor_t *a = &boss->actors[1];
    float dx, dy; direction(px - a->x, py - a->y, &dx, &dy); float distance = length(px - a->x, py - a->y);
    a->rotation = us_fx_turn_float(a->rotation, us_fx_angle_float(dx, dy), 5 * delta);
    float scale = (float)((double)(distance - 175) * 2), vx, vy;
    scale = clamp(scale, -180, 125); vx = dx * scale; vy = dy * scale;
    if (distance >= 145 && distance <= 210) {
        float wave = (float)us_fx_sin_double(a->elapsed * 1.7);
        vx = dy * wave * 75; vy = -dx * wave * 75;
    }
    if (a->cast_left > 0) {
        a->cast_left = max_zero(a->cast_left - delta); vx *= 0.12f; vy *= 0.12f;
        if (!a->cast_left) { launch_bomb(boss, a->x + dx * 28, a->y + dy * 28, a->cast_x, a->cast_y, out); a->shot_flash = 0.12; }
    }
    a->shot_clock -= delta;
    if (a->shot_clock <= 0 && a->cast_left <= 0) {
        float distance_v = length(pvx, pvy);
        if (distance_v > 0 && distance_v > max_speed) { pvx = pvx / distance_v * max_speed; pvy = pvy / distance_v * max_speed; }
        a->cast_x = clamp(px + pvx * 1.9f, 36, 764); a->cast_y = clamp(py + pvy * 1.9f, 36, 444);
        a->cast_left = 0.3; a->shot_clock = boss->actors[0].active && a->hp / a->max_hp >= 0.35 ? 2.35 : 1.8;
    }
    move_toward(&a->vx, &a->vy, vx, vy, (float)(1400 * delta));
}
static void move_actor(us_twin_t *boss, unsigned role) {
    us_twin_actor_t *a = &boss->actors[role];
    float sx = clamp(a->x, 48, 752), sy = clamp(a->y, 48, 432);
    float mx = a->vx * (float)(1.0 / 60), my = a->vy * (float)(1.0 / 60);
    if (!role && a->move_state == 2) {
        double travel = 1;
        if (mx > 0 && (752 - sx) / mx < travel) travel = (752 - sx) / mx;
        else if (mx < 0 && (48 - sx) / mx < travel) travel = (48 - sx) / mx;
        if (my > 0 && (432 - sy) / my < travel) travel = (432 - sy) / my;
        else if (my < 0 && (48 - sy) / my < travel) travel = (48 - sy) / my;
        a->x = sx + mx * (float)travel; a->y = sy + my * (float)travel;
        if (travel < 1) us_twin_guard_block(boss);
    } else { a->x = clamp(a->x + mx, 48, 752); a->y = clamp(a->y + my, 48, 432); }
}
void us_twin_step(us_twin_t *boss, int32_t pxq, int32_t pyq, int32_t pvxq, int32_t pvyq,
                  unsigned max_speed, us_twin_output_t *out) {
    memset(out, 0, sizeof(*out));
    const double delta = 1.0 / 60; float px = pxq / 65536.0f, py = pyq / 65536.0f;
    if (boss->active && boss->combat_enabled) {
        boss->elapsed += delta; us_twin_refresh(boss);
        for (unsigned role = 0; role < 2; ++role) {
            us_twin_actor_t *a = &boss->actors[role]; if (!a->active) continue;
            a->flash = max_zero(a->flash - delta); a->elapsed += delta;
            a->shot_flash = max_zero(a->shot_flash - delta); a->shield_flash = max_zero(a->shield_flash - delta);
            a->core_hit_clock = max_zero(a->core_hit_clock - delta); a->recovery = max_zero(a->recovery - delta);
            if (a->recovery > 0) move_toward(&a->vx, &a->vy, 0, 0, (float)(1800 * delta));
            else if (!role) pursue(boss, px, py);
            else bombard(boss, px, py, pvxq / 65536.0f, pvyq / 65536.0f, (float)max_speed, out);
            move_actor(boss, role);
        }
        for (unsigned i = 0; i < US_TWIN_PARTS; ++i)
            if (boss->part_hp[i] > 0) boss->part_flash[i] = max_zero(boss->part_flash[i] - delta);
    }
    for (unsigned i = 0; i < US_TWIN_BOMBS; ++i) {
        us_twin_bomb_t *bomb = &boss->bombs[i]; if (!bomb->active) continue;
        if (!boss->active) { bomb->active = 0; continue; }
        bomb->age += delta; bomb->lifetime -= delta;
        double progress = bomb->age / 1.05; if (progress > 1) progress = 1;
        float weight = (float)(1 - (1 - progress) * (1 - progress));
        bomb->x = bomb->launch_x + (bomb->landing_x - bomb->launch_x) * weight;
        bomb->y = bomb->launch_y + (bomb->landing_y - bomb->launch_y) * weight;
        bomb->speed = 2 * (double)length(bomb->landing_x - bomb->launch_x, bomb->landing_y - bomb->launch_y) / 1.05 * (1 - progress);
        if (bomb->lifetime <= 0) {
            us_twin_break_shield(boss, bomb->x, bomb->y, 88, 1);
            if (us_twin_blast_contains(px - bomb->x, py - bomb->y, 88)) ++out->damage_count;
            out->explosions[out->exploded].x = bomb->x; out->explosions[out->exploded++].y = bomb->y;
            bomb->active = 0;
        }
    }
}
