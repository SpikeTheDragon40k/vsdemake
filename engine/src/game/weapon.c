#include <math.h>
#include <string.h>

#include "core/vs_enums.h"
#include "game/game.h"

/* notes/behaviour/weapons.md. Weapon objects accumulate table deltas; bullets
 * are per-weapon-type behaviours. */

#define PI 3.14159265358979f

static int weapon_record(vs_game *g, int type, int level, vs_weapons_rec *out) {
    int gi = vs_table_find_group_enum(&g->weapons_table, type);
    if (gi < 0) return -1;
    vs_group grp;
    vs_table_group(&g->weapons_table, (uint32_t)gi, &grp);
    if ((uint32_t)level >= grp.count) return -1;
    vs_table_record(&g->weapons_table, grp.first + (uint32_t)level, out);
    return 0;
}

const char *vs_weapon_name(const vs_game *g, int type) {
    vs_weapons_rec r;
    if (weapon_record((vs_game *)g, type, 0, &r) != 0 || !VS_HAS(&r, VS_WEAPONS_F_NAME)) return "?";
    return vs_table_str(&g->weapons_table, r.name);
}

/* Adds (or sets) every field of a table entry onto the weapon object. */
static void apply_entry(vs_weapon *w, const vs_weapons_rec *r, int set) {
#define F(field, name) if (VS_HAS(r, VS_WEAPONS_F_##name)) { if (set) w->field = r->field; else w->field += r->field; }
    F(interval, INTERVAL) F(repeatInterval, REPEATINTERVAL) F(power, POWER) F(area, AREA) F(speed, SPEED) F(amount, AMOUNT)
    F(duration, DURATION) F(critChance, CRITCHANCE) F(critMul, CRITMUL) F(hitBoxDelay, HITBOXDELAY) F(knockback, KNOCKBACK)
    F(chance, CHANCE) F(shieldInvulTime, SHIELDINVULTIME) F(volume, VOLUME) F(penetrating, PENETRATING)
    F(charges, CHARGES)
#undef F
    /* non-additive fields */
    if (VS_HAS(r, VS_WEAPONS_F_HITVFX)) w->hitVFX = r->hitVFX;
    if (VS_HAS(r, VS_WEAPONS_F_HITSWALLS)) w->hitsWalls = r->hitsWalls;
    if (VS_HAS(r, VS_WEAPONS_F_INTERVALDEPENDSONDURATION)) w->intervalDependsOnDuration = r->intervalDependsOnDuration;
    if (VS_HAS(r, VS_WEAPONS_F_ISPOWERUP)) w->is_power_up = r->isPowerUp;
    if (VS_HAS(r, VS_WEAPONS_F_POOLLIMIT)) w->poolLimit = r->poolLimit;
    /* passive bonus fields (own fields of the power-up weapon object) */
#define B(field, name) if (VS_HAS(r, VS_WEAPONS_F_##name)) { if (set) w->b_##field = r->field; else w->b_##field += r->field; }
    B(power, POWER) B(area, AREA) B(speed, SPEED) B(cooldown, COOLDOWN) B(amount, AMOUNT) B(maxHp, MAXHP) B(moveSpeed, MOVESPEED)
    B(growth, GROWTH) B(magnet, MAGNET) B(luck, LUCK) B(armor, ARMOR) B(greed, GREED) B(regen, REGEN) B(duration, DURATION)
#undef B
}

float vs_weapon_p_area(const vs_game *g, const vs_weapon *w) { return g->player.area * w->area; }
int vs_weapon_p_amount(const vs_game *g, const vs_weapon *w) { return (int)(g->player.amount + w->amount); }
float vs_weapon_p_power(const vs_game *g, const vs_weapon *w) { return g->player.power * w->power; }
static float p_speed(const vs_game *g, const vs_weapon *w) { return g->player.speed * w->speed; }
float vs_weapon_p_interval(const vs_game *g, const vs_weapon *w) { return (g->player.cooldown > 0.1f ? g->player.cooldown : 0.1f) * w->interval; }

