#include <math.h>
#include <string.h>

#include "core/vs_enums.h"
#include "game/game.h"

/* notes/behaviour/spawning.md and stage-events.md */

#define PI 3.14159265358979f

static vs_pool *pool_find(vs_stage *s, int type, int is_boss) {
    for (int i = 0; i < s->pool_count; i++) if (s->pools[i].type == type && s->pools[i].is_boss == is_boss) return &s->pools[i];
    return NULL;
}
static vs_pool *pool_get(vs_stage *s, int type, int is_boss) {
    vs_pool *p = pool_find(s, type, is_boss);
    if (p) return p;
    if (s->pool_count >= VS_MAX_POOLS) return NULL;
    p = &s->pools[s->pool_count++];
    p->type = type; p->is_boss = is_boss; p->enabled = 1;
    return p;
}

static void update_enemy_pools(vs_stage *s) {
    for (int i = 0; i < s->pool_count; i++) s->pools[i].enabled = 0;
    for (int i = 0; i < s->enemy_count; i++) { vs_pool *p = pool_get(s, s->enemies[i], 0); if (p) p->enabled = 1; }
    for (int i = 0; i < s->boss_count; i++) { vs_pool *p = pool_get(s, s->bosses[i], 1); if (p) p->enabled = 1; }
}

static void start_timers(vs_game *g) {
    vs_stage *s = &g->stage;
    vs_timer_cancel(g, VS_TIMER_SPAWN, 0);
    vs_timer_add(g, VS_TIMER_SPAWN, s->frequency, -1, 0, 0);
    vs_timer_cancel(g, VS_TIMER_DESTRUCTIBLE, 0);
    vs_timer_add(g, VS_TIMER_DESTRUCTIBLE, s->destructibleFreq, -1, 0, 0);
}

static void play_events(vs_game *g, const vs_stages_rec *row) {
    if (!VS_HAS(row, VS_STAGES_F_EVENTS)) return;
    const uint8_t *items;
    uint32_t n = vs_table_list(&g->stage.table, row->events, &items);
    for (uint32_t i = 0; i < n; i++) {
        int rec = (int)vs_list_u32(items, i);
        vs_stages_events_rec ev;
        vs_table_record(&g->stage.events_table, (uint32_t)rec, &ev);
        if (VS_HAS(&ev, VS_STAGES_EVENTS_F_DELAY) && ev.delay > 0) {
            vs_timer_add(g, VS_TIMER_EVENT, ev.delay, VS_HAS(&ev, VS_STAGES_EVENTS_F_REPEAT) ? ev.repeat : 0, rec, 0);
        } else {
            vs_stage_event_trigger(g, rec);
        }
    }
}

/* UpdateAllData: copy the row's fields onto the stage; events/bosses/treasure
 * reset first, everything else persists. */
static void update_all_data(vs_game *g, const vs_stages_rec *row) {
    vs_stage *s = &g->stage;
    s->boss_count = 0;
    s->has_treasure = 0;
    if (VS_HAS(row, VS_STAGES_F_MINIMUM)) s->minimum = row->minimum;
    if (VS_HAS(row, VS_STAGES_F_FREQUENCY)) s->frequency = row->frequency;
    if (VS_HAS(row, VS_STAGES_F_STARTINGSPAWNS)) s->startingSpawns = row->startingSpawns;
    if (VS_HAS(row, VS_STAGES_F_DESTRUCTIBLEFREQ)) s->destructibleFreq = row->destructibleFreq;
    if (VS_HAS(row, VS_STAGES_F_DESTRUCTIBLECHANCE)) s->destructibleChance = row->destructibleChance;
    if (VS_HAS(row, VS_STAGES_F_DESTRUCTIBLECHANCEMAX)) s->destructibleChanceMax = row->destructibleChanceMax;
    if (VS_HAS(row, VS_STAGES_F_MAXDESTRUCTIBLES)) s->maxDestructibles = row->maxDestructibles;
    if (VS_HAS(row, VS_STAGES_F_DESTRUCTIBLETYPE)) s->destructibleType = row->destructibleType;
    if (VS_HAS(row, VS_STAGES_F_DAYNIGHT)) s->day_night = row->dayNight;
    if (VS_HAS(row, VS_STAGES_F_ENEMIES)) {
        const uint8_t *items;
        uint32_t n = vs_table_list(&s->table, row->enemies, &items);
        s->enemy_count = 0;
        for (uint32_t i = 0; i < n && s->enemy_count < 32; i++) s->enemies[s->enemy_count++] = vs_list_i32(items, i);
    }
    if (VS_HAS(row, VS_STAGES_F_BOSSES)) {
        const uint8_t *items;
        uint32_t n = vs_table_list(&s->table, row->bosses, &items);
        for (uint32_t i = 0; i < n && s->boss_count < 8; i++) s->bosses[s->boss_count++] = vs_list_i32(items, i);
    }
    if (VS_HAS(row, VS_STAGES_F_TREASURE)) {
        const uint8_t *items;
        uint32_t n = vs_table_list(&s->table, row->treasure, &items);
        if (n >= 1) { vs_table_record(&s->treasure_table, vs_list_u32(items, 0), &s->treasure); s->has_treasure = 1; }
    }
    update_enemy_pools(s);
    start_timers(g);
    play_events(g, row);
}

