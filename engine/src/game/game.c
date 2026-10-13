#include "game/game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/vs_enums.h"
#include "core/vs_text.h"

static float table_constant(const vs_pack *pack, const char *name, float dflt) {
    vs_table t;
    if (vs_pack_table(pack, "constants", &t) != 0) return dflt;
    int gi = vs_table_find_group(&t, name);
    if (gi < 0) return dflt;
    vs_group grp;
    vs_table_group(&t, (uint32_t)gi, &grp);
    vs_constants_rec r;
    vs_table_record(&t, grp.first, &r);
    return r.value;
}

static int load_texture(vs_game *g, const char *name) {
    int entry = vs_pack_find(g->pack, "TEX", name);
    vs_tex_info ti;
    if (entry < 0 || vs_pack_tex(g->pack, entry, &ti) != 0 || g->texture_count >= VS_MAX_TEXTURES) return -1;
    vs_texture *t = vs_plat_texture_create(ti.w, ti.h, ti.pixels);
    if (!t) return -1;
    g->textures[g->texture_count] = t;
    return g->texture_count++;
}

static int load_atlas(vs_game *g, const char *name, vs_atlas *out, int *tex) {
    int idx = vs_pack_find(g->pack, "ATL", name);
    if (idx < 0 || vs_pack_atlas(g->pack, idx, out) != 0) return -1;
    *tex = load_texture(g, vs_pack_entry_name(g->pack, out->tex_entry));
    return *tex < 0 ? -1 : 0;
}

static int open_table(vs_game *g, const char *name, vs_table *t, const char *fields) {
    if (vs_pack_table(g->pack, name, t) != 0 || vs_table_check_fields(t, fields) != 0) { vs_plat_log("table %s missing or layout mismatch", name); return -1; }
    return 0;
}

int vs_game_init(vs_game *g, const vs_pack *pack, int lw, int lh, uint64_t seed) {
    int auto_pick = g->auto_pick;
    memset(g, 0, sizeof *g);
    g->auto_pick = auto_pick;
    g->pack = pack;
    g->logical_w = lw;
    g->logical_h = lh;
    vs_rng_seed(&g->rng, seed);
    g->next_uid = 1;

    g->base_player_px_speed = table_constant(pack, "_basePlayerPxSpeed", 82.5f);
    g->base_enemy_speed = table_constant(pack, "_baseEnemySpeed", 0.231f);
    g->base_projectile_speed = table_constant(pack, "_baseProjectileSpeed", 165.0f);
    g->player_px_speed = g->base_player_px_speed;
    g->enemy_speed = g->base_enemy_speed;
    g->projectile_speed = g->base_projectile_speed;
    g->gold_multiplier = g->enemy_health_multiplier = 1;

    if (open_table(g, "characters", &g->characters_table, VS_CHARACTERS_FIELD_NAMES)) return -1;
    if (open_table(g, "enemies", &g->enemies_table, VS_ENEMIES_FIELD_NAMES)) return -1;
    if (open_table(g, "hitvfx", &g->hitvfx_table, VS_HITVFX_FIELD_NAMES)) return -1;
    if (open_table(g, "weapons", &g->weapons_table, VS_WEAPONS_FIELD_NAMES)) return -1;
    if (open_table(g, "pickups", &g->pickups_table, VS_PICKUPS_FIELD_NAMES)) return -1;

    if (load_atlas(g, "characters", &g->atl_characters, &g->tex_characters) != 0) return -1;
    if (load_atlas(g, "enemies", &g->atl_enemies, &g->tex_enemies) != 0) return -1;
    if (load_atlas(g, "items", &g->atl_items, &g->tex_items) != 0) return -1;
    if (load_atlas(g, "vfx", &g->atl_vfx, &g->tex_vfx) != 0) return -1;
    if (vs_text_init() != 0) return -1;

    /* InitGame order (Core, line 6381): player + magnet, ticker, MakeLevelOne,
     * LevelUpFactory.Init, stage (+ starting spawns), AddWeapon(starting). */
    g->magnet.radius = 30; g->magnet.percentage = 1;
    if (vs_player_init(g, VS_CHARACTERS_ANTONIO) != 0) return -1;
    if (g->player.magnet_bonus > 0) { g->magnet.radius += g->magnet.radius * g->player.magnet_bonus; g->magnet.percentage += g->player.magnet_bonus; }
    vs_timer_add(g, VS_TIMER_TICKER, 1000, -1, 0, 0);
    vs_timer_add(g, VS_TIMER_REGEN, 1000, -1, 0, 0);
    vs_levelup_init(g);
    if (vs_stage_init(g, VS_STAGES_FOREST) != 0) { vs_plat_log("stage init failed"); return -1; }
    vs_weapon_add(g, g->player.startingWeapon, 1);
    {
        vs_stage *s = &g->stage;
        vs_stages_rec *row0 = &s->rows[0].rec;
        const char *bg = VS_HAS(row0, VS_STAGES_F_BGTEXTURENAME) ? vs_table_str(&s->table, row0->BGTextureName) : "bg_forest";
        g->tex_bg = load_texture(g, bg);
    }
    return g->tex_bg < 0 ? -1 : 0;
}

