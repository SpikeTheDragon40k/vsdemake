#include <math.h>
#include <string.h>

#include "core/vs_enums.h"
#include "game/game.h"

/* notes/behaviour/weapons.md. Weapon objects accumulate table deltas; bullets
 * are per-weapon-type behaviours. */

#define PI 3.14159265358979f
#define DEG(x) ((x) * PI / 180.0f)

static int weapon_record(vs_game *g, int type, int level, vs_weapons_rec *out) {
    int gi = vs_table_find_group_enum(&g->weapons_table, type);
    if (gi < 0) return -1;
    vs_group grp;
    vs_table_group(&g->weapons_table, (uint32_t)gi, &grp);
    if ((uint32_t)level >= grp.count) return -1;
    vs_table_record(&g->weapons_table, grp.first + (uint32_t)level, out);
    return 0;
}

int vs_weapon_record(const vs_game *g, int type, int level, vs_weapons_rec *out) { return weapon_record((vs_game *)g, type, level, out); }

const char *vs_weapon_name(const vs_game *g, int type) {
    vs_weapons_rec r;
    if (weapon_record((vs_game *)g, type, 0, &r) != 0 || !VS_HAS(&r, VS_WEAPONS_F_NAME)) return "?";
    return vs_table_str(&g->weapons_table, r.name);
}

static void apply_entry(vs_weapon *w, const vs_weapons_rec *r, int set) {
#define F(field, name) if (VS_HAS(r, VS_WEAPONS_F_##name)) { if (set) w->field = r->field; else w->field += r->field; }
    F(interval, INTERVAL) F(repeatInterval, REPEATINTERVAL) F(power, POWER) F(area, AREA) F(speed, SPEED) F(amount, AMOUNT)
    F(duration, DURATION) F(critChance, CRITCHANCE) F(critMul, CRITMUL) F(hitBoxDelay, HITBOXDELAY) F(knockback, KNOCKBACK)
    F(chance, CHANCE) F(shieldInvulTime, SHIELDINVULTIME) F(volume, VOLUME) F(penetrating, PENETRATING) F(charges, CHARGES)
#undef F
    if (VS_HAS(r, VS_WEAPONS_F_HITVFX)) w->hitVFX = r->hitVFX;
    if (VS_HAS(r, VS_WEAPONS_F_HITSWALLS)) w->hitsWalls = r->hitsWalls;
    if (VS_HAS(r, VS_WEAPONS_F_INTERVALDEPENDSONDURATION)) w->intervalDependsOnDuration = r->intervalDependsOnDuration;
    if (VS_HAS(r, VS_WEAPONS_F_ISPOWERUP)) w->is_power_up = r->isPowerUp;
    if (VS_HAS(r, VS_WEAPONS_F_POOLLIMIT)) w->poolLimit = r->poolLimit;
#define B(field, name) if (VS_HAS(r, VS_WEAPONS_F_##name)) { if (set) w->b_##field = r->field; else w->b_##field += r->field; }
    B(power, POWER) B(area, AREA) B(speed, SPEED) B(cooldown, COOLDOWN) B(amount, AMOUNT) B(maxHp, MAXHP) B(moveSpeed, MOVESPEED)
    B(growth, GROWTH) B(magnet, MAGNET) B(luck, LUCK) B(armor, ARMOR) B(greed, GREED) B(regen, REGEN) B(duration, DURATION)
#undef B
}

float vs_weapon_p_area(const vs_game *g, const vs_weapon *w) {
    float k = w->type == VS_WEAPONS_AXE ? 1.3f : w->type == VS_WEAPONS_SCYTHE ? 1.2f : 1.0f;
    return k * g->player.area * w->area;
}
int vs_weapon_p_amount(const vs_game *g, const vs_weapon *w) {
    return (w->type == VS_WEAPONS_GARLIC || w->type == VS_WEAPONS_VORTEX) ? 1 : (int)(g->player.amount + w->amount);
}
float vs_weapon_p_power(const vs_game *g, const vs_weapon *w) { return g->player.power * w->power + w->recovered_bonus; }

