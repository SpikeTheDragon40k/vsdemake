#include <math.h>
#include <stdio.h>
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
    if (type == VS_PICKUPS_WEAPON) p->stationary = 1;   /* isStationary: the magnet doesn't pull it */
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
    vs_save_count_pickup(g, p->type);
    switch (p->type) {
    case VS_PICKUPS_GEM:
        pl->xp += p->value * pl->growth;
        vs_sfx(g, VS_SND_GEM, 0.1f, 0, 1, 1, 1);
        break;
    case VS_PICKUPS_COIN: case VS_PICKUPS_COINBAG1: case VS_PICKUPS_COINBAG2: case VS_PICKUPS_COINBAGMAX:
        g->coins += p->value * g->gold_multiplier * pl->greed;
        vs_sfx(g, VS_SND_COIN, p->type == VS_PICKUPS_COINBAGMAX ? 0.65f : 0.5f, 0, 1, 0, 0);
        break;
    case VS_PICKUPS_ROAST:
        vs_player_recover_hp(g, p->value);
        pl->invul_ms += 300;   /* SetInvulForMilliSeconds(300) */
        vs_sfx(g, VS_SND_ROAST, 0.6f, 0, 1, 0, 0);
        break;
    case VS_PICKUPS_LITTLEHEART:
        vs_player_recover_hp(g, 1);
        vs_sfx(g, VS_SND_LITTLEHEART, 0.35f, 200 * (vs_sfx_random(g) - 0.5f), 1.2f, 50, 3);
        break;
    case VS_PICKUPS_CLOVER:
        pl->luck += p->value;
        break;
    case VS_PICKUPS_VACUUM:
        vs_pickups_vacuum_all(g);
        vs_sfx(g, VS_SND_VACUUM, 0.6f, 0, 1, 0, 0);
        break;
    case VS_PICKUPS_OROLOGION:
        vs_enemy_time_stop_all(g, 1);
        vs_timer_cancel(g, VS_TIMER_TIMESTOP, 0);
        vs_timer_add(g, VS_TIMER_TIMESTOP, 10000, 0, 0, 0);
        vs_sfx(g, VS_SND_OROLOGION, 1.8f, 0, 2, 0, 0);
        break;
    case VS_PICKUPS_ROSARY:
        vs_sfx(g, VS_SND_ROSARY, 1.8f, 0, 2, 0, 0);
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
    case VS_PICKUPS_NFT: {
        /* Nduja Fritta (line 1330): the hidden fire weapon for 10 s ×
         * duration; another one restarts the time */
        int slot = vs_weapon_find(g, VS_WEAPONS_NDUJA);
        if (slot < 0) {
            slot = vs_weapon_add(g, VS_WEAPONS_NDUJA, 0);
            if (slot >= 0) g->weapons[slot].hidden = 1;
        }
        if (slot >= 0) g->weapons[slot].hidden_left_ms = 10000.0f * pl->duration;
        break;
    }
    case VS_PICKUPS_WEAPON:
        /* map bonus item: the item-found screen offers it (take / leave);
         * headless runs take it */
        if (p->weapon_type <= 0) break;
        if (g->auto_pick && !g->show_chest) vs_level_weapon_up(g, p->weapon_type, 1);
        else vs_item_found_start(g, p->weapon_type);
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
        if (p->type == VS_PICKUPS_WEAPON) p->anim_ms += VS_TICK_MS;
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

/* Treasure chest (treasure.md): the prizes are decided now, as the
 * original does when its treasure scene starts; they are given when the
 * screen closes (vs_treasure_award). Headless runs skip the screen. */
void vs_treasure_open(vs_game *g, int level, int treasure_row) {
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
    vs_save_count_pickup(g, VS_PICKUPS_STATS_TREASURE_1 + (slots == 5 ? 2 : slots == 3 ? 1 : 0));
    float coins = level >= 3 ? 200 + 100 * vs_rng_f32(&g->rng) : level == 2 ? 100 + 100 * vs_rng_f32(&g->rng) : 50 + 50 * vs_rng_f32(&g->rng);
    coins = (float)(int)coins;
    int prizes[5];
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
        prizes[i] = item;
        if (item >= 0) vs_levelup_remove_from_store(g, item);
    }
    if (g->auto_pick && !g->show_chest) {
        if (slots == 5) vs_sfx(g, VS_SND_TREASURE3, 1.1f, 0, 1, 0, 0);
        else vs_sfx(g, slots == 3 ? VS_SND_TREASURE2 : VS_SND_TREASURE1, 1.0f, 0, 1, 0, 0);
        vs_treasure_award(g, slots, prizes, coins);
    } else vs_treasure_ui_start(g, level >= 3 ? 3 : level == 2 ? 2 : 1, slots, prizes, coins);
}

