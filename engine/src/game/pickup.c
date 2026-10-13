#include <math.h>
#include <string.h>

#include "core/vs_enums.h"
#include "game/game.h"

/* notes/behaviour/pickups.md */

static int pickup_record(vs_game *g, int type, vs_pickups_rec *out) {
    int gi = vs_table_find_group_enum(&g->pickups_table, type);
    if (gi < 0) return -1;
    vs_group grp;
    vs_table_group(&g->pickups_table, (uint32_t)gi, &grp);
    vs_table_record(&g->pickups_table, grp.first, out);
    return 0;
}

static vs_pickup *alloc(vs_game *g) {
    for (int i = 0; i < VS_MAX_PICKUPS; i++) if (!g->pickups[i].active) { memset(&g->pickups[i], 0, sizeof g->pickups[i]); return &g->pickups[i]; }
    return NULL;
}

vs_pickup *vs_pickup_make(vs_game *g, float x, float y, int type) {
    vs_pickup *p = alloc(g);
    if (!p) return NULL;
    vs_pickups_rec r;
    p->active = 1; p->type = type; p->x = x; p->y = y;
    p->value = pickup_record(g, type, &r) == 0 ? r.value : 0;
    p->speed = 250; p->t = 1; p->t_from = 1; p->tween_ms = 500;
    p->treasure_row = -1;
    if (type == VS_PICKUPS_TREASURE) p->go_to_player = 1;
    return p;
}

static int gem_count(const vs_game *g) {
    int n = 0;
    for (int i = 0; i < VS_MAX_PICKUPS; i++) if (g->pickups[i].active && g->pickups[i].type == VS_PICKUPS_GEM) n++;
    return n;
}

static vs_pickup *furthest_gem(vs_game *g) {
    vs_pickup *best = NULL;
    float best_d = -1;
    for (int i = 0; i < VS_MAX_PICKUPS; i++) {
        vs_pickup *p = &g->pickups[i];
        if (!p->active || p->type != VS_PICKUPS_GEM) continue;
        float dx = p->x - g->player.x, dy = p->y - g->player.y, d = dx * dx + dy * dy;
        if (d > best_d) { best_d = d; best = p; }
    }
    return best;
}

static int on_screen_06(const vs_game *g, const vs_pickup *p) {
    /* containmentRect_Screen(0.6): |dx| < 0.6 W and |dy| < 0.6 H (literal) */
    return fabsf(g->player.x - p->x) < 0.6f * g->logical_w && fabsf(g->player.y - p->y) < 0.6f * g->logical_h;
}

void vs_pickup_make_gem(vs_game *g, float x, float y, float value) {
    if (gem_count(g) > VS_MAX_GEMS) {
        float acc = 0;
        int stopped = 0, tries = 0;
        while (gem_count(g) > VS_MAX_GEMS) {
            vs_pickup *f = furthest_gem(g);
            if (f) {
                if (on_screen_06(g, f)) { stopped = 1; break; }
                acc += f->value;
                f->active = 0;
            }
            if (++tries > VS_MAX_GEMS) break;
        }
        vs_pickup *f = furthest_gem(g);
        if (f) f->value += acc;
        if (stopped) { if (f) f->value += value; return; }
    }
    vs_pickup *p = vs_pickup_make(g, x, y, VS_PICKUPS_GEM);
    if (p) p->value = value;
}

static void vacuum(vs_pickup *p) {
    if (p->go_to_player) return;
    p->t_from = -1; p->t = -1; p->tween_ms = 0;
    p->go_to_player = 1;
}

void vs_pickups_vacuum_all(vs_game *g) {
    for (int i = 0; i < VS_MAX_PICKUPS; i++) {
        vs_pickup *p = &g->pickups[i];
        if (p->active && p->type == VS_PICKUPS_GEM) p->go_to_player = 1;
    }
}

static void get_taken(vs_game *g, vs_pickup *p) {
    vs_player *pl = &g->player;
    switch (p->type) {
    case VS_PICKUPS_GEM:
        pl->xp += p->value * pl->growth;
        break;
    case VS_PICKUPS_COIN: case VS_PICKUPS_COINBAG1: case VS_PICKUPS_COINBAG2: case VS_PICKUPS_COINBAGMAX:
        g->coins += p->value * g->gold_multiplier * pl->greed;
        break;
    case VS_PICKUPS_ROAST:
        vs_player_recover_hp(g, p->value);
        break;
    case VS_PICKUPS_LITTLEHEART:
        vs_player_recover_hp(g, 1);
        break;
    case VS_PICKUPS_CLOVER:
        pl->luck += p->value;
        break;
    case VS_PICKUPS_VACUUM:
        vs_pickups_vacuum_all(g);
        break;
    case VS_PICKUPS_OROLOGION:
        vs_enemy_time_stop_all(g, 1);
        vs_timer_cancel(g, VS_TIMER_TIMESTOP, 0);
        vs_timer_add(g, VS_TIMER_TIMESTOP, 10000, 0, 0, 0);
        break;
    case VS_PICKUPS_ROSARY:
        for (int i = 0; i < g->enemy_count; i++) {
            vs_enemy *e = &g->enemies[i];
            if (!e->active || e->res_Rosary) continue;
            if (fabsf(pl->x - e->x) < 0.6f * g->logical_w && fabsf(pl->y - e->y) < 0.6f * g->logical_h)
                vs_enemy_get_damaged(g, e, e->maxHp > 66 ? e->maxHp : 66, VS_HITVFX_NONE, 1);
        }
        break;
    case VS_PICKUPS_TREASURE:
        vs_treasure_open(g, p->treasure_level, p->treasure_row);
        break;
    default: break;
    }
    p->active = 0;
}