void vs_game_update(vs_game *g, const vs_pad *pad) {
    if (g->game_over) return;
    if (g->levelup.pending) {
        /* scene paused: only the level-up UI runs */
        vs_levelup_input(g, pad);
        g->prev_buttons = pad->buttons;
        return;
    }
    g->prev_buttons = pad->buttons;

    /* notes/behaviour/loop.md */
    vs_player_update(g, pad);
    vs_enemies_cull_step(g);
    vs_bullets_cull(g);

    /* Core.Update */
    g->update_tick++;
    int slow = 0;
    if (g->update_tick % 4 == 0) { g->update_tick = 0; slow = 1; }
    if (slow) {
        if (g->player.xp >= g->levelup.current_xp_factor) {
            if (vs_player_level_up(g)) vs_weapons_reset_timers(g);
            vs_levelup_calculate_xp_factor(g);
            vs_levelup_open(g);
        }
        vs_pickups_update(g);
    }
    vs_bullets_update(g);
    vs_weapons_update(g);
    vs_enemies_update(g);
    vs_pickups_physics(g);
    vs_timers_step(g);
    g->tick++;
}

static uint32_t lerp_color(uint32_t a, uint32_t b, float f) {
    uint32_t out = 0;
    for (int s = 0; s < 24; s += 8) {
        float ca = (float)((a >> s) & 255), cb = (float)((b >> s) & 255);
        out |= ((uint32_t)(ca + (cb - ca) * f) & 255) << s;
    }
    return out;
}

static uint32_t day_night_tint(const vs_game *g) {
    if (!g->stage.day_night) return 0xffffff;
    const int cycle = 900;
    int t = g->survived_seconds % cycle;
    int even = (g->survived_seconds / cycle) % 2 == 0;
    float f = even ? (float)t / cycle : (float)(cycle - t) / cycle;
    return lerp_color(0xffffff, 0x0444ee, f);
}

static void render_levelup(vs_game *g) {
    vs_levelup *lu = &g->levelup;
    float x = g->logical_w * 0.5f - 150, y = 60;
    vs_text_draw(x, y, 2, 0xffffff, "LEVEL UP");
    char line[96];
    for (int i = 0; i < lu->option_count; i++) {
        if (lu->items_mode) snprintf(line, sizeof line, "%s %s", i == lu->selected ? ">" : " ", lu->item_options[i] == VS_PICKUPS_ROAST ? "Floor Chicken" : "Big Coin Bag");
        else {
            int slot = vs_weapon_find(g, lu->options[i]);
            snprintf(line, sizeof line, "%s %-24s %s %d", i == lu->selected ? ">" : " ", vs_weapon_name(g, lu->options[i]), slot >= 0 ? "level" : "new", slot >= 0 ? g->weapons[slot].level + 1 : 1);
        }
        vs_text_draw(x, y + 30 + 14 * i, 1.5f, i == lu->selected ? 0xffff80 : 0xffffff, line);
    }
}

