#include "usurper_boundary.h"
#include <string.h>
static double nonnegative(double v) { return v < 0 ? 0 : v; }
static float clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float length(float x, float y) { return us_fx_sqrt_float(x * x + y * y); }
static void direction(float x, float y, float *dx, float *dy) {
    float distance = length(x, y); *dx = distance ? x / distance : 0; *dy = distance ? y / distance : 0;
}
static void toward(float *x, float *y, float tx, float ty, float step) {
    float dx = tx - *x, dy = ty - *y, distance = length(dx, dy);
    if (distance <= step || distance < 0.00001f) { *x = tx; *y = ty; }
    else { *x += dx / distance * step; *y += dy / distance * step; }
}
static double root_double(double value) {
    if(value<=0) return 0;
    uint64_t bits;memcpy(&bits,&value,sizeof(bits));bits=(bits>>1)+UINT64_C(0x1ff8000000000000);
    double root;memcpy(&root,&bits,sizeof(root));
    for(unsigned i=0;i<7u;++i) root=(root+value/root)*0.5;
    return root;
}
void us_boundary_safe_rect(const us_boundary_t *boss, float rect[4]) {
    rect[0] = 24 + (float)boss->insets[0]; rect[1] = 24 + (float)boss->insets[1];
    rect[2] = rect[0] + (752 - (float)(boss->insets[0] + boss->insets[2]));
    rect[3] = rect[1] + (432 - (float)(boss->insets[1] + boss->insets[3]));
}
static void constrain(const us_boundary_t *boss, float *x, float *y, float margin) {
    float rect[4]; us_boundary_safe_rect(boss, rect);
    *x = clamp(*x, rect[0] + margin, rect[2] - margin); *y = clamp(*y, rect[1] + margin, rect[3] - margin);
}
static void rail(const us_boundary_t *boss, unsigned side, float x, float y, float *rx, float *ry) {
    float r[4]; us_boundary_safe_rect(boss, r);
    if (side & 1u) { *rx = clamp(x, r[0] + 54, r[2] - 54); *ry = side == 1 ? r[1] : r[3]; }
    else { *rx = side == 0 ? r[0] : r[2]; *ry = clamp(y, r[1] + 54, r[3] - 54); }
}
static void module_position(const us_boundary_t *boss, unsigned side, unsigned role, float *x, float *y) {
    float r[4]; us_boundary_safe_rect(boss, r); float fraction = role == 1 ? 0.32f : 0.68f;
    float width = 752 - (float)(boss->insets[0] + boss->insets[2]);
    float height = 432 - (float)(boss->insets[1] + boss->insets[3]);
    float tx = r[0] + width * fraction, ty = r[1] + height * fraction;
    rail(boss, side, tx, ty, x, y);
    if (!side) *x += 12; else if (side == 1) *y += 12; else if (side == 2) *x -= 12; else *y -= 12;
}
static void confine_player(us_boundary_t *boss) {
    float x = boss->px, y = boss->py; constrain(boss, &x, &y, 16);
    if (x != boss->px || y != boss->py) {
        float dx, dy; direction(x - boss->px, y - boss->py, &dx, &dy);
        float dot = boss->vx * dx + boss->vy * dy;
        boss->px = x; boss->py = y;
        if (dot < 0) { boss->vx -= dx * dot; boss->vy -= dy * dot; }
    }
}
void us_boundary_init(us_boundary_t *boss, uint32_t hp_q16, int32_t px, int32_t py, unsigned units) {
    memset(boss, 0, sizeof(*boss)); boss->px = px / 65536.0f; boss->py = py / 65536.0f;
    boss->active = 1; boss->hp = boss->max_hp = hp_q16 / 65536.0; boss->remaining = 4;
    boss->beat_clock = 1; boss->shot_clock = 0.6;
    for (unsigned side = 0; side < 4; ++side) for (unsigned role = 0; role < 3; ++role) {
        us_boundary_part_t *part = &boss->parts[side * 3 + role];
        part->side = (uint8_t)side; part->role = (uint8_t)role; part->active = 1; part->tracked_unit = part->captive = -1;
        part->hp = role == 0 ? 300 : role == 1 ? 420 : 240;
        if (!role) {
            rail(boss, side, boss->px, boss->py, &part->mount_x, &part->mount_y);
            part->x = part->mount_x; part->y = part->mount_y; part->destination_x = boss->px; part->destination_y = boss->py; part->clock = 1;
        } else {
            module_position(boss, side, role, &part->x, &part->y);
            direction(boss->px - part->x, boss->py - part->y, &part->heading_x, &part->heading_y); part->clock = 1.4 + side * 0.8;
        }
    }
    confine_player(boss);
    for (unsigned i = 0; i < units && i < US_BOUNDARY_UNITS; ++i) {
        boss->units[i].active = boss->units[i].valid = 1; boss->units[i].x = boss->px; boss->units[i].y = boss->py;
        boss->units[i].order = (uint8_t)i;
    }
}
static unsigned unit_count(const us_boundary_t *boss) {
    unsigned count = 0; for (unsigned i = 0; i < US_BOUNDARY_UNITS; ++i) if (boss->units[i].active) ++count; return count;
}
static void begin_attack(us_boundary_t *boss, unsigned side, unsigned quick) {
    us_boundary_part_t *arm = &boss->parts[side * 3]; if (!arm->active || arm->state != US_BOUNDARY_WAIT) return;
    arm->fast = (uint8_t)quick; arm->state = US_BOUNDARY_WARN; arm->clock = quick ? 0.28 : 0.48; arm->tracked_unit = -1;
    float x = boss->px, y = boss->py; unsigned count = unit_count(boss);
    if (count) {
        unsigned index = side % count;
        for (unsigned order = 0; order < US_BOUNDARY_UNITS && arm->tracked_unit < 0; ++order)
        for (unsigned i = 0; i < US_BOUNDARY_UNITS; ++i) if (boss->units[i].active && boss->units[i].order == order) {
            if (!index) { arm->tracked_unit = (int)i; x = boss->units[i].x; y = boss->units[i].y; break; }
            --index;
        }
    }
    constrain(boss, &x, &y, 18); arm->destination_x = x; arm->destination_y = y;
}
static void track(us_boundary_t *boss, us_boundary_part_t *arm, float distance) {
    float x = boss->px, y = boss->py;
    if (arm->tracked_unit >= 0 && boss->units[arm->tracked_unit].valid) { x = boss->units[arm->tracked_unit].x; y = boss->units[arm->tracked_unit].y; }
    constrain(boss, &x, &y, 18); toward(&arm->destination_x, &arm->destination_y, x, y, distance);
}
void us_boundary_finish_crush(us_boundary_t *boss) {
    for (unsigned side = 0; side < 4u; ++side) {
        if (!(boss->crush_pending & (1u << side))) continue;
        us_boundary_part_t *arm = &boss->parts[side * 3u];
        if (arm->captive < 0) continue;
        boss->units[arm->captive].pending_free = 1;
        arm->captive = -1; arm->flash = 0.18; arm->state = US_BOUNDARY_RETURN;
    }
    boss->crush_pending = 0; boss->crush_holding = 0; boss->crush_left = 0;
}
void us_boundary_flush_deferred(us_boundary_t *boss) {
    for (unsigned i = 0; i < US_BOUNDARY_UNITS; ++i) if (boss->units[i].pending_free) {
        boss->units[i].pending_free = 0; boss->units[i].valid = 0;
    }
}
void us_boundary_cinematic_tick(us_boundary_t *boss, double real_delta) {
    if (!boss->crush_holding) return;
    boss->crush_left -= real_delta;
    double progress = (0.42 - boss->crush_left - 0.1) / 0.28;
    if (progress < 0) progress = 0; else if (progress > 1) progress = 1;
    for (unsigned side = 0; side < 4u; ++side)
        if (boss->crush_pending & (1u << side)) boss->parts[side * 3u].crush_progress = progress * progress;
    if (boss->crush_left <= 0) us_boundary_finish_crush(boss);
}
static void grab(us_boundary_t *boss, us_boundary_part_t *arm, float sx, float sy, us_boundary_output_t *output) {
    float dx = arm->x - sx, dy = arm->y - sy, norm = dx * dx + dy * dy;
    for (unsigned order = 0; order < US_BOUNDARY_UNITS; ++order)
    for (unsigned i = 0; i < US_BOUNDARY_UNITS; ++i) {
        us_boundary_unit_t *unit = &boss->units[i]; if (!unit->active || unit->order != order) continue;
        float projection = norm ? ((unit->x - sx) * dx + (unit->y - sy) * dy) / norm : 0;
        projection = clamp(projection, 0, 1); float x = sx + dx * projection, y = sy + dy * projection;
        if (length(unit->x - x, unit->y - y) > 23) continue;
        unit->active = 0; unit->x = arm->x = x; unit->y = arm->y = y;
        arm->state = US_BOUNDARY_HOLD; arm->clock = 0.12; arm->captive = (int)i; arm->crush_progress = 0;
        output->grabbed_units |= 1u << i; boss->crush_pending |= 1u << arm->side;
        if (!boss->presentation_enabled) { us_boundary_finish_crush(boss); ++output->crushed; }
        else if (!boss->crush_holding) { boss->crush_holding = 1; boss->crush_left = 0.42; }
        return;
    }
}
void us_boundary_guard_block(us_boundary_t *boss, unsigned side) {
    if (side < 4u) { boss->parts[side * 3].state = US_BOUNDARY_RETURN; boss->parts[side * 3].clock = 0; }
}
static unsigned guard_arm(us_boundary_t *boss,us_boundary_part_t *arm,float sx,float sy,float tx,float ty) {
    float mx=tx-sx,my=ty-sy,norm=mx*mx+my*my;
    for(unsigned i=0;i<boss->guard_count&&i<US_BOUNDARY_GUARDS;++i) {
        us_boundary_guard_t *field=&boss->guards[i];if(!field->is_warden||field->age>=field->duration) continue;
        double radius=field->radius+16;
        float along=norm?((field->x-sx)*mx+(field->y-sy)*my)/norm:0;along=clamp(along,0,1);
        float nx=sx+mx*along,ny=sy+my*along,ox=nx-field->x,oy=ny-field->y;
        if((double)(ox*ox+oy*oy)>radius*radius) continue;
        float dx,dy;direction(mx,my,&dx,&dy);if(dx==0&&dy==0) continue;
        float px=sx-field->x,py=sy-field->y;double projection=px*dx+py*dy;
        if(projection>=0) continue;
        double radicand=projection*projection-(double)(px*px+py*py)+radius*radius;
        double distance=-projection-root_double(nonnegative(radicand));if(distance<0) distance=0;
        float qx=sx+dx*(float)distance-field->x,qy=sy+dy*(float)distance-field->y;
        direction(qx,qy,&nx,&ny);arm->x=field->x+nx*(float)radius;arm->y=field->y+ny*(float)radius;
        us_boundary_guard_block(boss,arm->side);
        us_boundary_hit(boss,arm->side*3u,(field->strengthened?195u:65u)*US_FX_ONE,0);
        float atx=field->x+nx*(float)field->radius,aty=field->y+ny*(float)field->radius;
        direction(atx-field->x,aty-field->y,&field->block_x,&field->block_y);field->block_flash=0.24;
        return 1;
    }
    return 0;
}
static void arm_step(us_boundary_t *boss, us_boundary_part_t *arm, us_boundary_output_t *output) {
    const double delta = boss->delta; arm->flash = nonnegative(arm->flash - delta); arm->clock -= delta;
    float x, y; rail(boss, arm->side, arm->state == US_BOUNDARY_WAIT ? boss->px : arm->mount_x,
        arm->state == US_BOUNDARY_WAIT ? boss->py : arm->mount_y, &x, &y);
    toward(&arm->mount_x, &arm->mount_y, x, y, (float)(110 * delta));
    if (arm->state == US_BOUNDARY_WAIT) { arm->x = arm->mount_x; arm->y = arm->mount_y; }
    else if (arm->state == US_BOUNDARY_WARN) {
        arm->x = arm->mount_x; arm->y = arm->mount_y; track(boss, arm, (float)(260 * delta));
        if (arm->clock <= 0) { arm->state = US_BOUNDARY_REACH; arm->reach_age = 0; }
    } else if (arm->state == US_BOUNDARY_REACH) {
        arm->reach_age += delta;
        if (!arm->fast && arm->reach_age < 0.45 && length(arm->x - arm->destination_x, arm->y - arm->destination_y) > 60) track(boss, arm, (float)(120 * delta));
        float sx = arm->x, sy = arm->y;
        toward(&arm->x, &arm->y, arm->destination_x, arm->destination_y, (float)((arm->fast ? 850 : 440) * delta));
        if(guard_arm(boss,arm,sx,sy,arm->x,arm->y)) return;
        grab(boss, arm, sx, sy, output);
        if (length(arm->x - boss->px, arm->y - boss->py) < 22) ++output->charge_hits;
        if (length(arm->x - arm->destination_x, arm->y - arm->destination_y) < 1 || arm->captive >= 0 || arm->reach_age >= 1.65) { arm->state = US_BOUNDARY_HOLD; arm->clock = 0.12; }
    } else if (arm->state == US_BOUNDARY_HOLD) { if (arm->clock <= 0) arm->state = US_BOUNDARY_RETURN; }
    else {
        toward(&arm->x, &arm->y, arm->mount_x, arm->mount_y, (float)((arm->fast ? 780 : 620) * delta));
        if (length(arm->x - arm->mount_x, arm->y - arm->mount_y) < 1) arm->state = US_BOUNDARY_WAIT;
    }
    if (arm->captive >= 0) { boss->units[arm->captive].x = arm->x; boss->units[arm->captive].y = arm->y; }
    float tx = arm->x == arm->mount_x && arm->y == arm->mount_y ? arm->destination_x : arm->x;
    float ty = arm->x == arm->mount_x && arm->y == arm->mount_y ? arm->destination_y : arm->y;
    float dx, dy; direction(tx - arm->mount_x, ty - arm->mount_y, &dx, &dy); arm->rotation = us_fx_angle_float(dx, dy);
}
static unsigned intersect(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, float *x, float *y) {
    /* Preserve float32 operation order from Godot 4.6 Geometry2D:
     * https://github.com/godotengine/godot/blob/4.6/core/math/geometry_2d.h */
    float bxr = bx - ax, byr = by - ay, length_b = bxr * bxr + byr * byr;
    if (!length_b) return 0;
    float axis_x = bxr / length_b, axis_y = byr / length_b;
    float crx = (cx - ax) * axis_x + (cy - ay) * axis_y;
    float cry = (cy - ay) * axis_x - (cx - ax) * axis_y;
    float drx = (dx - ax) * axis_x + (dy - ay) * axis_y;
    float dry = (dy - ay) * axis_x - (dx - ax) * axis_y;
    if ((cry < -0.00001f && dry < -0.00001f) || (cry > 0.00001f && dry > 0.00001f)) return 0;
    float difference = cry - dry; if (difference < 0) difference = -difference;
    float tolerance = (cry < 0 ? -cry : cry) * 0.00001f; if (tolerance < 0.00001f) tolerance = 0.00001f;
    if (difference < tolerance) return 0;
    float position = drx + (crx - drx) * dry / (dry - cry);
    if (position < 0 || position > 1) return 0;
    *x = ax + bxr * position; *y = ay + byr * position; return 1;
}
static void update_beam(us_boundary_t *boss, us_boundary_part_t *part) {
    part->beam_x = part->x + part->heading_x * 1000; part->beam_y = part->y + part->heading_y * 1000;
    float r[4]; us_boundary_safe_rect(boss, r);
    float vx[4] = {r[0], r[2], r[2], r[0]}, vy[4] = {r[1], r[1], r[3], r[3]};
    for (unsigned i = 0; i < 4u; ++i) {
        float x, y;
        if (intersect(part->x, part->y, part->beam_x, part->beam_y, vx[i], vy[i], vx[(i + 1) % 4], vy[(i + 1) % 4], &x, &y)) { part->beam_x = x; part->beam_y = y; }
    }
    for(unsigned i=0;i<boss->guard_count&&i<US_BOUNDARY_GUARDS;++i) {
        us_boundary_guard_t *field=&boss->guards[i];if(!field->is_warden||field->age>=field->duration) continue;
        float dx=field->x-part->x,dy=field->y-part->y;
        double along=dx*part->heading_x+dy*part->heading_y;
        double cross=(double)(dx*dx+dy*dy)-along*along;
        if(along<=0||cross>field->radius*field->radius) continue;
        double distance=along-root_double(nonnegative(field->radius*field->radius-cross));if(distance<0) distance=0;
        if(distance<(double)length(part->beam_x-part->x,part->beam_y-part->y)) {
            part->beam_x=part->x+part->heading_x*(float)distance;part->beam_y=part->y+part->heading_y*(float)distance;
            if(part->state==2) {field->block_flash=0.24;field->block_x=-part->heading_x;field->block_y=-part->heading_y;}
        }
    }
}
static void module_step(us_boundary_t *boss, us_boundary_part_t *part, us_boundary_output_t *output) {
    const double delta = boss->delta; part->flash = nonnegative(part->flash - delta); module_position(boss, part->side, part->role, &part->x, &part->y);
    if (part->role != 2) return;
    part->clock -= delta;
    if (part->clock <= 0) {
        part->state = (uint8_t)((part->state + 1) % 3); part->clock = part->state == 1 ? 0.8 : part->state == 2 ? 0.25 : boss->phase ? 0.85 : 1.4;
        if (part->state == 1) direction(boss->px - part->x, boss->py - part->y, &part->heading_x, &part->heading_y);
        update_beam(boss, part);
        if (part->state == 2) {
            float dx = part->beam_x - part->x, dy = part->beam_y - part->y, norm = dx * dx + dy * dy;
            float projection = norm ? ((boss->px - part->x) * dx + (boss->py - part->y) * dy) / norm : 0;
            projection = clamp(projection, 0, 1); float x = part->x + dx * projection, y = part->y + dy * projection;
            if (length(boss->px - x, boss->py - y) < 11) ++output->beam_hits;
        }
    }
    if (part->state) update_beam(boss, part);
}
void us_boundary_hit(us_boundary_t *boss, unsigned target, uint32_t damage_q16, unsigned beam) {
    if (!boss->active || target > US_BOUNDARY_CORE) return;
    double damage = damage_q16 / 65536.0;
    if (target != US_BOUNDARY_CORE) {
        us_boundary_part_t *part = &boss->parts[target]; if (!part->active) return;
        part->flash = 0.12; part->hp = nonnegative(part->hp - damage * (beam ? 2.2 : 1));
        if (part->hp <= 0) {
            part->active = 0;
            if (!part->role && part->captive >= 0) {
                boss->units[part->captive].pending_free = 1; part->captive = -1; part->flash = 0.18; part->state = US_BOUNDARY_RETURN;
            }
            if (part->role == 1) {
                boss->broken[part->side] = 1; --boss->remaining; ++boss->breaks;
                boss->hp = nonnegative(boss->hp - boss->max_hp * 0.2); boss->phase = boss->remaining <= 2;
                if (!boss->remaining) boss->exposed = 1;
                if (boss->hp <= 0) boss->active = 0;
            }
        }
        return;
    }
    if (!boss->exposed) return;
    boss->hp = nonnegative(boss->hp - damage * 1.6); boss->flash = 0.12;
    if (boss->core_hit_clock <= 0) boss->core_hit_clock = 0.08;
    if (boss->hp <= 0) boss->active = 0;
}
void us_boundary_step_delta(us_boundary_t *boss, int32_t px, int32_t py, int32_t vx, int32_t vy, double delta, us_boundary_output_t *output) {
    memset(output, 0, sizeof(*output)); boss->px = px / 65536.0f; boss->py = py / 65536.0f; boss->vx = vx / 65536.0f; boss->vy = vy / 65536.0f;
    if (!boss->active) return;
    boss->delta = delta;
    for (unsigned i = 0; i < US_BOUNDARY_PARTS; ++i) if (boss->parts[i].active) {
        if (!boss->parts[i].role) arm_step(boss, &boss->parts[i], output); else module_step(boss, &boss->parts[i], output);
    }
    boss->flash = nonnegative(boss->flash - delta); boss->elapsed += delta; boss->core_hit_clock = nonnegative(boss->core_hit_clock - delta);
    for (unsigned side = 0; side < 4u; ++side) if (!boss->broken[side]) {
        double limit = (side & 1u) ? 125 : 210; boss->insets[side] += delta * (3 + boss->phase);
        if (boss->insets[side] > limit) boss->insets[side] = limit;
    }
    confine_player(boss); boss->beat_clock -= delta;
    if (boss->beat_clock <= 0) {
        static const unsigned masks[7] = {1,2,4,8,5,10,15}; static const double delays[7] = {0.48,0.48,0.48,1.65,0.95,1.8,3.2};
        for (unsigned side = 0; side < 4u; ++side) if (masks[boss->beat] & (1u << side)) begin_attack(boss, side, boss->beat == 1 || boss->beat == 3 || boss->beat == 5);
        boss->beat_clock = delays[boss->beat]; boss->beat = (boss->beat + 1) % 7;
    }
    if (boss->exposed) {
        boss->shot_clock -= delta;
        if (boss->shot_clock <= 0) {
            boss->shot_clock = 1.1; float dx, dy; direction(boss->px - 400, boss->py - 240, &dx, &dy);
            for (unsigned shot = 0; shot < 3u; ++shot) {
                float cx, sy; us_fx_direction_float((float)(((int)shot - 1) * 0.25), &cx, &sy);
                output->shots[shot].x = 400; output->shots[shot].y = 240;
                output->shots[shot].dx = dx * cx - dy * sy; output->shots[shot].dy = dx * sy + dy * cx;
                output->shots[shot].damage = 12u; output->shots[shot].speed = 150;
            }
            output->shot_count = 3;
        }
    }
}
void us_boundary_step(us_boundary_t *boss, int32_t px, int32_t py, int32_t vx, int32_t vy, us_boundary_output_t *output) {
    us_boundary_flush_deferred(boss);
    us_boundary_step_delta(boss, px, py, vx, vy, 1.0 / 60, output);
}