int vs_stage_init(vs_game *g, int stage_enum) {
    vs_stage *s = &g->stage;
    memset(s, 0, sizeof *s);
    s->stage_enum = stage_enum;
    if (vs_pack_table(g->pack, "stages", &s->table) != 0 || vs_table_check_fields(&s->table, VS_STAGES_FIELD_NAMES) != 0) return -1;
    if (vs_pack_table(g->pack, "stages.events", &s->events_table) != 0 || vs_table_check_fields(&s->events_table, VS_STAGES_EVENTS_FIELD_NAMES) != 0) return -1;
    if (vs_pack_table(g->pack, "stages.treasure", &s->treasure_table) != 0 || vs_table_check_fields(&s->treasure_table, VS_STAGES_TREASURE_FIELD_NAMES) != 0) return -1;
    int gi = vs_table_find_group_enum(&s->table, stage_enum);
    if (gi < 0) return -1;
    vs_group grp;
    vs_table_group(&s->table, (uint32_t)gi, &grp);
    s->row_count = 0;
    for (uint32_t i = 0; i < grp.count && s->row_count < VS_MAX_STAGE_ROWS; i++) {
        vs_stage_row *r = &s->rows[s->row_count++];
        vs_table_record(&s->table, grp.first + i, &r->rec);
        r->row_index = (int)(grp.first + i);
    }
    /* constructor defaults */
    s->frequency = 1000; s->maximum = 300; s->minimumMultiplier = 1; s->destructibleFreq = 10000;
    s->destructibleChance = 5; s->destructibleChanceMax = 50; s->maxDestructibles = 10; s->destructibleType = VS_DESTRUCTIBLES_BRAZIER;
    s->treasure_attached = 0;

    const vs_stages_rec *row0 = &s->rows[0].rec;
    s->has_tileset = VS_HAS(row0, VS_STAGES_F_TILESET_SETKEY);
    s->spawn_type = VS_HAS(row0, VS_STAGES_F_SPAWNTYPE) ? row0->spawnType : VS_SPAWN_STANDARD;
    /* rectangles (literal, asymmetric) */
    float W = (float)g->logical_w, H = (float)g->logical_h;
    s->rect_outer_x = -0.5f * W - 150; s->rect_outer_y = -0.5f * H - 150; s->rect_outer_w = W + 200; s->rect_outer_h = H + 200;
    s->rect_inner_x = -0.5f * W - 75; s->rect_inner_y = -0.5f * H - 75; s->rect_inner_w = W + 100; s->rect_inner_h = H + 100;
    /* stage mods → constants */
    g->player_px_speed = g->base_player_px_speed * (VS_HAS(row0, VS_STAGES_F_MODS_PLAYERPXSPEED) ? row0->mods_PlayerPxSpeed : 1);
    g->enemy_speed = g->base_enemy_speed * (VS_HAS(row0, VS_STAGES_F_MODS_ENEMYSPEED) ? row0->mods_EnemySpeed : 1);
    g->projectile_speed = g->base_projectile_speed * (VS_HAS(row0, VS_STAGES_F_MODS_PROJECTILESPEED) ? row0->mods_ProjectileSpeed : 1);
    g->gold_multiplier = VS_HAS(row0, VS_STAGES_F_MODS_GOLDMULTIPLIER) ? row0->mods_GoldMultiplier : 1;
    g->enemy_health_multiplier = VS_HAS(row0, VS_STAGES_F_MODS_ENEMYHEALTH) ? row0->mods_EnemyHealth : 1;

    /* Tileset stages: Spawners / PlayerStart from the map (map rendering and
     * tiling arrive with the tilemap renderer; positions are usable now). */
    if (s->has_tileset) {
        const char *map_key = vs_table_str(&s->table, row0->tileset_mapKey);
        vs_map m;
        int mi = vs_pack_find(g->pack, "MAP", map_key);
        if (mi >= 0 && vs_pack_map(g->pack, mi, &m) == 0) {
            for (uint32_t gi2 = 0; gi2 < m.object_group_count; gi2++) {
                uint32_t n; const uint8_t *cur;
                const char *gname = vs_map_object_group(&m, gi2, &n, &cur);
                for (uint32_t k = 0; k < n; k++) {
                    vs_map_object o;
                    vs_map_object_next(&cur, &o);
                    if (strcmp(gname, "Spawners") == 0 && s->spawner_count < 64) s->spawner_y[s->spawner_count++] = o.y;
                    if (strcmp(gname, "Scripts") == 0 && strcmp(o.name, "PlayerStart") == 0) { s->start_x = o.x; s->start_y = o.y; }
                }
            }
        }
    }
    g->player.x = s->start_x; g->player.y = s->start_y;

    s->minute = 0;
    update_all_data(g, row0);
    for (int i = 0; i < s->startingSpawns; i++) vs_stage_spawn_round(g);
    return 0;
}

