#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/vs_enums.h"
#include "game/game.h"

/* notes/behaviour/enemies.md */


static int frame_prefix(vs_game *g, const vs_enemies_rec *r, const char *tex, char *out, size_t n) {
    /* frameNames: pick one at random, strip "0.png" → prefix */
    const uint8_t *items;
    uint32_t cnt = vs_table_list(&g->enemies_table, r->frameNames, &items);
    if (cnt == 0) return -1;
    const char *name = vs_table_str(&g->enemies_table, vs_list_u32(items, vs_rng_below(&g->rng, cnt)));
    const char *suffix = strstr(name, "0.png");
    size_t plen = suffix ? (size_t)(suffix - name) : strlen(name);
    if (plen >= n) plen = n - 1;
    memcpy(out, name, plen);
    out[plen] = 0;
    (void)tex;
    return 0;
}

vs_enemy *vs_enemy_spawn(vs_game *g, int type, float x, float y, int from_event) {
    if (g->enemy_count >= VS_MAX_ENEMIES) return NULL;
    int gi = vs_table_find_group_enum(&g->enemies_table, type);
    if (gi < 0) return NULL;
    vs_group grp;
    vs_table_group(&g->enemies_table, (uint32_t)gi, &grp);
    vs_enemies_rec r;
    vs_table_record(&g->enemies_table, grp.first, &r);

    vs_enemy *e = &g->enemies[g->enemy_count++];
    memset(e, 0, sizeof *e);
    e->active = 1;
    e->uid = g->next_uid++;
    e->type = type;
    e->x = x; e->y = y;
    e->treasure_row = -1;
    e->from_event = from_event;
    e->cullable = !from_event;

    /* Init (per instance in the original; here per spawn, which only changes
     * when a random maxSpeed is re-rolled on reuse). */
    float speed = r.speed;
    if (VS_HAS(&r, VS_ENEMIES_F_MAXSPEED)) speed = vs_rng_f32(&g->rng) * (r.maxSpeed - r.speed) + r.speed;
    e->data_maxHp = r.maxHp; e->power = r.power; e->knockback = r.knockback; e->maxKnockback = r.maxKnockback;
    e->deathKB = r.deathKB; e->xp = r.xp; e->speed = e->default_speed = speed;
    e->more_x = r.moreX; e->more_y = r.moreY;
    e->alpha = VS_HAS(&r, VS_ENEMIES_F_ALPHA) ? r.alpha : 1.0f;
    e->scale = VS_HAS(&r, VS_ENEMIES_F_SCALE) ? r.scale : 1.0f;
    e->res_Freeze = r.res_Freeze; e->res_Debuffs = r.res_Debuffs; e->res_Rosary = r.res_Rosary;
    e->idle_frames = r.idleFrameCount;
    e->pass_through_walls = r.passThroughWalls;
    e->frame_end = r.end;
    if (VS_HAS(&r, VS_ENEMIES_F_SKILLS)) {
        const uint8_t *items;
        uint32_t cnt = vs_table_list(&g->enemies_table, r.skills, &items);
        for (uint32_t i = 0; i < cnt; i++) {
            switch (vs_list_i32(items, i)) {
            case 0: e->hp_x_level = 1; break;
            case 1: e->fixed_direction = 1; break;
            case 2: e->self_destruct = 1; break;
            case 3: e->medusa = 1; e->medusa_elapsed = 2.0f * vs_rng_f32(&g->rng); break;
            default: break;
            }
        }
    }
    frame_prefix(g, &r, vs_table_str(&g->enemies_table, r.textureName), e->frame_prefix, sizeof e->frame_prefix);
    vs_frame f;
    char name[48];
    snprintf(name, sizeof name, "%s0.png", e->frame_prefix);
    if (vs_atlas_find(&g->atl_enemies, name, &f) == 0) { e->src_w = f.src_w; e->src_h = f.src_h; }
    else { e->src_w = e->src_h = 16; }

    /* OnRecycle */
    e->maxHp = e->data_maxHp * g->enemy_health_multiplier;
    if (e->hp_x_level) e->maxHp = e->data_maxHp * g->player.level * g->enemy_health_multiplier;
    e->hp = e->maxHp;
    e->damageKB = 1;
    e->time_stopped = g->time_stopped;
    /* collider: r = 0.4 w, offset (r/2, r/2) from the top-left; origin (0.5, 1);
     * colliderOverride: r = w × radius, offset (r/2, r/2 + offsetY) */
    float w = e->src_w, h = e->src_h;
    float r0 = 0.4f * w, ox = 0.5f * r0, oy = 0.5f * r0;
    if (VS_HAS(&r, VS_ENEMIES_F_COLLIDER_RADIUS)) {
        r0 = w * r.collider_radius; ox = 0.5f * r0; oy = 0.5f * r0 + r.collider_offsetY;
    }
    e->body_r = r0 * e->scale;
    e->body_ox = (-0.5f * w + ox + r0) * e->scale;
    e->body_oy = (-h + oy + r0) * e->scale;
    if (e->idle_frames) e->anim_frame_start = (int)ceilf(vs_rng_f32(&g->rng) * (0.5f * e->idle_frames));
    e->wobble = (int)vs_rng_below(&g->rng, 10);
    return e;
}