int vs_weapon_implemented(int type) {
    switch (type) {
    case VS_WEAPONS_WHIP: case VS_WEAPONS_VAMPIRICA: case VS_WEAPONS_MAGIC_MISSILE: case VS_WEAPONS_HOLY_MISSILE:
    case VS_WEAPONS_KNIFE: case VS_WEAPONS_THOUSAND: case VS_WEAPONS_AXE: case VS_WEAPONS_SCYTHE:
    case VS_WEAPONS_CROSS: case VS_WEAPONS_HEAVENSWORD: case VS_WEAPONS_HOLYBOOK: case VS_WEAPONS_VESPERS:
    case VS_WEAPONS_FIREBALL: case VS_WEAPONS_HELLFIRE: case VS_WEAPONS_GARLIC: case VS_WEAPONS_VORTEX:
    case VS_WEAPONS_HOLYWATER: case VS_WEAPONS_DIAMOND: case VS_WEAPONS_LIGHTNING:
        return 1;
    default: return 0;
    }
}
static float p_speed(const vs_game *g, const vs_weapon *w) { return g->player.speed * w->speed; }
static float p_duration(const vs_game *g, const vs_weapon *w) { return g->player.duration * w->duration; }
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

static void shuffle_radii(vs_game *g, vs_weapon *w) {
    for (int i = 11; i > 0; i--) { int j = (int)vs_rng_below(&g->rng, (uint32_t)i + 1); float t = w->hw_radii[i]; w->hw_radii[i] = w->hw_radii[j]; w->hw_radii[j] = t; }
}

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
    if (type == VS_WEAPONS_HOLYWATER) {
        float m = g->logical_w < g->logical_h ? (float)g->logical_w : (float)g->logical_h;
        for (int i = 0; i < 12; i++) w->hw_radii[i] = 0.25f * m + 0.1f * m * (i / 12.0f);
        shuffle_radii(g, w);
    }
    if (VS_HAS(&r, VS_WEAPONS_F_EVOLVESFROM)) {
        const uint8_t *items;
        uint32_t n = vs_table_list(&g->weapons_table, r.evolvesFrom, &items);
        for (uint32_t i = 0; i < n; i++) vs_weapon_remove(g, vs_list_i32(items, i));
    }
    if (type == VS_WEAPONS_VORTEX) g->recovered_hp = 0;
    w->level = 1;
    if (w->is_power_up) passive_apply(g, w);
    else reset_firing_timer(g, slot);
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
    if (w->type == VS_WEAPONS_HOLYWATER) shuffle_radii(g, w);
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
        b->penetrating = w->penetrating; b->scale = 1; b->alpha = 255; b->cullable = 1; b->visible = 1;
        b->x = g->player.x; b->y = g->player.y;
        return b;
    }
    return NULL;
}

static float true_speed(const vs_game *g, const vs_weapon *w, float bullet_speed) {
    return p_speed(g, w) * g->projectile_speed * bullet_speed;
}

static vs_enemy *enemy_by_uid(vs_game *g, int uid) {
    for (int i = 0; i < g->enemy_count; i++) if (g->enemies[i].active && (int)g->enemies[i].uid == uid) return &g->enemies[i];
    return NULL;
}

static void set_velocity_angle(vs_bullet *b, float angle_rad, float speed) {
    b->vx = cosf(angle_rad) * speed; b->vy = sinf(angle_rad) * speed;
    b->angle = angle_rad * 180.0f / PI;
}

/* body.position is the body's top-left in Arcade; the aim helpers use it. */
static void aim_at_enemy(vs_game *g, vs_bullet *b, const vs_enemy *e, float speed) {
    vs_player *p = &g->player;
    float dx = (e->x + e->body_ox - e->body_r) - p->x, dy = (e->y + e->body_oy - e->body_r) - p->y;
    if (dx == 0 && dy == 0) dx = 1;
    set_velocity_angle(b, atan2f(dy, dx), speed);
}

static void aim_player_facing(vs_game *g, vs_bullet *b, float speed) {
    float dx = g->player.last_dir_x, dy = g->player.last_dir_y;
    if (dx == 0 && dy == 0) dx = 1;
    set_velocity_angle(b, atan2f(dy, dx), speed);
}

static void aim_for_nearest_enemy(vs_game *g, vs_bullet *b, float speed) {
    vs_enemy *e = vs_enemy_closest_to_player(g);
    if (e) aim_at_enemy(g, b, e, speed); else aim_player_facing(g, b, speed);
}

static float angle_from_target(vs_game *g, const vs_enemy *e) {
    vs_player *p = &g->player;
    float px = p->x + p->body_ox - p->body_r, py = p->y + p->body_oy - p->body_r;
    return atan2f(e->y - py, e->x - px);
}