/* OnSceneOver: every prize levelled up (store untouched), CoinsAward plus
 * 25 per filler as coins (Greed and gold multiplier apply), 1 s of
 * invulnerability. */
void vs_treasure_award(vs_game *g, int n, const int *prizes, float coins_award) {
    vs_player *pl = &g->player;
    float coins = coins_award;
    for (int i = 0; i < n; i++) {
        if (prizes[i] >= 0) vs_level_weapon_up(g, prizes[i], 0);
        else coins += 25;
    }
    g->coins += coins * g->gold_multiplier * pl->greed;
    pl->invul_ms += 1000;
    g->chests_opened++;
}

/* ---- loot (LootManager, line 6056) and destructibles (line 5974) ---- */

void vs_loot_recalculate(vs_game *g) {
    vs_loot *l = &g->loot;
    l->count = 0; l->acc = 0;
    for (uint32_t gi = 0; gi < g->pickups_table.group_count; gi++) {
        vs_group grp; vs_table_group(&g->pickups_table, gi, &grp);
        vs_pickups_rec r; vs_table_record(&g->pickups_table, grp.first, &r);
        if (g->player.level < r.unlocksAt) continue;
        l->acc += (VS_HAS(&r, VS_PICKUPS_F_ISRARE) && r.isRare) ? r.rarity * g->player.luck : r.rarity;
        if (r.rarity > 0 && l->count < 32) { l->type[l->count] = grp.enum_index; l->w[l->count] = l->acc; l->count++; }
    }
}

int vs_loot_random_item(vs_game *g) {
    vs_loot *l = &g->loot;
    float r = vs_rng_f32(&g->rng) * l->acc;
    for (int i = 0; i < l->count; i++) if (l->w[i] >= r) return l->type[i];
    return VS_PICKUPS_COIN;
}

static vs_destructible *furthest_destructible(vs_game *g) {
    vs_destructible *best = NULL; float bd = -1;
    for (int i = 0; i < VS_MAX_DESTRUCTIBLES; i++) {
        vs_destructible *d = &g->destructibles[i];
        if (!d->active) continue;
        float dx = d->x - g->player.x, dy = d->y - g->player.y, dd = dx * dx + dy * dy;
        if (dd > bd) { bd = dd; best = d; }
    }
    return best;
}

void vs_destructible_make(vs_game *g, int type, float x, float y, int has_pos) {
    if (!has_pos) vs_stage_position_out_of_sight(g, (g->player.vx == 0 && g->player.vy == 0) ? 0.0f : 45.0f, 0, &x, &y);
    vs_destructible *d = NULL;
    for (int i = 0; i < VS_MAX_DESTRUCTIBLES; i++) if (!g->destructibles[i].active) { d = &g->destructibles[i]; break; }
    if (!d) { d = furthest_destructible(g); if (!d) return; }
    memset(d, 0, sizeof *d);
    int gi = vs_table_find_group_enum(&g->destructibles_table, type);
    vs_destructibles_rec r; if (gi >= 0) { vs_group grp; vs_table_group(&g->destructibles_table, (uint32_t)gi, &grp); vs_table_record(&g->destructibles_table, grp.first, &r); }
    d->active = 1; d->type = type; d->x = x; d->y = y;
    d->maxHp = gi >= 0 ? r.maxHp : 1; d->hp = d->maxHp;
    if (gi >= 0) strncpy(d->frame_prefix, vs_table_str(&g->destructibles_table, r.frameName), sizeof d->frame_prefix - 1);
    vs_frame f; char name[48]; snprintf(name, sizeof name, "%s1.png", d->frame_prefix);
    if (vs_atlas_find(&g->atl_items, name, &f) == 0) { d->src_w = f.src_w; d->src_h = f.src_h; } else { d->src_w = d->src_h = 32; }
    int n = 0; for (int i = 0; i < VS_MAX_DESTRUCTIBLES; i++) n += g->destructibles[i].active;
    while (n > g->stage.maxDestructibles) { vs_destructible *f2 = furthest_destructible(g); if (!f2) break; f2->active = 0; n--; }
}

void vs_destructible_damage(vs_game *g, vs_destructible *d, float dmg) {
    if (d->is_dead) return;
    d->hp -= dmg;
    d->blink_ms = 120;
    if (d->hp <= 0) {
        d->is_dead = 1;
        vs_save_count_destroyed(g, d->type);
        vs_pickup_make(g, d->x, d->y, vs_loot_random_item(g));
    }
}

void vs_destructibles_update(vs_game *g) {
    for (int i = 0; i < VS_MAX_DESTRUCTIBLES; i++) {
        vs_destructible *d = &g->destructibles[i];
        if (!d->active) continue;
        if (d->blink_ms > 0) { d->blink_ms -= VS_TICK_MS; if (d->blink_ms <= 0) { d->blink_ms = 0; if (d->is_dead) d->active = 0; } }
    }
}