static void despawn(vs_game *g, vs_enemy *e) {
    if (e->from_event) g->stage.event_spawned--;
    e->active = 0;
}

void vs_enemy_disappear(vs_game *g, vs_enemy *e) {
    (void)g;
    if (e->is_dead) return;
    e->is_dead = 1;
    e->dying = 1;
    e->anim_t = 0;
    e->vx = e->vy = 0;
}

void vs_enemy_die(vs_game *g, vs_enemy *e) {
    if (e->is_dead) return;
    e->is_dead = 1;
    e->dying = 1;
    e->anim_t = 0;
    float k = -e->deathKB;
    e->vx = g->enemy_speed * e->speed * e->dir_x * k;
    e->vy = g->enemy_speed * e->speed * e->dir_y * k;
    e->blink_ms = 0;
    int gems = (int)floorf((vs_rng_f32(&g->rng) + 0.5f) * e->xp);
    if (gems > 0) vs_pickup_make_gem(g, e->x, e->y, (float)gems);
    if (e->treasure_row >= 0) {
        vs_pickup *chest = vs_pickup_make(g, e->x, e->y, VS_PICKUPS_TREASURE);
        if (chest) { chest->treasure_level = g->stage.treasure.level; chest->treasure_row = e->treasure_row; }
        e->treasure_row = -1;
    }
    g->kills++;
}

void vs_enemy_get_damaged(vs_game *g, vs_enemy *e, float dmg, int hit_type, float kb) {
    if (e->is_dead) return;
    if (dmg > 0) vs_fx_damage_number(g, e->x, e->y, dmg);
    e->hp -= dmg;
    if (e->hp <= 0) vs_enemy_die(g, e);
    else e->damageKB = kb;
    /* OnGetDamaged: tint and blink from the hitvfx table */
    float duration = e->is_dead ? 60.0f : 120.0f;
    e->hit_tint = 0xffffff;
    if (hit_type != VS_HITVFX_NONE) vs_fx_hit(g, e->x, e->y, hit_type);
    if (hit_type != VS_HITVFX_NONE) {
        int gi = vs_table_find_group_enum(&g->hitvfx_table, hit_type);
        if (gi >= 0) {
            vs_group grp;
            vs_table_group(&g->hitvfx_table, (uint32_t)gi, &grp);
            vs_hitvfx_rec h;
            vs_table_record(&g->hitvfx_table, grp.first, &h);
            e->hit_tint = (uint32_t)h.targetTint;
            if (!e->is_dead) duration = h.duration;
        }
    }
    e->blink_ms = duration;
    e->receiving_damage = 1;
    if (dmg >= 10000) vs_player_get_damaged(g, 9);
}