void vs_weapon_fire_one(vs_game *g, int slot, int index) {
    vs_weapon *w = &g->weapons[slot];
    vs_player *p = &g->player;
    if (!w->active) return;
    vs_bullet *b = bullet_alloc(g, slot, index);
    if (!b) return;
    float area = vs_weapon_p_area(g, w);
    int amount = vs_weapon_p_amount(g, w);
    switch (w->type) {
    case VS_WEAPONS_WHIP: case VS_WEAPONS_VAMPIRICA: {
        int side_left = p->facing_left ? (index % 2 != 1) : (index % 2 == 1);
        b->x = p->x + (side_left ? -64.0f : 64.0f);
        b->y = p->y - 16.0f - 16.0f * index;
        b->flip_y = index % 2 == 1; b->flip_x = side_left;
        b->scale = area; b->body_w = 147.0f * area; b->body_h = 22.0f * area;
        break;
    }
    case VS_WEAPONS_MAGIC_MISSILE: case VS_WEAPONS_HOLY_MISSILE:
        b->x += (vs_rng_f32(&g->rng) - 0.5f) * index * 10.0f;
        b->y += (vs_rng_f32(&g->rng) - 0.5f) * index * 10.0f;
        b->body_r = 8.0f * area; b->scale = area;
        aim_for_nearest_enemy(g, b, true_speed(g, w, 1.0f));
        break;
    case VS_WEAPONS_KNIFE: case VS_WEAPONS_THOUSAND:
        b->body_r = 8.0f * area; b->scale = area;
        b->y -= 12;
        b->x += (vs_rng_f32(&g->rng) - 0.5f) * (index == 0 ? 0 : 40) * area;
        b->y += (vs_rng_f32(&g->rng) - 0.5f) * (index == 0 ? 0 : 40) * area;
        aim_player_facing(g, b, true_speed(g, w, 2.0f));
        break;
    case VS_WEAPONS_AXE: {
        b->body_r = 12.0f * area; b->scale = area; b->cullable = 0;
        float dir = p->last_dir_x < 0 ? -1.0f : 1.0f;
        float ang = 45.0f * dir / amount * index - 90.0f;
        set_velocity_angle(b, DEG(ang), true_speed(g, w, 2.0f));
        b->ax = b->vx; b->ay = b->vy > -600 ? b->vy : -600;
        break;
    }
    case VS_WEAPONS_SCYTHE:
        /* Death Spiral: evenly spread over a full circle, straight out */
        b->body_r = 16.0f * area; b->scale = area;
        set_velocity_angle(b, DEG(360.0f / amount * index - 90.0f), true_speed(g, w, 1.0f));
        break;
    case VS_WEAPONS_HELLFIRE: {
        b->body_r = 16.0f * area; b->scale = 0;
        vs_enemy *t = enemy_by_uid(g, w->target_uid);
        if (!t) t = vs_enemy_closest_to_player(g);
        static const float hoffs[] = {0, 10, -10, 20, -20, 30, -30, 40, -40};
        float base = t ? angle_from_target(g, t) : atan2f(p->last_dir_y, (p->last_dir_x == 0 && p->last_dir_y == 0) ? 1 : p->last_dir_x);
        set_velocity_angle(b, base + DEG(hoffs[index % 9]), true_speed(g, w, 1.0f));
        break;
    }
    case VS_WEAPONS_HEAVENSWORD:
        b->body_r = 12.0f * area; b->scale = area; b->cullable = 0;
        b->x += (vs_rng_f32(&g->rng) - 0.5f) * index * 20.0f * area;
        b->y += (vs_rng_f32(&g->rng) - 0.5f) * index * 20.0f * area;
        aim_for_nearest_enemy(g, b, true_speed(g, w, 1.0f));
        b->ax = b->vx; b->ay = b->vy;
        break;
    case VS_WEAPONS_FIREBALL: {
        b->body_r = 12.0f * area; b->scale = 0; b->y -= 16;
        vs_enemy *t = enemy_by_uid(g, w->target_uid);
        if (!t) t = vs_enemy_closest_to_player(g);
        static const float offs[] = {0, 5, -5, 10, -10, 15, -15, 20, -20, 25, -25, 30, -30, 35, -35, 40, -40, 45, -45};
        float base = t ? angle_from_target(g, t) : atan2f(p->last_dir_y, (p->last_dir_x == 0 && p->last_dir_y == 0) ? 1 : p->last_dir_x);
        set_velocity_angle(b, base + DEG(offs[index % 19]), true_speed(g, w, 0.8f));
        break;
    }
    case VS_WEAPONS_CROSS:
        b->body_r = 12.0f * area; b->scale = area;
        b->x += (vs_rng_f32(&g->rng) - 0.5f) * index * 20.0f * area;
        b->y += (vs_rng_f32(&g->rng) - 0.5f) * index * 20.0f * area;
        aim_for_nearest_enemy(g, b, true_speed(g, w, 1.0f));
        b->ax = b->vx; b->ay = b->vy;
        break;
    case VS_WEAPONS_HOLYBOOK: case VS_WEAPONS_VESPERS:
        b->body_r = 0; b->scale = 0; b->cullable = 0;
        b->f0 = 2 * PI / amount * index;
        break;
    case VS_WEAPONS_GARLIC: case VS_WEAPONS_VORTEX:
        b->body_r = 32.0f * area; b->visible = 0; b->cullable = 0;
        break;
    case VS_WEAPONS_LIGHTNING: {
        /* random enemy inside the screen rectangle; strike from above, hit for 60 ms */
        int cand[64]; int n = 0;
        for (int i = 0; i < g->enemy_count && n < 64; i++) {
            vs_enemy *e = &g->enemies[i];
            if (!e->active || e->is_dead) continue;
            if (fabsf(e->x - p->x) < 0.5f * g->logical_w && fabsf(e->y - p->y) < 0.5f * g->logical_h) cand[n++] = i;
        }
        if (n == 0) { b->active = 0; return; }
        vs_enemy *t = &g->enemies[cand[vs_rng_below(&g->rng, (uint32_t)n)]];
        b->bx = t->x; b->by = t->y;
        b->x = t->x; b->y = p->y - 0.6f * g->logical_h; b->ay = b->y;
        b->body_r = 0; b->scale = 0.5f * area; b->cullable = 0; b->state = 0;
        break;
    }
    case VS_WEAPONS_DIAMOND: {
        b->body_r = 8.0f * area; b->scale = area; b->cullable = 0;
        vs_enemy *t = enemy_by_uid(g, w->target_uid);
        if (!t) t = vs_enemy_closest_to_player(g);
        static const float offs[] = {0, 10, -10, 20, -20, 30, -30, 40, -40};
        float base = t ? angle_from_target(g, t) : atan2f(p->last_dir_y, (p->last_dir_x == 0 && p->last_dir_y == 0) ? 1 : p->last_dir_x);
        set_velocity_angle(b, base + DEG(offs[index % 9]), true_speed(g, w, 1.1f));
        break;
    }
    case VS_WEAPONS_HOLYWATER: {
        b->body_r = 0; b->scale = 1; b->cullable = 0; b->state = 0;
        b->x = p->x + (vs_rng_f32(&g->rng) - 0.5f) * g->logical_w * 0.5f;
        b->y = p->y - 0.6f * g->logical_h;
        w->hw_angle_i = (w->hw_angle_i + 1) % 12;
        w->hw_radius_i = (w->hw_radius_i + 1) % 12;
        float ang = 2 * PI * w->hw_angle_i / 12.0f, rad = w->hw_radii[w->hw_radius_i];
        float tx = p->x + cosf(ang) * rad, ty = p->y + sinf(ang) * rad;
        if (amount < 4 && index == 0) { vs_enemy *e = vs_enemy_closest_to_player(g); if (e) { tx = e->x; ty = e->y; } }
        b->ax = b->x; b->ay = b->y; b->bx = tx; b->by = ty;
        break;
    }
    default:
        b->body_r = 8.0f * area; b->scale = area;
        aim_for_nearest_enemy(g, b, true_speed(g, w, 1.0f));
        break;
    }
}