static void reset_firing_timer(vs_game *g, int slot) {
    vs_weapon *w = &g->weapons[slot];
    vs_timer_cancel(g, VS_TIMER_WEAPON_FIRE, slot);
    if (w->is_power_up) return;
    float delay = w->intervalDependsOnDuration ? w->duration + vs_weapon_p_interval(g, w) : vs_weapon_p_interval(g, w);
    vs_timer_add(g, VS_TIMER_WEAPON_FIRE, delay, -1, slot, 0);
}

void vs_weapons_reset_timers(vs_game *g) {
    for (int i = 0; i < VS_MAX_WEAPONS; i++) if (g->weapons[i].active) reset_firing_timer(g, i);
}

int vs_weapon_find(const vs_game *g, int type) {
    for (int i = 0; i < VS_MAX_WEAPONS; i++) if (g->weapons[i].active && g->weapons[i].type == type) return i;
    return -1;
}

static void passive_apply(vs_game *g, vs_weapon *w) {
    vs_player *p = &g->player;
    p->power += w->b_power; p->area += w->b_area; p->speed += w->b_speed; p->cooldown += w->b_cooldown; p->amount += w->b_amount;
    p->moveSpeed += w->b_moveSpeed; p->growth += w->b_growth; p->luck += w->b_luck; p->armor += w->b_armor; p->duration += w->b_duration;
    p->greed += w->b_greed; p->regen += w->b_regen; p->maxHp += p->maxHp * w->b_maxHp;
    if (w->b_cooldown > 0) vs_weapons_reset_timers(g);
    if (w->b_magnet > 0) { g->magnet.radius += g->magnet.radius * w->b_magnet; g->magnet.percentage += w->b_magnet; }
}

/* MakeLevelOne + constructor (weapon.c ↔ weapons.md "Weapon object"). */
int vs_weapon_add(vs_game *g, int type, int from_levelup) {
    int slot = -1;
    for (int i = 0; i < VS_MAX_WEAPONS; i++) if (!g->weapons[i].active) { slot = i; break; }
    if (slot < 0) return -1;
    vs_weapon *w = &g->weapons[slot];
    memset(w, 0, sizeof *w);
    w->active = 1; w->type = type;
    w->interval = 1000; w->repeatInterval = 100; w->power = w->area = w->speed = w->amount = 1; w->duration = 1000;
    w->critMul = 2; w->hitBoxDelay = 1000; w->hitVFX = VS_HITVFX_DEFAULT; w->knockback = 5; w->hitsWalls = 1; w->penetrating = 1; w->poolLimit = 50;
    vs_weapons_rec r;
    if (weapon_record(g, type, 0, &r) != 0) { w->active = 0; return -1; }
    apply_entry(w, &r, 1);
    w->level = 0;
    if (VS_HAS(&r, VS_WEAPONS_F_EVOLVESFROM)) {
        const uint8_t *items;
        uint32_t n = vs_table_list(&g->weapons_table, r.evolvesFrom, &items);
        for (uint32_t i = 0; i < n; i++) vs_weapon_remove(g, vs_list_i32(items, i));
    }
    if (w->is_power_up) {
        /* passive constructor: bonuses start at 0, MakeLevelOne sets them */
        w->level = 1;
        passive_apply(g, w);
    } else {
        w->level = 1;
        reset_firing_timer(g, slot);
    }
    if (from_levelup) vs_levelup_remove_from_store(g, type); else vs_levelup_calculate_weights(g);
    return slot;
}

void vs_weapon_remove(vs_game *g, int type) {
    int slot = vs_weapon_find(g, type);
    if (slot < 0) return;
    vs_timer_cancel(g, VS_TIMER_WEAPON_FIRE, slot);
    vs_timer_cancel(g, VS_TIMER_WEAPON_REPEAT, slot);
    for (int i = 0; i < VS_MAX_BULLETS; i++) if (g->bullets[i].active && g->bullets[i].weapon == slot) g->bullets[i].active = 0;
    g->weapons[slot].active = 0;
}