void vs_enemy_time_stop_all(vs_game *g, int stopped) {
    g->time_stopped = stopped;
    for (int i = 0; i < g->enemy_count; i++) {
        vs_enemy *e = &g->enemies[i];
        if (!e->active) continue;
        e->time_stopped = stopped;
        e->speed = stopped ? 0 : e->default_speed;
        if (stopped) e->vx = e->vy = 0;
    }
}

int vs_enemies_permanent(const vs_game *g) {
    int alive = 0;
    for (int i = 0; i < g->enemy_count; i++) if (g->enemies[i].active) alive++;
    return alive - g->stage.event_spawned;
}

/* One enemy's per-frame update (notes/behaviour/enemies.md). */
static void update_one(vs_game *g, vs_enemy *e) {
    vs_player *p = &g->player;
    const float dt = VS_TICK_MS / 1000.0f;
    if (e->dying) {
        /* death animation: frames 0..end at 24 fps, then despawn */
        e->anim_t += VS_TICK_MS;
        e->x += e->vx * dt; e->y += e->vy * dt;
        if (e->anim_t >= (e->frame_end + 1) * (1000.0f / 24.0f)) despawn(g, e);
        return;
    }
    if (e->blink_ms > 0) {
        e->blink_ms -= VS_TICK_MS;
        if (e->blink_ms <= 0) { e->blink_ms = 0; e->receiving_damage = 0; }
    }
    if (e->time_stopped) return;
    if (!e->fixed_direction || (e->dir_x == 0 && e->dir_y == 0)) {
        float dx = p->x - e->x, dy = p->y - e->y;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 0) { e->dir_x = dx / len; e->dir_y = dy / len; }
    }
    if (e->medusa) {
        e->medusa_elapsed += 0.05f;
        e->dir_y = cosf(e->medusa_elapsed);
    }
    if (e->self_destruct && !e->sd_triggered) {
        float dx = p->x - e->x, dy = p->y - e->y;
        if (dx * dx + dy * dy < 40000.0f) {
            e->sd_triggered = 1;
            vs_timer_add(g, VS_TIMER_SELFDESTRUCT, 2000, 0, (int32_t)(e - g->enemies), 0);
        }
    }
    float k = e->receiving_damage ? -e->knockback * e->damageKB : 1.0f;
    e->vx = g->enemy_speed * e->speed * e->dir_x * k;
    e->vy = g->enemy_speed * e->speed * e->dir_y * k;
    e->x += e->vx * dt;
    e->y += e->vy * dt;
    if (!e->pass_through_walls) { float cx = e->x + e->body_ox, cy = e->y + e->body_oy; vs_tilemap_collide(g, &cx, &cy, e->body_r, 1); e->x = cx - e->body_ox; e->y = cy - e->body_oy; }
    e->anim_t += VS_TICK_MS;
}

/* Arcade-style circle separation. Enemies push each other apart (equal
 * mass: half each); the player is immovable so enemies are pushed out fully
 * and the contact damages the player (notes/behaviour/physics.md). */