void vs_weapon_fire(vs_game *g, int slot) {
    vs_weapon *w = &g->weapons[slot];
    if (!w->active || w->is_power_up) return;
    if (w->type == VS_WEAPONS_VORTEX) w->recovered_bonus = g->recovered_hp / 600.0f < 6 ? g->recovered_hp / 600.0f : 6;
    if (w->type == VS_WEAPONS_FIREBALL || w->type == VS_WEAPONS_DIAMOND || w->type == VS_WEAPONS_HELLFIRE) {
        int n = 0;
        for (int i = 0; i < g->enemy_count; i++) if (g->enemies[i].active && !g->enemies[i].pass_through_walls) n++;
        w->target_uid = -1;
        if (n) {
            int k = (int)vs_rng_below(&g->rng, (uint32_t)n);
            for (int i = 0; i < g->enemy_count; i++) if (g->enemies[i].active && !g->enemies[i].pass_through_walls && k-- == 0) { w->target_uid = (int)g->enemies[i].uid; break; }
        }
    }
    vs_weapon_fire_one(g, slot, 0);
    int amount = vs_weapon_p_amount(g, w);
    for (int i = 1; i < amount; i++) {
        if (w->repeatInterval * i > 0) vs_timer_add(g, VS_TIMER_WEAPON_REPEAT, w->repeatInterval * i, 0, slot, i);
        else vs_weapon_fire_one(g, slot, i);
    }
}