static void render_hud(vs_game *g) {
    char buf[128];
    vs_player *p = &g->player;
    float xp0 = g->levelup.previous_xp_factor, xp1 = g->levelup.current_xp_factor;
    float frac = xp1 > xp0 ? (p->xp - xp0) / (xp1 - xp0) : 0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    snprintf(buf, sizeof buf, "LV %d  XP %.0f/%.0f (%.0f%%)  HP %.0f/%.0f  %02d:%02d  KILLS %u  GOLD %.0f  ENEMIES %d",
             p->level, p->xp, xp1, frac * 100, p->hp, p->maxHp, g->survived_seconds / 60, g->survived_seconds % 60, g->kills, g->coins, g->enemy_count);
    vs_text_draw(4, 4, 1, 0xffffff, buf);
    float wy = 16;
    for (int i = 0; i < VS_MAX_WEAPONS; i++) {
        if (!g->weapons[i].active) continue;
        snprintf(buf, sizeof buf, "%s %d", vs_weapon_name(g, g->weapons[i].type), g->weapons[i].level);
        vs_text_draw(4, wy, 1, 0xc0c0ff, buf);
        wy += 9;
    }
    if (g->game_over) vs_text_draw(g->logical_w * 0.5f - 60, g->logical_h * 0.5f - 10, 3, 0xff4040, "GAME OVER");
}