static void separate(vs_game *g) {
    vs_player *p = &g->player;
    float px = p->x + p->body_ox, py = p->y + p->body_oy;
    int n = g->enemy_count;
    for (int i = 0; i < n; i++) {
        vs_enemy *a = &g->enemies[i];
        if (!a->active || a->dying) continue;
        float ax = a->x + a->body_ox, ay = a->y + a->body_oy;
        /* vs player */
        if (!p->is_dead) {
            float dx = ax - px, dy = ay - py, rr = a->body_r + p->body_r;
            float d2 = dx * dx + dy * dy;
            if (d2 < rr * rr) {
                if (!a->time_stopped) vs_player_get_damaged(g, a->power);
                float d = sqrtf(d2);
                if (d < 0.0001f) { dx = 1; dy = 0; d = 1; }
                float push = rr - d;
                a->x += dx / d * push; a->y += dy / d * push;
                ax = a->x + a->body_ox; ay = a->y + a->body_oy;
            }
        }
        if (a->pass_through_walls) continue; /* not in the collision group */
        for (int j = i + 1; j < n; j++) {
            vs_enemy *b = &g->enemies[j];
            if (!b->active || b->dying || b->pass_through_walls) continue;
            float bx = b->x + b->body_ox, by = b->y + b->body_oy;
            float dx = bx - ax, dy = by - ay, rr = a->body_r + b->body_r;
            if (fabsf(dx) > rr || fabsf(dy) > rr) continue;
            float d2 = dx * dx + dy * dy;
            if (d2 >= rr * rr) continue;
            float d = sqrtf(d2);
            if (d < 0.0001f) { dx = 1; dy = 0; d = 1; }
            float push = 0.5f * (rr - d);
            float ux = dx / d, uy = dy / d;
            a->x -= ux * push; a->y -= uy * push;
            b->x += ux * push; b->y += uy * push;
            ax = a->x + a->body_ox; ay = a->y + a->body_oy;
        }
    }
}

void vs_enemies_update(vs_game *g) {
    for (int i = 0; i < g->enemy_count; i++) {
        vs_enemy *e = &g->enemies[i];
        if (e->active) update_one(g, e);
    }
    separate(g);
    /* compact */
    int w = 0;
    for (int i = 0; i < g->enemy_count; i++) {
        if (!g->enemies[i].active) continue;
        if (w != i) g->enemies[w] = g->enemies[i];
        w++;
    }
    g->enemy_count = w;
}

/* 10 round-robin checks per frame against 0.7·W / 0.8·H (loop.md). */
void vs_enemies_cull_step(vs_game *g) {
    if (g->enemy_count == 0) return;
    float lim_x = 0.7f * g->logical_w, lim_y = 0.8f * g->logical_h;
    for (int k = 0; k < 10; k++) {
        vs_enemy *e = &g->enemies[g->cull_index % g->enemy_count];
        g->cull_index++;
        if (!e->active || (!e->cullable && !e->teleport_on_cull)) continue;
        float dx = fabsf(g->player.x - e->x), dy = fabsf(g->player.y - e->y);
        if (dx <= lim_x && dy <= lim_y) continue;
        if (e->teleport_on_cull) {
            /* GetPositionForEnemyOutOfSight(5°, 48): heading ± 2.5°, half screen + 48 px */
            float a;
            vs_player *p = &g->player;
            if (g->stage.has_tileset && g->stage.spawner_count > 0) {
                float edge = vs_rng_f32(&g->rng) < 0.5f ? g->stage.rect_outer_x : g->stage.rect_outer_x + g->stage.rect_outer_w;
                float sgn = p->vx > 0 ? 1.0f : -1.0f;
                e->x = p->x + edge * sgn; e->y = p->y;
            } else {
                a = atan2f(p->vy, p->vx) + (vs_rng_f32(&g->rng) - 0.5f) * (5.0f * 3.14159265f / 180.0f);
                float c = cosf(a), s = sinf(a);
                e->x = p->x + 0.5f * c * g->logical_w + (c > 0 ? 48.0f : c < 0 ? -48.0f : 0);
                e->y = p->y + 0.5f * s * g->logical_h + (s > 0 ? 48.0f : s < 0 ? -48.0f : 0);
            }
            continue;
        }
        despawn(g, e);
    }
}

vs_enemy *vs_enemy_closest_to_player(vs_game *g) {
    vs_enemy *best = NULL;
    float best_d = 0;
    for (int i = 0; i < g->enemy_count; i++) {
        vs_enemy *e = &g->enemies[i];
        if (!e->active || e->is_dead) continue;
        float dx = e->x - g->player.x, dy = e->y - g->player.y, d = dx * dx + dy * dy;
        if (!best || d < best_d) { best = e; best_d = d; }
    }
    return best;
}