static vs_enemy *spawn_from_pool(vs_game *g, vs_pool *p, float x, float y) {
    vs_enemy *e = vs_enemy_spawn(g, p->type, x, y, 0);
    return e;
}

static int permanent(vs_game *g) { return vs_enemies_permanent(g); }

static int spawn_in_outer_rect(vs_game *g) {
    vs_stage *s = &g->stage;
    if (permanent(g) >= s->maximum) return 0;
    int any = 0;
    for (int i = 0; i < s->pool_count; i++) {
        vs_pool *p = &s->pools[i];
        if (!p->enabled || p->is_boss) continue;
        /* Phaser RandomOutside(outer, inner): a point in outer but not inner.
         * Pick one of the four border bands by area. */
        float ox = s->rect_outer_x, oy = s->rect_outer_y, ow = s->rect_outer_w, oh = s->rect_outer_h;
        float ix = s->rect_inner_x, iy = s->rect_inner_y, iw = s->rect_inner_w, ih = s->rect_inner_h;
        float top = (iy - oy) * ow, bottom = (oy + oh - (iy + ih)) * ow, left = (ix - ox) * ih, right = (ox + ow - (ix + iw)) * ih;
        float total = top + bottom + left + right;
        float r = vs_rng_f32(&g->rng) * total, px, py;
        if (r < top) { px = ox + vs_rng_f32(&g->rng) * ow; py = oy + vs_rng_f32(&g->rng) * (iy - oy); }
        else if (r < top + bottom) { px = ox + vs_rng_f32(&g->rng) * ow; py = iy + ih + vs_rng_f32(&g->rng) * (oy + oh - (iy + ih)); }
        else if (r < top + bottom + left) { px = ox + vs_rng_f32(&g->rng) * (ix - ox); py = iy + vs_rng_f32(&g->rng) * ih; }
        else { px = ix + iw + vs_rng_f32(&g->rng) * (ox + ow - (ix + iw)); py = iy + vs_rng_f32(&g->rng) * ih; }
        spawn_from_pool(g, p, g->player.x + px, g->player.y + py);
        any = 1;
    }
    return any;
}

static void swarm_check(vs_game *g) {
    vs_stage *s = &g->stage;
    for (int i = 0; permanent(g) < s->minimum * s->minimumMultiplier && i < s->maximum; i++) spawn_in_outer_rect(g);
}

static void spawn_in_random_location(vs_game *g) {
    vs_stage *s = &g->stage;
    if (permanent(g) >= s->maximum) return;
    for (int i = 0; permanent(g) < s->minimum * s->minimumMultiplier && i < s->maximum; i++) {
        for (int k = 0; k < s->pool_count; k++) {
            vs_pool *p = &s->pools[k];
            if (!p->enabled || p->is_boss) continue;
            float edge = vs_rng_f32(&g->rng) < 0.5f ? s->rect_outer_x : s->rect_outer_x + s->rect_outer_w;
            float y = g->tilemap.origin_y[g->tilemap.cur] + s->spawner_y[vs_rng_below(&g->rng, (uint32_t)s->spawner_count)];
            spawn_from_pool(g, p, g->player.x + edge, y);
        }
    }
}