void vs_weapons_update(vs_game *g) {
    for (int i = 0; i < VS_MAX_WEAPONS; i++) {
        vs_weapon *w = &g->weapons[i];
        if (!w->active) continue;
        w->stats_lifetime += VS_TICK_MS;
        if (w->heal_cd_ms > 0) w->heal_cd_ms -= VS_TICK_MS;
    }
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
    if (b->body_w <= 0) return 0;
    float hx = 0.5f * b->body_w, hy = 0.5f * b->body_h;
    float cx = ex < b->x - hx ? b->x - hx : ex > b->x + hx ? b->x + hx : ex;
    float cy = ey < b->y - hy ? b->y - hy : ey > b->y + hy ? b->y + hy : ey;
    float dx = ex - cx, dy = ey - cy;
    return dx * dx + dy * dy < r * r;
}

static void on_hit(vs_game *g, vs_bullet *b, vs_enemy *e, float crit) {
    vs_weapon *w = &g->weapons[b->weapon];
    int was_alive = !e->is_dead;
    if ((b->type == VS_WEAPONS_GARLIC || b->type == VS_WEAPONS_VORTEX) && was_alive && e->res_Debuffs < 1) {
        if (e->knockback < e->maxKnockback) e->knockback += 0.3f;
        if (e->res_Freeze > 0) e->res_Freeze -= 0.1f;
    }
    float dmg = vs_weapon_p_power(g, w) * crit;
    vs_enemy_get_damaged(g, e, dmg, w->hitVFX, w->knockback);
    w->stats_damage += dmg;
    /* Soul Eater: a kill may drop a Little Heart that flies to the player */
    if (b->type == VS_WEAPONS_VORTEX && was_alive && e->is_dead) {
        float r = vs_rng_f32(&g->rng);
        if (vs_weapon_p_power(g, w) / 7.0f * 0.15f > r) {
            float o = 10.0f * (r - 0.5f);
            vs_pickup *h = vs_pickup_make(g, e->x + o, e->y - o, VS_PICKUPS_LITTLEHEART);
            if (h) { h->go_to_player = 1; h->t = 1; }
        }
    }
    switch (b->type) {
    case VS_WEAPONS_MAGIC_MISSILE: case VS_WEAPONS_HOLY_MISSILE: case VS_WEAPONS_KNIFE: case VS_WEAPONS_THOUSAND:
    case VS_WEAPONS_AXE: case VS_WEAPONS_FIREBALL:
        if (was_alive) { b->penetrating -= 1; if (b->penetrating <= 0) b->active = 0; }
        break;
    default: break;
    }
}