int vs_weapon_level_up(vs_game *g, int slot) {
    vs_weapon *w = &g->weapons[slot];
    vs_weapons_rec r;
    if (weapon_record(g, w->type, w->level, &r) != 0) return 0;
    if (w->is_power_up) {
        w->b_power = w->b_area = w->b_speed = w->b_cooldown = w->b_amount = w->b_maxHp = w->b_moveSpeed = w->b_growth = w->b_magnet = w->b_luck = w->b_armor = w->b_greed = w->b_regen = w->b_duration = 0;
    }
    apply_entry(w, &r, 0);
    reset_firing_timer(g, slot);
    w->level++;
    if (w->is_power_up) passive_apply(g, w);
    else vs_weapon_fire(g, slot);
    if (VS_HAS(&r, VS_WEAPONS_F_ADDEVOLVEDWEAPON)) vs_levelup_add_late_weapon(g, r.addEvolvedWeapon);
    return 1;
}

/* ---- bullets ---- */

static int bullets_alive(const vs_game *g, int slot) {
    int n = 0;
    for (int i = 0; i < VS_MAX_BULLETS; i++) if (g->bullets[i].active && g->bullets[i].weapon == slot) n++;
    return n;
}

static vs_bullet *bullet_alloc(vs_game *g, int slot, int index) {
    vs_weapon *w = &g->weapons[slot];
    if (bullets_alive(g, slot) >= w->poolLimit) return NULL;
    for (int i = 0; i < VS_MAX_BULLETS; i++) {
        vs_bullet *b = &g->bullets[i];
        if (b->active) continue;
        memset(b, 0, sizeof *b);
        b->active = 1; b->weapon = slot; b->type = w->type; b->index = index;
        b->penetrating = w->penetrating; b->scale = 1; b->alpha = 255;
        return b;
    }
    return NULL;
}

static float true_speed(const vs_game *g, const vs_weapon *w, float bullet_speed) {
    return p_speed(g, w) * g->projectile_speed * bullet_speed;
}

static void aim_for_nearest_enemy(vs_game *g, vs_bullet *b, float speed) {
    vs_player *p = &g->player;
    vs_enemy *e = vs_enemy_closest_to_player(g);
    float dx, dy;
    if (e) { dx = (e->x + e->body_ox - e->body_r) - p->x; dy = (e->y + e->body_oy - e->body_r) - p->y; } /* body.position = top-left of the body */
    else { dx = p->last_dir_x; dy = p->last_dir_y; if (dx == 0 && dy == 0) dx = 1; }
    float len = sqrtf(dx * dx + dy * dy);
    if (len == 0) { dx = 1; len = 1; }
    b->vx = speed * dx / len; b->vy = speed * dy / len;
    b->angle = atan2f(b->vy, b->vx) * 180.0f / PI;
}

void vs_weapon_fire_one(vs_game *g, int slot, int index) {
    vs_weapon *w = &g->weapons[slot];
    vs_player *p = &g->player;
    if (!w->active) return;
    vs_bullet *b = bullet_alloc(g, slot, index);
    if (!b) return;
    float area = vs_weapon_p_area(g, w);
    switch (w->type) {
    case VS_WEAPONS_WHIP: {
        int side_left = p->facing_left ? (index % 2 != 1) : (index % 2 == 1);
        b->x = p->x + (side_left ? -64.0f : 64.0f);
        b->y = p->y - 16.0f - 16.0f * index;
        b->flip_y = index % 2 == 1;
        b->flip_x = side_left;
        b->scale = area;
        b->body_w = 147.0f * area; b->body_h = 22.0f * area;
        break;
    }
    case VS_WEAPONS_MAGIC_MISSILE:
    default: {
        b->x = p->x + (vs_rng_f32(&g->rng) - 0.5f) * index * 10.0f;
        b->y = p->y + (vs_rng_f32(&g->rng) - 0.5f) * index * 10.0f;
        b->body_r = 8.0f * area;
        b->scale = area;
        aim_for_nearest_enemy(g, b, true_speed(g, w, 1.0f));
        break;
    }
    }
}

void vs_weapon_fire(vs_game *g, int slot) {
    vs_weapon *w = &g->weapons[slot];
    if (!w->active || w->is_power_up) return;
    vs_weapon_fire_one(g, slot, 0);
    int amount = vs_weapon_p_amount(g, w);
    for (int i = 1; i < amount; i++) {
        if (w->repeatInterval * i > 0) vs_timer_add(g, VS_TIMER_WEAPON_REPEAT, w->repeatInterval * i, 0, slot, i);
        else vs_weapon_fire_one(g, slot, i);
    }
}