static void spawn_horizontal(vs_game *g) {
    vs_stage *s = &g->stage;
    if (permanent(g) >= s->maximum) return;
    for (int i = 0; permanent(g) < s->minimum * s->minimumMultiplier && i < s->maximum; i++) {
        for (int k = 0; k < s->pool_count; k++) {
            vs_pool *p = &s->pools[k];
            if (!p->enabled || p->is_boss) continue;
            float edge = vs_rng_f32(&g->rng) < 0.5f ? s->rect_outer_x : s->rect_outer_x + s->rect_outer_w;
            spawn_from_pool(g, p, g->player.x + edge, g->player.y);
        }
    }
}

void vs_stage_spawn_round(vs_game *g) {
    vs_stage *s = &g->stage;
    if (s->has_tileset && s->spawner_count > 0) spawn_in_random_location(g);
    else if (s->spawn_type == VS_SPAWN_HORIZONTAL) spawn_horizontal(g);
    else if (spawn_in_outer_rect(g)) swarm_check(g);
}

/* SetTreasureLevelFromChance; beginner's luck counter is save data (meta.md),
 * here it starts at 6 (= no beginner's luck) until the save file exists. */
static int treasure_level_from_chance(vs_game *g, vs_stages_treasure_rec *t) {
    float luck = g->player.luck;
    const uint8_t *items;
    uint32_t n = vs_table_list(&g->stage.treasure_table, t->chances, &items);
    float c0 = n > 0 ? vs_list_f32(items, 0) : 0, c1 = n > 1 ? vs_list_f32(items, 1) : 0, c2 = n > 2 ? vs_list_f32(items, 2) : 0;
    float r1 = 100 * vs_rng_f32(&g->rng), r2 = 100 * vs_rng_f32(&g->rng), r3 = 100 * vs_rng_f32(&g->rng);
    if (r1 <= c0 * luck) return t->level = 3;
    if (r2 <= c1 * luck) return t->level = 2;
    if (r3 <= c2 * luck) return t->level = 1;
    return 0;
}

void vs_stage_spawn_boss(vs_game *g) {
    vs_stage *s = &g->stage;
    for (int i = 0; i < s->pool_count; i++) {
        vs_pool *p = &s->pools[i];
        if (!p->enabled || !p->is_boss) continue;
        float a = 2 * PI * vs_rng_f32(&g->rng), d = 256 * vs_rng_f32(&g->rng);
        float x = g->player.x + 0.9f * cosf(a) * (g->logical_w + d);
        float y = g->player.y + 0.9f * sinf(a) * (g->logical_h + d);
        vs_enemy *e = vs_enemy_spawn(g, p->type, x, y, 0);
        if (!e) continue;
        e->teleport_on_cull = 1;
        if (!s->treasure_attached && s->has_treasure && treasure_level_from_chance(g, &s->treasure) > 0) {
            s->treasure_attached = 1;
            e->treasure_row = s->rows[s->minute < s->row_count ? s->minute : 0].row_index;
        }
    }
}

void vs_stage_check_minute(vs_game *g) {
    vs_stage *s = &g->stage;
    int m = g->survived_seconds / 60;
    if (m > s->minute) {
        for (int i = 0; i < s->row_count; i++) {
            if (s->rows[i].rec.minute == m) { s->minute = m; update_all_data(g, &s->rows[i].rec); break; }
        }
    }
    s->treasure_attached = 0;
    vs_stage_spawn_boss(g);
}


/* ---- events (notes/behaviour/stage-events.md) ---- */

/* Event enemies are tagged with a token (the event record + a serial) so the
 * end timer can find them. */
static int32_t next_token = 1;

static void ring_spawn(vs_game *g, int type, int count, float step, float duration, int random_each) {
    vs_stage *s = &g->stage;
    vs_pool *p = pool_get(s, type, 0);
    if (p) p->enabled = 0;
    int32_t token = next_token++;
    float a = 2 * PI * vs_rng_f32(&g->rng);
    for (int i = 0; i < count; i++) {
        if (random_each) a = 2 * PI * vs_rng_f32(&g->rng);
        float x = g->player.x + 0.72f * cosf(a) * g->logical_w;
        float y = g->player.y + 0.72f * sinf(a) * g->logical_h;
        a += step;
        vs_enemy *e = vs_enemy_spawn(g, type, x, y, 1);
        if (!e) break;
        e->event_token = token;
        s->event_spawned++;
    }
    vs_timer_add(g, VS_TIMER_EVENT_END, duration, 0, token, 0);
}