static void bullet_motion(vs_game *g, vs_bullet *b) {
    vs_weapon *w = &g->weapons[b->weapon];
    vs_player *p = &g->player;
    const float dt = VS_TICK_MS / 1000.0f;
    float area = vs_weapon_p_area(g, w);
    switch (b->type) {
    case VS_WEAPONS_WHIP:
        if (b->age_ms >= 200) { b->active = 0; return; }
        b->alpha = b->age_ms < 100 ? 255 : (uint8_t)(255 * (1.0f - (b->age_ms - 100) / 100.0f));
        break;
    case VS_WEAPONS_VAMPIRICA:   /* fades over 200 ms instead of 100 */
        if (b->age_ms >= 300) { b->active = 0; return; }
        b->alpha = b->age_ms < 100 ? 255 : (uint8_t)(255 * (1.0f - (b->age_ms - 100) / 200.0f));
        break;
    case VS_WEAPONS_SCYTHE:
        b->angle += 720.0f * dt;
        break;
    case VS_WEAPONS_HELLFIRE:
        b->scale = area * (b->age_ms < 200 ? b->age_ms / 200.0f : 1);
        b->angle = atan2f(b->vy, b->vx) * 180.0f / PI;
        break;
    case VS_WEAPONS_HEAVENSWORD: {
        /* accel 2 -> 0 over 500 ms, then turn back (0 -> -2 over 500 ms),
         * spinning, with a fresh hit list; cullable 500 ms after turning */
        float accel;
        if (b->age_ms < 500) accel = 2.0f - 2.0f * b->age_ms / 500.0f;
        else {
            if (b->state == 0) { b->state = 1; b->hit_count = 0; }
            accel = b->age_ms < 1000 ? -2.0f * (b->age_ms - 500) / 500.0f : -2.0f;
            b->angle += 360.0f * dt;
            if (b->age_ms >= 1000) b->cullable = 1;
        }
        b->vx = b->ax * accel; b->vy = b->ay * accel;
        break;
    }
    case VS_WEAPONS_AXE:
        b->ay += VS_TICK_MS * 0.625f;
        b->vx = b->ax; b->vy = b->ay;
        b->angle += 360.0f * dt;
        if (b->age_ms > 4000) { b->scale = area * (1 - (b->age_ms - 4000) / 500.0f); if (b->age_ms >= 4500) { b->active = 0; return; } }
        break;
    case VS_WEAPONS_FIREBALL:
        b->scale = area * (b->age_ms < 200 ? b->age_ms / 200.0f : 1);
        break;
    case VS_WEAPONS_CROSS: {
        float t = b->age_ms < 1000 ? b->age_ms / 1000.0f : 1;
        float a0 = 1.5f + 0.1f * b->index;
        float accel = a0 + (-2 - a0) * t;
        b->vx = b->ax * accel; b->vy = b->ay * accel;
        b->angle += 720.0f * dt;
        break;
    }
    case VS_WEAPONS_HOLYBOOK: case VS_WEAPONS_VESPERS: {
        float dur = p_duration(g, w);
        b->f0 += 0.035f * p_speed(g, w) * VS_TICK_MS / 16.0f;
        b->x = p->x + cosf(b->f0) * area * 50; b->y = p->y + sinf(b->f0) * area * 50;
        b->vx = b->vy = 0;
        float full = 1 + 0.2f * area;
        if (b->age_ms < 500) b->scale = full * b->age_ms / 500.0f;
        else if (b->age_ms < dur) b->scale = full;
        else { b->scale = full * (1 - (b->age_ms - dur) / 500.0f); if (b->age_ms >= dur + 500) { b->active = 0; return; } }
        b->body_r = 8.0f * b->scale;
        b->hit_reset_ms += VS_TICK_MS;
        if (b->hit_reset_ms >= w->hitBoxDelay) { b->hit_reset_ms -= w->hitBoxDelay; b->hit_count = 0; }
        break;
    }
    case VS_WEAPONS_GARLIC: case VS_WEAPONS_VORTEX:
        b->x = p->x; b->y = p->y;
        if (b->age_ms >= vs_weapon_p_interval(g, w)) { b->active = 0; return; }
        break;
    case VS_WEAPONS_LIGHTNING:
        if (b->state == 0) {
            float t = b->age_ms / 70.0f;
            if (t >= 1) { t = 1; b->state = 1; b->f1 = b->age_ms; b->body_r = 16.0f * b->scale; b->hit_count = 0; }
            b->y = b->ay + (b->by - b->ay) * t;
        } else if (b->age_ms - b->f1 >= 60) { b->active = 0; return; }
        break;
    case VS_WEAPONS_DIAMOND: {
        /* bounces on the screen edges (WorldBoxCollider), clearing its hit list */
        float hw = 0.5f * g->logical_w, hh = 0.5f * g->logical_h;
        if ((b->x < p->x - hw && b->vx < 0) || (b->x > p->x + hw && b->vx > 0)) { b->vx = -b->vx; b->hit_count = 0; }
        if ((b->y < p->y - hh && b->vy < 0) || (b->y > p->y + hh && b->vy > 0)) { b->vy = -b->vy; b->hit_count = 0; }
        b->angle = atan2f(b->vy, b->vx) * 180.0f / PI;
        float dur = p_duration(g, w);
        if (b->age_ms > dur) { b->alpha = (uint8_t)(255 * (1 - (b->age_ms - dur) / 100.0f)); if (b->age_ms >= dur + 100) { b->active = 0; return; } }
        break;
    }
    case VS_WEAPONS_HOLYWATER:
        if (b->state == 0) {
            float t = b->age_ms / 750.0f;
            if (t >= 1) { t = 1; b->state = 1; b->hit_count = 0; b->hit_reset_ms = 0; b->f1 = b->age_ms; b->scale = area; b->body_r = 16.0f * area; }
            b->x = b->ax + (b->bx - b->ax) * t; b->y = b->ay + (b->by - b->ay) * t;
            b->angle += 600.0f * dt;
        } else {
            b->vx = b->vy = 0;
            b->hit_reset_ms += VS_TICK_MS;
            if (b->hit_reset_ms >= w->hitBoxDelay) { b->hit_reset_ms -= w->hitBoxDelay; b->hit_count = 0; }
            if (b->age_ms - b->f1 >= p_duration(g, w)) { b->active = 0; return; }
        }
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
        bullet_motion(g, b);
        if (!b->active) continue;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        if (b->body_r <= 0 && b->body_w <= 0) continue;
        int wall_sensitive = g->weapons[b->weapon].hitsWalls && b->type != VS_WEAPONS_HOLYBOOK && b->type != VS_WEAPONS_VESPERS && b->type != VS_WEAPONS_GARLIC && b->type != VS_WEAPONS_VORTEX && b->type != VS_WEAPONS_HOLYWATER;
        if (b->body_r > 0 && wall_sensitive && vs_tilemap_solid_at(g, b->x, b->y, 1)) { b->active = 0; continue; }
        for (int k = 0; k < VS_MAX_DESTRUCTIBLES && b->active; k++) {
            vs_destructible *d = &g->destructibles[k];
            if (!d->active || d->is_dead) continue;
            /* body: circle r=16 at the sprite's top-left (origin 0.5, 1) */
            vs_enemy tmp = {0};
            tmp.x = d->x; tmp.y = d->y; tmp.body_r = 16; tmp.body_ox = -0.5f * d->src_w + 16; tmp.body_oy = -d->src_h + 16;
            if (!overlaps_enemy(b, &tmp)) continue;
            uint32_t duid = 0x80000000u | (uint32_t)k;
            if (already_hit(b, duid)) continue;
            vs_destructible_damage(g, d, vs_weapon_p_power(g, &g->weapons[b->weapon]));
        }
        for (int k = 0; k < g->enemy_count && b->active; k++) {
            vs_enemy *e = &g->enemies[k];
            if (!e->active || e->is_dead) continue;
            if (!overlaps_enemy(b, e)) continue;
            /* Bloody Tear / Heaven Sword roll a crit on every overlap, as the
             * original's onBulletOverlapsEnemy; a Bloody Tear crit heals 8 HP
             * (once per second) even when the hit itself is already spent. */
            float crit = 1;
            if (b->type == VS_WEAPONS_VAMPIRICA || b->type == VS_WEAPONS_HEAVENSWORD) {
                vs_weapon *w = &g->weapons[b->weapon];
                if (vs_rng_f32(&g->rng) < w->critChance * g->player.luck) crit = w->critMul;
                if (b->type == VS_WEAPONS_VAMPIRICA && crit > 1 && w->heal_cd_ms <= 0) {
                    w->heal_cd_ms = 1000;
                    vs_player_recover_hp(g, 8);
                    vs_fx_damage_number(g, g->player.x, g->player.y, -8);
                }
            }
            if (already_hit(b, e->uid)) continue;
            on_hit(g, b, e, crit);
        }
    }
}

void vs_bullets_cull(vs_game *g) {
    float hw = 0.6f * g->logical_w, hh = 0.65f * g->logical_h;
    for (int i = 0; i < VS_MAX_BULLETS; i++) {
        vs_bullet *b = &g->bullets[i];
        if (!b->active || !b->cullable) continue;
        if (b->x < g->player.x - hw || b->x >= g->player.x + hw || b->y < g->player.y - hh || b->y >= g->player.y + hh) b->active = 0;
    }
}