void vs_weapons_update(vs_game *g) {
    for (int i = 0; i < VS_MAX_WEAPONS; i++) if (g->weapons[i].active) g->weapons[i].stats_lifetime += VS_TICK_MS;
}

static int already_hit(vs_bullet *b, uint32_t uid) {
    for (int i = 0; i < b->hit_count; i++) if (b->hit[i] == uid) return 1;
    if (b->hit_count < VS_BULLET_HITS) b->hit[b->hit_count++] = uid;
    return 0;
}

static int overlaps_enemy(const vs_bullet *b, const vs_enemy *e) {
    float ex = e->x + e->body_ox, ey = e->y + e->body_oy, r = e->body_r;
    if (b->body_r > 0) {
        float dx = ex - b->x, dy = ey - b->y, rr = r + b->body_r;
        return dx * dx + dy * dy < rr * rr;
    }
    /* circle vs axis-aligned rectangle centred on the bullet */
    float hx = 0.5f * b->body_w, hy = 0.5f * b->body_h;
    float cx = ex < b->x - hx ? b->x - hx : ex > b->x + hx ? b->x + hx : ex;
    float cy = ey < b->y - hy ? b->y - hy : ey > b->y + hy ? b->y + hy : ey;
    float dx = ex - cx, dy = ey - cy;
    return dx * dx + dy * dy < r * r;
}

static void on_hit(vs_game *g, vs_bullet *b, vs_enemy *e) {
    vs_weapon *w = &g->weapons[b->weapon];
    float dmg = vs_weapon_p_power(g, w);
    vs_enemy_get_damaged(g, e, dmg, w->hitVFX, w->knockback);
    w->stats_damage += dmg;
    switch (b->type) {
    case VS_WEAPONS_MAGIC_MISSILE:
        if (!e->is_dead || 1) { /* OnHasHitAnObject runs before the damage kills; alive check uses pre-hit state */ }
        break;
    default: break;
    }
}

void vs_bullets_update(vs_game *g) {
    const float dt = VS_TICK_MS / 1000.0f;
    for (int i = 0; i < VS_MAX_BULLETS; i++) {
        vs_bullet *b = &g->bullets[i];
        if (!b->active) continue;
        b->age_ms += VS_TICK_MS;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        switch (b->type) {
        case VS_WEAPONS_WHIP:
            /* alpha 1 for 100 ms, then fades to 0 over 100 ms and despawns */
            if (b->age_ms >= 200) { b->active = 0; continue; }
            b->alpha = b->age_ms < 100 ? 255 : (uint8_t)(255 * (1.0f - (b->age_ms - 100) / 100.0f));
            break;
        default: break;
        }
        if (b->body_r > 0 && g->weapons[b->weapon].hitsWalls && vs_tilemap_solid_at(g, b->x, b->y, 1)) { b->active = 0; continue; }
        /* overlaps (physics step) */
        for (int k = 0; k < g->enemy_count && b->active; k++) {
            vs_enemy *e = &g->enemies[k];
            if (!e->active || e->is_dead) continue;
            if (!overlaps_enemy(b, e)) continue;
            if (already_hit(b, e->uid)) continue;
            int was_alive = !e->is_dead;
            on_hit(g, b, e);
            if (b->type == VS_WEAPONS_MAGIC_MISSILE && was_alive) {
                b->penetrating -= 1;
                if (b->penetrating <= 0) b->active = 0;
            }
        }
    }
}

/* Bullets outside 1.2·W × 1.3·H around the player are despawned every frame. */
void vs_bullets_cull(vs_game *g) {
    float hw = 0.6f * g->logical_w, hh = 0.65f * g->logical_h;
    for (int i = 0; i < VS_MAX_BULLETS; i++) {
        vs_bullet *b = &g->bullets[i];
        if (!b->active) continue;
        if (b->x < g->player.x - hw || b->x >= g->player.x + hw || b->y < g->player.y - hh || b->y >= g->player.y + hh) b->active = 0;
    }
}