static void column_spawn(vs_game *g, int type, int count, float duration, int wall) {
    vs_stage *s = &g->stage;
    vs_pool *p = pool_get(s, type, 0);
    if (p) p->enabled = 0;
    int32_t token = next_token++;
    float edge = vs_rng_f32(&g->rng) < 0.5f ? s->rect_outer_x : s->rect_outer_x + s->rect_outer_w;
    for (int i = 0; i < count; i++) {
        float x = g->player.x + edge, y;
        if (wall) {
            int n = s->spawner_count - 1;
            y = g->tilemap.origin_y[g->tilemap.cur] + s->spawner_y[n > 0 ? i % n : 0];
        } else {
            y = g->player.y + 8.0f * (i - 0.5f * count);
        }
        vs_enemy *e = vs_enemy_spawn(g, type, x, y, 1);
        if (!e) break;
        e->event_token = token;
        s->event_spawned++;
    }
    vs_timer_add(g, VS_TIMER_EVENT_END, duration, 0, token, 0);
}

int vs_stage_event_trigger(vs_game *g, int event_record) {
    vs_stages_events_rec ev;
    vs_table_record(&g->stage.events_table, (uint32_t)event_record, &ev);
    int alive = 0;
    for (int i = 0; i < g->enemy_count; i++) alive += g->enemies[i].active;
    if (ev.eventType != VS_EVENTS_ERASE_ENEMIES && alive > 600) return 0;
    float luck = g->player.luck > 0 ? g->player.luck : 1;
    if (VS_HAS(&ev, VS_STAGES_EVENTS_F_CHANCE) && ev.chance && 100 * vs_rng_f32(&g->rng) > ev.chance * (1.0f / luck)) return 0;
    float duration = VS_HAS(&ev, VS_STAGES_EVENTS_F_DURATION) ? ev.duration : 0;
    int more_x = VS_HAS(&ev, VS_STAGES_EVENTS_F_MOREX) ? (int)ev.moreX : 0;
    int more_y = VS_HAS(&ev, VS_STAGES_EVENTS_F_MOREY) && ev.moreY >= 0 ? ev.moreY : VS_ENEMIES_MEDUSA1;
    switch (ev.eventType) {
    case VS_EVENTS_JELLY_WALL: ring_spawn(g, VS_ENEMIES_JELLYFISH, 100, PI / 50, duration ? duration : 60000, 0); break;
    case VS_EVENTS_FLOWER_WALL: ring_spawn(g, VS_ENEMIES_FLOWER, 100, PI / 50, duration ? duration : 60000, 0); break;
    case VS_EVENTS_BAT_SWARM: ring_spawn(g, VS_ENEMIES_BATSWARM, 50, PI / 500, duration ? duration : 10000, 0); break;
    case VS_EVENTS_GHOST_SWARM: ring_spawn(g, VS_ENEMIES_GHOSTSWARM, 20, PI / 500, duration ? duration : 10000, 0); break;
    case VS_EVENTS_SKULL_SWARM: ring_spawn(g, VS_ENEMIES_SKULL2_SWARM, more_x ? more_x : 1, PI / 50, duration ? duration : 15000, 0); break;
    case VS_EVENTS_SHADE_BOMB_RED: ring_spawn(g, VS_ENEMIES_SHADERED, more_x ? more_x : 1, PI / 500, duration ? duration : 60000, 1); break;
    case VS_EVENTS_MEDUSA_SWARM: column_spawn(g, more_y, more_x ? more_x : 1, duration ? duration : 15000, 0); break;
    case VS_EVENTS_MEDUSA_WALL: column_spawn(g, more_y, more_x ? more_x : 1, duration ? duration : 15000, g->stage.has_tileset); break;
    case VS_EVENTS_ERASE_ENEMIES:
        for (int i = 0; i < g->enemy_count; i++) {
            vs_enemy *e = &g->enemies[i];
            if (e->active && !e->res_Rosary) vs_enemy_disappear(g, e);
        }
        break;
    default: break;
    }
    return 1;
}

void vs_stage_event_end(vs_game *g, int32_t token) {
    for (int i = 0; i < g->enemy_count; i++) {
        vs_enemy *e = &g->enemies[i];
        if (!e->active || !e->from_event || e->event_token != token) continue;
        e->from_event = 0;
        g->stage.event_spawned--;
        if (!e->is_dead) { e->cullable = 1; vs_enemy_disappear(g, e); }
    }
}