void vs_game_render(vs_game *g) {
    vs_player *p = &g->player;
    float cam_x = p->x - g->logical_w * 0.5f, cam_y = p->y - g->logical_h * 0.5f;
    vs_plat_begin_frame(0x000000);
    vs_plat_draw_tiled(g->textures[g->tex_bg], p->x, p->y, day_night_tint(g));
    vs_render_begin(g, cam_x, cam_y);

    /* pickups: depth 9 + y - player.y; gems behind everything (y - player.y - H) */
    for (int i = 0; i < VS_MAX_PICKUPS; i++) {
        vs_pickup *pk = &g->pickups[i];
        if (!pk->active) continue;
        const char *fname;
        if (pk->type == VS_PICKUPS_GEM) fname = pk->value <= 2 ? "GemBlue.png" : pk->value <= 9 ? "GemGreen.png" : "GemRed.png";
        else if (pk->type == VS_PICKUPS_TREASURE) fname = "BoxOpen.png";
        else if (pk->type == VS_PICKUPS_ROAST) fname = "Roast.png";
        else fname = "CoinGold.png";
        vs_frame f;
        if (vs_atlas_find(&g->atl_items, fname, &f) != 0) continue;
        float depth = pk->type == VS_PICKUPS_GEM ? pk->y - p->y - g->logical_h : 9 + pk->y - p->y;
        vs_render_sprite(g, g->tex_items, &f, pk->x, pk->y, 0.5f, 0.5f, 1.0f, 0, 0, 0, 0xffffff, 255, depth);
    }

    /* enemies: depth y - player.y, origin (0.5, 1), flip when moving right, wobble */
    for (int i = 0; i < g->enemy_count; i++) {
        vs_enemy *e = &g->enemies[i];
        if (!e->active) continue;
        vs_frame f;
        char name[64];
        int found;
        if (e->dying) {
            int fr = (int)(e->anim_t / (1000.0f / 24.0f));
            if (fr > e->frame_end) fr = e->frame_end;
            snprintf(name, sizeof name, "%s%d.png", e->frame_prefix, fr);
            found = vs_atlas_find(&g->atl_enemies, name, &f) == 0;
        } else if (e->idle_frames > 0) {
            int fr = 1 + (e->anim_frame_start + (int)(e->anim_t / 125.0f)) % e->idle_frames;
            snprintf(name, sizeof name, "%si%02d.png", e->frame_prefix, fr);
            found = vs_atlas_find(&g->atl_enemies, name, &f) == 0;
            if (!found) { snprintf(name, sizeof name, "%s0.png", e->frame_prefix); found = vs_atlas_find(&g->atl_enemies, name, &f) == 0; }
        } else {
            snprintf(name, sizeof name, "%s0.png", e->frame_prefix);
            found = vs_atlas_find(&g->atl_enemies, name, &f) == 0;
        }
        if (!found) continue;
        float phase = fmodf((float)g->tick * VS_TICK_MS + e->wobble * 45.0f, 1000.0f);
        float angle = e->dying || e->time_stopped ? 0 : (phase < 500 ? -5 + 10 * phase / 500 : 5 - 10 * (phase - 500) / 500);
        uint32_t tint = e->time_stopped ? 0x0000ff : (e->receiving_damage ? e->hit_tint : 0xffffff);
        vs_render_sprite(g, g->tex_enemies, &f, e->x, e->y, 0.5f, 1.0f, e->scale, e->dir_x > 0, 0, angle, tint, (uint8_t)(e->alpha * 255), e->y - p->y);
    }

    /* bullets (depth just above the player) */
    for (int i = 0; i < VS_MAX_BULLETS; i++) {
        vs_bullet *b = &g->bullets[i];
        if (!b->active) continue;
        vs_frame f;
        const char *fname = b->type == VS_WEAPONS_WHIP ? "slash.png" : "ProjectileHoly1.png";
        if (vs_atlas_find(&g->atl_vfx, fname, &f) != 0) continue;
        float scale = b->scale;
        if (b->type == VS_WEAPONS_WHIP && b->age_ms < 100) scale *= b->age_ms / 100.0f; /* scale-in tween */
        vs_render_sprite(g, g->tex_vfx, &f, b->x, b->y, 0.5f, 0.5f, scale, b->flip_x, b->flip_y, b->angle, 0xffffff, b->alpha, 1);
    }

    /* player at depth 0 */
    {
        char name[64];
        int frame = 1;
        if (p->walking_frames > 0) frame = 1 + (int)(p->anim_t / 125.0f) % p->walking_frames;
        snprintf(name, sizeof name, "%s%02d.png", p->sprite_prefix, frame);
        vs_frame f;
        if (vs_atlas_find(&g->atl_characters, name, &f) == 0) {
            uint32_t tint = p->receiving_damage ? 0xff0000 : (vs_player_is_invul(g) ? 0xffffbb : 0xffffff);
            vs_render_sprite(g, g->tex_characters, &f, p->x, p->y, 0.5f, 1.0f, 1.0f, p->facing_left, 0, 0, tint, 255, 0);
        }
    }
    vs_render_end(g);
    render_hud(g);
    if (g->levelup.pending) render_levelup(g);
    vs_plat_end_frame();
}

void vs_game_shutdown(vs_game *g) {
    vs_text_shutdown();
    for (int i = 0; i < g->texture_count; i++) vs_plat_texture_destroy(g->textures[i]);
    g->texture_count = 0;
}

uint32_t vs_game_state_hash(const vs_game *g) {
    uint32_t h = 0x811c9dc5u;
#define MIX(v) do { uint32_t _u; float _f = (float)(v); memcpy(&_u, &_f, 4); h ^= _u; h *= 0x01000193u; } while (0)
    MIX(g->tick); MIX(g->survived_seconds); MIX(g->player.x); MIX(g->player.y); MIX(g->player.hp); MIX(g->player.level); MIX(g->player.xp);
    MIX(g->enemy_count); MIX(g->kills); MIX(g->coins);
    for (int i = 0; i < g->enemy_count; i++) { const vs_enemy *e = &g->enemies[i]; MIX(e->type); MIX(e->x); MIX(e->y); MIX(e->hp); }
    for (int i = 0; i < VS_MAX_WEAPONS; i++) if (g->weapons[i].active) { MIX(g->weapons[i].type); MIX(g->weapons[i].level); }
#undef MIX
    return h;
}