/* Slow block (every 4th frame): GoToPlayer sets the velocity. */
void vs_pickups_update(vs_game *g) {
    vs_player *pl = &g->player;
    for (int i = 0; i < VS_MAX_PICKUPS; i++) {
        vs_pickup *p = &g->pickups[i];
        if (!p->active || !p->go_to_player || p->stationary) continue;
        float dx = pl->x - p->x, dy = pl->y - 8 - p->y;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 0) { dx /= len; dy /= len; }
        p->vx = p->speed * dx * p->t;
        p->vy = p->speed * dy * p->t;
        p->speed += 1;
    }
}

/* Every frame: tween, move, magnet and player overlaps. */
void vs_pickups_physics(vs_game *g) {
    vs_player *pl = &g->player;
    const float dt = VS_TICK_MS / 1000.0f;
    float mx = pl->x, my = pl->y - 12, mr = g->magnet.radius;
    float px = pl->x + pl->body_ox, py = pl->y + pl->body_oy, pr = pl->body_r;
    for (int i = 0; i < VS_MAX_PICKUPS; i++) {
        vs_pickup *p = &g->pickups[i];
        if (!p->active) continue;
        if (p->tween_ms < 500) {
            p->tween_ms += VS_TICK_MS;
            float f = p->tween_ms >= 500 ? 1 : p->tween_ms / 500.0f;
            p->t = p->t_from + (1 - p->t_from) * f;
        }
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        float r = p->type == VS_PICKUPS_TREASURE ? 16.0f : 10.0f;
        float dx = p->x - mx, dy = p->y - my, rr = r + mr;
        if (dx * dx + dy * dy < rr * rr) vacuum(p);
        dx = p->x - px; dy = p->y - py; rr = r + pr;
        if (dx * dx + dy * dy < rr * rr) get_taken(g, p);
    }
}

/* Treasure chest: resolve prizes immediately (treasure.md; the chest
 * animation is not reproduced). */
void vs_treasure_open(vs_game *g, int level, int treasure_row) {
    vs_player *pl = &g->player;
    vs_stage *s = &g->stage;
    int prize_types[5] = {VS_PRIZES_EVOLUTION, VS_PRIZES_POWERUP, VS_PRIZES_POWERUP, VS_PRIZES_POWERUP, VS_PRIZES_POWERUP};
    int fixed[5] = {-1, -1, -1, -1, -1};
    if (treasure_row >= 0) {
        vs_stages_rec row; vs_table_record(&s->table, (uint32_t)treasure_row, &row);
        if (VS_HAS(&row, VS_STAGES_F_TREASURE)) {
            const uint8_t *items; uint32_t n = vs_table_list(&s->table, row.treasure, &items);
            if (n >= 1) {
                vs_stages_treasure_rec t; vs_table_record(&s->treasure_table, vs_list_u32(items, 0), &t);
                if (VS_HAS(&t, VS_STAGES_TREASURE_F_PRIZETYPES)) { uint32_t m = vs_table_list(&s->treasure_table, t.prizeTypes, &items); for (uint32_t i = 0; i < m && i < 5; i++) prize_types[i] = vs_list_i32(items, i); }
                if (VS_HAS(&t, VS_STAGES_TREASURE_F_FIXEDPRIZES)) { uint32_t m = vs_table_list(&s->treasure_table, t.fixedPrizes, &items); for (uint32_t i = 0; i < m && i < 5; i++) fixed[i] = vs_list_i32(items, i); }
            }
        }
    }
    int slots = level >= 3 ? 5 : level == 2 ? 3 : 1;
    float coins = level >= 3 ? 200 + 100 * vs_rng_f32(&g->rng) : level == 2 ? 100 + 100 * vs_rng_f32(&g->rng) : 50 + 50 * vs_rng_f32(&g->rng);
    coins = (float)(int)coins;
    int prizes[5]; int prize_count = 0;
    for (int i = 0; i < slots; i++) {
        int type = prize_types[i], item = -1;
        if (type == VS_PRIZES_RANDOM) type = vs_rng_f32(&g->rng) < 0.5f ? VS_PRIZES_POWERUP : VS_PRIZES_EXISTING_ANY;
        switch (type) {
        case VS_PRIZES_POWERUP: item = vs_levelup_pull_remaining_powerup(g); break;
        case VS_PRIZES_EXISTING_WEAPON: item = vs_levelup_pull_existing(g, 0); break;
        case VS_PRIZES_EXISTING_ANY: item = vs_levelup_pull_existing(g, 1); break;
        case VS_PRIZES_EVOLUTION: item = vs_levelup_pull_evolution(g); if (item < 0) item = vs_levelup_pull_existing(g, 1); break;
        case VS_PRIZES_FIXED: item = fixed[i] >= 0 ? vs_levelup_pull_fixed(g, fixed[i]) : -1; if (item < 0) item = vs_levelup_pull_existing(g, 1); break;
        default: break;
        }
        if (item >= 0) { prizes[prize_count++] = item; vs_levelup_remove_from_store(g, item); }
        else coins += 25;
    }
    for (int i = 0; i < prize_count; i++) vs_level_weapon_up(g, prizes[i], 0);
    g->coins += coins * g->gold_multiplier * pl->greed;
    pl->invul_ms += 1000;
    g->chests_opened++;
}
