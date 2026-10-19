#include "game/game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/vs_audio.h"
#include "core/vs_enums.h"
#include "core/vs_image.h"
#include "core/vs_text.h"
#include "core/vs_version.h"

static float table_constant(const vs_pack *pack, const char *name, float dflt) {
    vs_table t;
    if (vs_pack_table(pack, "constants", &t) != 0) return dflt;
    int gi = vs_table_find_group(&t, name);
    if (gi < 0) return dflt;
    vs_group grp;
    vs_table_group(&t, (uint32_t)gi, &grp);
    vs_constants_rec r;
    vs_table_record(&t, grp.first, &r);
    /* speeds and multipliers: a NaN, zero or absurd value would break the
     * simulation, so the built-in value is used instead */
    return r.value > 1e-6f && r.value < 1e7f ? r.value : dflt;
}

/* The title screen's background art, built into the executable
 * (engine/assets/title_bg.png → title_bg_data.h). Padded to 1024 × 512
 * like the pack's textures. Returns the texture index or -1. */
#include "game/title_bg_data.h"
static int load_title_bg(vs_game *g) {
    int w, h;
    if (g->texture_count >= VS_MAX_TEXTURES) return -1;
    uint8_t *rgba = vs_image_decode(VS_TITLE_BG_JPG, sizeof VS_TITLE_BG_JPG, &w, &h);
    if (!rgba) return -1;
    const int tw = 1024, th = 512;
    uint8_t *argb = w <= tw && h <= th ? (uint8_t *)calloc((size_t)tw * th, 4) : NULL;
    if (argb) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const uint8_t *s = rgba + ((size_t)y * w + x) * 4;
                uint8_t *d = argb + ((size_t)y * tw + x) * 4;
                d[0] = 255; d[1] = s[0]; d[2] = s[1]; d[3] = s[2];   /* big-endian ARGB */
            }
    }
    vs_image_free(rgba);
    vs_texture *t = argb ? vs_plat_texture_create((uint32_t)tw, (uint32_t)th, argb) : NULL;
    free(argb);
    if (!t) return -1;
    g->title_bg_w = w; g->title_bg_h = h;
    g->textures[g->texture_count] = t;
    g->texture_names[g->texture_count] = "title_bg";
    return g->texture_count++;
}

/* The title art over the whole screen, darkened by `dim` (0 = as is). */
void vs_ui_menu_backdrop(vs_game *g, uint8_t dim) {
    if (g->tex_title_bg < 0) return;
    vs_sprite s = {0};
    s.tex = g->textures[g->tex_title_bg]; s.sw = (int16_t)g->title_bg_w; s.sh = (int16_t)g->title_bg_h;
    s.scale_x = (float)g->logical_w / g->title_bg_w; s.scale_y = (float)g->logical_h / g->title_bg_h;
    s.alpha = 255; s.tint = 0xffffff;
    vs_plat_draw(&s);
    if (dim) vs_ui_rect(g, 0, 0, (float)g->logical_w, (float)g->logical_h, 0x000000, dim);
}

static int load_texture(vs_game *g, const char *name) {
    for (int i = 0; i < g->texture_count; i++) if (g->texture_names[i] && strcmp(g->texture_names[i], name) == 0) return i;
    int entry = vs_pack_find(g->pack, "TEX", name);
    vs_tex_info ti;
    if (entry < 0 || vs_pack_tex(g->pack, entry, &ti) != 0 || g->texture_count >= VS_MAX_TEXTURES) return -1;
    vs_texture *t = vs_plat_texture_create(ti.w, ti.h, ti.pixels);
    if (!t) return -1;
    g->textures[g->texture_count] = t;
    g->texture_names[g->texture_count] = vs_pack_entry_name(g->pack, entry);
    return g->texture_count++;
}

static int load_atlas(vs_game *g, const char *name, vs_atlas *out, int *tex) {
    int idx = vs_pack_find(g->pack, "ATL", name);
    if (idx < 0 || vs_pack_atlas(g->pack, idx, out) != 0) return -1;
    *tex = load_texture(g, vs_pack_entry_name(g->pack, out->tex_entry));
    return *tex < 0 ? -1 : 0;
}

/* Enum ids from the tables index arrays sized by the compiled enums, so a
 * table whose group keys or enum fields (single or list) fall outside them
 * is refused (vs_pack_init has already checked every offset). */
typedef struct { int field, count; } enum_field;

static int ids_in_range(const vs_table *t, int key_count, const enum_field *ef, int nef) {
    for (uint32_t i = 0; i < t->group_count; i++) {
        int32_t k = vs_bei32(t->groups + 16 * i + 4);
        if (k < -1 || k >= key_count) return 0;
    }
    for (int j = 0; j < nef; j++) {
        uint32_t f = (uint32_t)ef[j].field;
        if (f >= t->field_count) return 0;
        uint32_t type = vs_be32(t->fields + 36 * f + 32);
        for (uint32_t r = 0; r < t->record_count; r++) {
            const uint8_t *rec = t->records + (uint64_t)r * t->record_size;
            uint32_t v = vs_be32(rec + 8 + 4 * f);
            if (type == 5) { if ((int32_t)v < -1 || (int32_t)v >= ef[j].count) return 0; }
            else if (type == 6) {
                if (!((vs_be64(rec) >> f) & 1) && v == 0) continue;
                const uint8_t *items; uint32_t n = vs_table_list(t, v, &items);
                for (uint32_t q = 0; q < n; q++) if (vs_list_i32(items, q) < -1 || vs_list_i32(items, q) >= ef[j].count) return 0;
            } else return 0;
        }
    }
    return 1;
}

static int open_table_ids(vs_game *g, const char *name, vs_table *t, const char *fields, int key_count, const enum_field *ef, int nef) {
    if (vs_pack_table(g->pack, name, t) != 0 || vs_table_check_fields(t, fields) != 0) { vs_plat_log("table %s missing or layout mismatch", name); return -1; }
    if (!ids_in_range(t, key_count, ef, nef)) { vs_plat_log("table %s has ids outside the game's enums", name); return -1; }
    return 0;
}
#define open_table(g, name, t, fields, key_count, ef) open_table_ids(g, name, t, fields, key_count, ef, (int)(sizeof ef / sizeof ef[0]))

static const enum_field CHARACTER_IDS[] = { { VS_CHARACTERS_F_STARTINGWEAPON, VS_WEAPONS_COUNT }, { VS_CHARACTERS_F_SHOWCASE, VS_WEAPONS_COUNT } };
static const enum_field ENEMY_IDS[] = { { VS_ENEMIES_F_SKILLS, 64 } };
static const enum_field WEAPON_IDS[] = { { VS_WEAPONS_F_BULLETTYPE, VS_WEAPONS_COUNT }, { VS_WEAPONS_F_ADDEVOLVEDWEAPON, VS_WEAPONS_COUNT },
                                         { VS_WEAPONS_F_EVOLVESFROM, VS_WEAPONS_COUNT }, { VS_WEAPONS_F_REQUIRES, VS_WEAPONS_COUNT }, { VS_WEAPONS_F_HITVFX, VS_HITVFX_COUNT } };
static const enum_field POWERUP_IDS[] = { { VS_POWERUPS_F_BULLETTYPE, VS_WEAPONS_COUNT } };
static const enum_field DESTRUCTIBLE_IDS[] = { { VS_DESTRUCTIBLES_F_DESTRUCTIBLETYPE, VS_DESTRUCTIBLES_COUNT } };
static const enum_field ACHIEVEMENT_IDS[] = { { VS_ACHIEVEMENTS_F_WEAPONTOUNLOCK, VS_WEAPONS_COUNT }, { VS_ACHIEVEMENTS_F_STAGETOUNLOCK, VS_STAGES_COUNT },
                                              { VS_ACHIEVEMENTS_F_CHARACTERTOUNLOCK, VS_CHARACTERS_COUNT }, { VS_ACHIEVEMENTS_F_HYPERTOUNLOCK, VS_STAGES_COUNT },
                                              { VS_ACHIEVEMENTS_F_WEAPONICON, VS_WEAPONS_COUNT } };
static const enum_field STAGE_IDS[] = { { VS_STAGES_F_BGM, VS_BGM_COUNT }, { VS_STAGES_F_LEGACYBGM, VS_BGM_COUNT }, { VS_STAGES_F_DESTRUCTIBLETYPE, VS_DESTRUCTIBLES_COUNT },
                                        { VS_STAGES_F_SPAWNTYPE, VS_SPAWN_COUNT }, { VS_STAGES_F_ENEMIES, VS_ENEMIES_COUNT }, { VS_STAGES_F_BOSSES, VS_ENEMIES_COUNT } };
static const enum_field EVENT_IDS[] = { { VS_STAGES_EVENTS_F_EVENTTYPE, VS_EVENTS_COUNT }, { VS_STAGES_EVENTS_F_MOREY, VS_ENEMIES_COUNT } };
static const enum_field TREASURE_IDS[] = { { VS_STAGES_TREASURE_F_PRIZETYPES, VS_PRIZES_COUNT }, { VS_STAGES_TREASURE_F_FIXEDPRIZES, VS_WEAPONS_COUNT } };
static const enum_field NO_IDS[1] = { { -1, 0 } };

void vs_build_character_list(vs_game *g) {
    g->menu_char_count = 0;
    for (uint32_t gi = 0; gi < g->characters_table.group_count && g->menu_char_count < 16; gi++) {
        vs_group grp; vs_table_group(&g->characters_table, gi, &grp);
        vs_characters_rec c; vs_table_record(&g->characters_table, grp.first, &c);
        (void)c;
        if (!vs_character_selectable(g, grp.enum_index)) continue;   /* unlocked, with a weapon the demake has */
        g->menu_char_list[g->menu_char_count++] = grp.enum_index;
    }
}

/* ---- sound (notes/behaviour/audio.md) ---- */
static const char *const SND_KEYS[VS_SND_COUNT] = {
    "Hit", "LossSFX", "Gem", "Coin", "Roast", "LittleHeart", "Vacuum",
    "Rosary", "Orologion", "Whip", "MagicMissile", "Shot", "Holywater",
    "Lightning", "LevelUp", "ClickIn", "ClickOut", "ThingFound",
    "Treasure1", "Treasure2", "Treasure3", "BGM_Intro", "BGM_GameOver",
    "Fireloop", "Explosion", "Alert",
};

void vs_sfx(vs_game *g, int snd, float volume, float detune, float rate, float delay_ms, int max_instances) {
    if (snd < 0 || snd >= VS_SND_COUNT) return;
    vs_audio_play(g->snd[snd], volume, detune, rate, delay_ms, max_instances);
}

/* Math.random() for sound pitch, from its own generator */
float vs_sfx_random(vs_game *g) { return vs_rng_f32(&g->audio_rng); }

/* The stage's track (its BGM field; the "stream-safe" default), volume 0.3, looped. */
void vs_music_for_stage(vs_game *g) {
    int gi = vs_table_find_group_enum(&g->stages_table, g->selected_stage);
    if (gi < 0) return;
    vs_group grp; vs_table_group(&g->stages_table, (uint32_t)gi, &grp);
    vs_stages_rec r; vs_table_record(&g->stages_table, grp.first, &r);
    if (!VS_HAS(&r, VS_STAGES_F_BGM)) return;
    vs_enum e;
    if (vs_pack_enum(g->pack, "bgm", &e) != 0 || r.BGM < 0 || (uint32_t)r.BGM >= e.count) return;
    vs_audio_music_play(vs_audio_find(vs_enum_value(&e, (uint32_t)r.BGM)), 0.3f, 1);
}

static void sound_init(vs_game *g) {
    for (int i = 0; i < VS_SND_COUNT; i++) g->snd[i] = vs_audio_find(SND_KEYS[i]);
    vs_rng_seed(&g->audio_rng, 0x5eedu);
    vs_rng_seed(&g->ui_rng, 0x7ea5u);
    vs_audio_set_volumes(g->save.sound_volume, g->save.music_volume);
}

int vs_game_init(vs_game *g, const vs_pack *pack, int lw, int lh, uint64_t seed) {
    int auto_pick = g->auto_pick, menu_stage = g->menu_stage, debug_hitboxes = g->debug_hitboxes, unlock_all = g->unlock_all, show_gameover = g->show_gameover, show_chest = g->show_chest;
    int selected_character = g->selected_character;
    char save_path[sizeof g->save_path], audio_status[sizeof g->audio_status];
    memcpy(save_path, g->save_path, sizeof save_path);
    memcpy(audio_status, g->audio_status, sizeof audio_status);
    memset(g, 0, sizeof *g);
    g->auto_pick = auto_pick; g->menu_stage = menu_stage; g->debug_hitboxes = debug_hitboxes; g->unlock_all = unlock_all; g->show_gameover = show_gameover; g->show_chest = show_chest;
    g->selected_character = selected_character;
    memcpy(g->audio_status, audio_status, sizeof audio_status);
    g->pack = pack;
    g->logical_w = lw;
    g->logical_h = lh;
    g->seed = seed;

    g->base_player_px_speed = table_constant(pack, "_basePlayerPxSpeed", 82.5f);
    g->base_enemy_speed = table_constant(pack, "_baseEnemySpeed", 0.231f);
    g->base_projectile_speed = table_constant(pack, "_baseProjectileSpeed", 165.0f);

    if (open_table(g, "characters", &g->characters_table, VS_CHARACTERS_FIELD_NAMES, VS_CHARACTERS_COUNT, CHARACTER_IDS)) return -1;
    if (open_table(g, "enemies", &g->enemies_table, VS_ENEMIES_FIELD_NAMES, VS_ENEMIES_COUNT, ENEMY_IDS)) return -1;
    if (open_table_ids(g, "hitvfx", &g->hitvfx_table, VS_HITVFX_FIELD_NAMES, VS_HITVFX_COUNT, NO_IDS, 0)) return -1;
    if (open_table(g, "weapons", &g->weapons_table, VS_WEAPONS_FIELD_NAMES, VS_WEAPONS_COUNT, WEAPON_IDS)) return -1;
    if (open_table_ids(g, "pickups", &g->pickups_table, VS_PICKUPS_FIELD_NAMES, VS_PICKUPS_COUNT, NO_IDS, 0)) return -1;
    if (open_table(g, "destructibles", &g->destructibles_table, VS_DESTRUCTIBLES_FIELD_NAMES, VS_DESTRUCTIBLES_COUNT, DESTRUCTIBLE_IDS)) return -1;
    if (open_table(g, "achievements", &g->achievements_table, VS_ACHIEVEMENTS_FIELD_NAMES, VS_ACHIEVEMENTS_COUNT, ACHIEVEMENT_IDS)) return -1;
    if (open_table(g, "stages", &g->stages_table, VS_STAGES_FIELD_NAMES, VS_STAGES_COUNT, STAGE_IDS)) return -1;
    if (open_table(g, "powerups", &g->powerups_table, VS_POWERUPS_FIELD_NAMES, VS_WEAPONS_COUNT, POWERUP_IDS)) return -1;
    {   /* the stage sub-tables, opened again by vs_stage_init */
        vs_table sub;
        if (open_table(g, "stages.events", &sub, VS_STAGES_EVENTS_FIELD_NAMES, VS_STAGES_COUNT, EVENT_IDS)) return -1;
        if (open_table(g, "stages.treasure", &sub, VS_STAGES_TREASURE_FIELD_NAMES, VS_STAGES_COUNT, TREASURE_IDS)) return -1;
    }
    vs_save_load(g, save_path);

    if (load_atlas(g, "characters", &g->atl_characters, &g->tex_characters) != 0) return -1;
    if (load_atlas(g, "enemies", &g->atl_enemies, &g->tex_enemies) != 0) return -1;
    if (load_atlas(g, "items", &g->atl_items, &g->tex_items) != 0) return -1;
    if (load_atlas(g, "vfx", &g->atl_vfx, &g->tex_vfx) != 0) return -1;
    if (load_atlas(g, "UI", &g->atl_ui, &g->tex_ui) != 0) return -1;
    vs_pack_lang(g->pack, "en", &g->lang);
    /* the demake's own logo (patcher: logo.js); older packs only have the original */
    {
        static const char *const names[2] = { "title_vsdemake", "title" };
        vs_tex_info ti;
        g->tex_title = -1;
        for (int i = 0; i < 2 && g->tex_title < 0; i++) {
            int entry = vs_pack_find(g->pack, "TEX", names[i]);
            if (entry < 0 || vs_pack_tex(g->pack, entry, &ti) != 0) continue;
            g->tex_title = load_texture(g, names[i]);
            g->title_w = (int)ti.used_w; g->title_h = (int)ti.used_h;
        }
    }
    g->tex_title_bg = load_title_bg(g);
    {
        int fi = vs_pack_find(pack, "FNT", "damNum-export");
        if (fi < 0 || vs_pack_font(pack, fi, &g->damage_font) != 0) return -1;
        g->tex_damage = load_texture(g, vs_pack_entry_name(pack, g->damage_font.tex_entry));
    }
    if (vs_text_init() != 0) return -1;
    sound_init(g);
    vs_sfx(g, VS_SND_BGM_INTRO, 0.7f, 0, 1, 0, 0);   /* after the start-up warning in the original */
    vs_build_character_list(g);
    /* menus start on the last run's choices (save), command line wins */
    if (!g->selected_character) g->selected_character = vs_character_selectable(g, g->save.selected_character) ? g->save.selected_character : VS_CHARACTERS_ANTONIO;
    for (int i = 0; i < g->menu_char_count; i++) if (g->menu_char_list[i] == g->selected_character) g->menu_sel = i;
    if (!g->menu_stage && g->save.selected_stage == VS_STAGES_LIBRARY) g->menu_stage = 1;
    if (g->menu_stage && !vs_stage_selectable(g, VS_STAGES_LIBRARY)) g->menu_stage = 0;
    g->selected_stage = g->menu_stage ? VS_STAGES_LIBRARY : VS_STAGES_FOREST;
    g->screen = VS_SCREEN_TITLE;
    if (g->auto_pick) return vs_game_start_run(g, g->selected_character, g->selected_stage);
    return 0;
}

/* InitGame (Core, line 6381): fresh run state; resources stay loaded. */
int vs_game_start_run(vs_game *g, int character_enum, int stage_enum) {
    g->paused = 0;
    g->quit_run = 0;
    memset(&g->treasure, 0, sizeof g->treasure);
    memset(&g->item_found, 0, sizeof g->item_found);
    memset(&g->player, 0, sizeof g->player);
    memset(g->enemies, 0, sizeof g->enemies);
    memset(g->weapons, 0, sizeof g->weapons);
    memset(g->bullets, 0, sizeof g->bullets);
    memset(g->pickups, 0, sizeof g->pickups);
    memset(&g->levelup, 0, sizeof g->levelup);
    memset(g->destructibles, 0, sizeof g->destructibles);
    memset(&g->stage, 0, sizeof g->stage);
    memset(&g->tilemap, 0, sizeof g->tilemap);
    memset(g->fx, 0, sizeof g->fx);
    memset(g->explosions, 0, sizeof g->explosions);
    memset(g->smoke, 0, sizeof g->smoke);
    g->timestop_fx_ms = -1;
    memset(g->timers, 0, sizeof g->timers);
    g->enemy_count = 0; g->cull_index = 0; g->next_uid = 1; g->time_stopped = 0; g->game_over = 0;
    g->kills = 0; g->coins = 0; g->chests_opened = 0; g->tick = 0; g->update_tick = 0; g->survived_seconds = 0;
    vs_rng_seed(&g->rng, g->seed);
    g->gameover_ms = 0;          /* the run-end save triggers when this is 0 */
    g->save_status[0] = 0;
    vs_save_run_start(g);
    g->player_px_speed = g->base_player_px_speed;
    g->enemy_speed = g->base_enemy_speed;
    g->projectile_speed = g->base_projectile_speed;
    g->gold_multiplier = g->enemy_health_multiplier = 1;

    g->magnet.radius = 30; g->magnet.percentage = 1;
    if (vs_player_init(g, character_enum) != 0) return -1;
    if (g->player.magnet_bonus > 0) { g->magnet.radius += g->magnet.radius * g->player.magnet_bonus; g->magnet.percentage += g->player.magnet_bonus; }
    vs_timer_add(g, VS_TIMER_TICKER, 1000, -1, 0, 0);
    vs_timer_add(g, VS_TIMER_REGEN, 1000, -1, 0, 0);
    vs_levelup_init(g);
    if (vs_stage_init(g, stage_enum) != 0) { vs_plat_log("stage init failed"); return -1; }
    vs_weapon_add(g, g->player.startingWeapon, 1);
    vs_loot_recalculate(g);
    vs_powerups_apply(g);   /* bought PowerUp ranks (shop.c) */
    {
        vs_stage *s = &g->stage;
        vs_stages_rec *row0 = &s->rows[0].rec;
        const char *bg = VS_HAS(row0, VS_STAGES_F_BGTEXTURENAME) ? vs_table_str(&s->table, row0->BGTextureName) : "bg_forest";
        g->tex_bg = load_texture(g, bg);
        if (s->has_tileset && vs_tilemap_init(g, vs_table_str(&s->table, row0->tileset_mapKey)) != 0) vs_plat_log("tilemap failed to load");
        vs_tilemap_update(g);
    }
    vs_music_for_stage(g);
    g->screen = VS_SCREEN_GAME;
    return g->tex_bg < 0 ? -1 : 0;
}

#define TITLE_ITEMS 5   /* START, POWER UP, COLLECTION, ACHIEVEMENTS, OPTIONS */

static void menu_update(vs_game *g, const vs_pad *pad) {
    if (g->screen == VS_SCREEN_OPTIONS) { vs_options_input(g, pad); return; }   /* reads prev_buttons itself */
    if (g->screen == VS_SCREEN_POWERUP) { vs_shop_input(g, pad); return; }
    if (g->screen == VS_SCREEN_COLLECTION) { vs_collection_input(g, pad); return; }
    if (g->screen == VS_SCREEN_SELECT) { vs_char_select_input(g, pad); return; }
    if (g->screen == VS_SCREEN_STAGE) { vs_stage_select_input(g, pad); return; }
    if (g->screen == VS_SCREEN_ACHIEVEMENTS) { vs_achievements_input(g, pad); return; }
    uint32_t pressed = pad->buttons & ~g->prev_buttons;
    g->prev_buttons = pad->buttons;
    switch (g->screen) {
    case VS_SCREEN_TITLE:
        if (pressed & VS_BTN_UP) { g->title_sel = (g->title_sel + TITLE_ITEMS - 1) % TITLE_ITEMS; vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0); }
        if (pressed & VS_BTN_DOWN) { g->title_sel = (g->title_sel + 1) % TITLE_ITEMS; vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0); }
        if (pressed & (VS_BTN_A | VS_BTN_START)) {
            vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0);
            if (g->title_sel == 4) { g->screen = VS_SCREEN_OPTIONS; vs_options_open(g); }
            else if (g->title_sel == 3) vs_achievements_open(g);
            else if (g->title_sel == 2) vs_collection_open(g);
            else if (g->title_sel == 1) vs_shop_open(g);
            else vs_char_select_open(g);
        }
        break;
    case VS_SCREEN_GAMEOVER:
        if (pressed & (VS_BTN_A | VS_BTN_START)) { g->screen = VS_SCREEN_TITLE; vs_audio_music_stop(); vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0); }
        break;
    default: break;
    }
}

void vs_game_update(vs_game *g, const vs_pad *pad) {
    if (g->screen != VS_SCREEN_GAME) { menu_update(g, pad); return; }
    if (g->paused) { vs_options_input(g, pad); return; }   /* the run is frozen */
    if (g->treasure.active) { vs_treasure_ui_update(g, pad); return; }
    if (g->item_found.active) { vs_item_found_update(g, pad); return; }
    if (g->game_over) {
        if (g->gameover_ms == 0) {
            vs_save_run_end(g);                       /* lifetime counters, achievements, save */
            vs_audio_music_fade(0, 500);
        }
        g->gameover_ms += VS_TICK_MS;
        if (g->gameover_ms > 1250 && (!g->auto_pick || g->show_gameover)) {
            g->screen = VS_SCREEN_GAMEOVER; g->prev_buttons = pad->buttons;
            vs_sfx(g, VS_SND_BGM_GAMEOVER, 1.4f, 0, 1, 0, 0);
        }
        return;
    }
    if (g->levelup.pending) {
        vs_levelup_input(g, pad);
        g->prev_buttons = pad->buttons;
        return;
    }
    /* Start pauses (original: pause button / gamepad button 9) */
    if (pad->buttons & ~g->prev_buttons & VS_BTN_START) {
        g->prev_buttons = pad->buttons;
        g->paused = 1;
        vs_options_open(g);
        return;
    }
    g->prev_buttons = pad->buttons;

    /* notes/behaviour/loop.md */
    vs_player_update(g, pad);
    vs_tilemap_update(g);
    vs_enemies_cull_step(g);
    vs_bullets_cull(g);

    g->update_tick++;
    int slow = 0;
    if (g->update_tick % 4 == 0) { g->update_tick = 0; slow = 1; }
    if (slow) {
        if (g->player.xp >= g->levelup.current_xp_factor) {
            if (vs_player_level_up(g)) vs_weapons_reset_timers(g);
            vs_loot_recalculate(g);
            vs_levelup_calculate_xp_factor(g);
            vs_levelup_open(g);
        }
        vs_pickups_update(g);
    }
    vs_bullets_update(g);
    vs_weapons_update(g);
    vs_enemies_update(g);
    vs_explosions_update(g);
    vs_pickups_physics(g);
    vs_destructibles_update(g);
    vs_fx_update(g);
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

/* Draws `frame` from the UI atlas stretched over (x, y, w, h) as a nine-slice
 * with corners of `c` px (the original's NineSlice configs). */
void vs_ui_nine_slice(vs_game *g, const char *frame, float x, float y, float w, float h, int cw, int ch, uint32_t tint) {
    vs_ui_nine_slice_alpha(g, frame, x, y, w, h, cw, ch, tint, 255);
}

void vs_ui_nine_slice_alpha(vs_game *g, const char *frame, float x, float y, float w, float h, int cw, int ch, uint32_t tint, uint8_t alpha) {
    vs_frame f;
    if (vs_atlas_find(&g->atl_ui, frame, &f) != 0) return;
    /* whole pixels, or the stretched rows leave 1 px seams */
    float x1 = floorf(x + w + 0.5f), y1 = floorf(y + h + 0.5f);
    x = floorf(x + 0.5f); y = floorf(y + 0.5f); w = x1 - x; h = y1 - y;
    int sx[3] = { f.x, f.x + cw, f.x + f.w - cw }, sw[3] = { cw, f.w - 2 * cw, cw };
    int sy[3] = { f.y, f.y + ch, f.y + f.h - ch }, sh[3] = { ch, f.h - 2 * ch, ch };
    float dx[3] = { x, x + cw, x + w - cw }, dw[3] = { (float)cw, w - 2 * cw, (float)cw };
    float dy[3] = { y, y + ch, y + h - ch }, dh[3] = { (float)ch, h - 2 * ch, (float)ch };
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            if (dw[i] <= 0 || dh[j] <= 0) continue;
            vs_sprite s = {0};
            s.tex = g->textures[g->tex_ui];
            s.sx = (int16_t)sx[i]; s.sy = (int16_t)sy[j]; s.sw = (int16_t)sw[i]; s.sh = (int16_t)sh[j];
            s.dx = dx[i]; s.dy = dy[j]; s.scale_x = dw[i] / sw[i]; s.scale_y = dh[j] / sh[j];
            s.tint = tint; s.alpha = alpha; s.blend = (uint8_t)vs_render_current_blend();
            vs_plat_draw(&s);
        }
    }
}

static void nine_slice(vs_game *g, const char *frame, float x, float y, float w, float h, int c, uint32_t tint) {
    vs_ui_nine_slice(g, frame, x, y, w, h, c, c, tint);
}

/* Solid rectangle in screen space (whiteDot stretched). */
void vs_ui_rect_blend(vs_game *g, float x, float y, float w, float h, uint32_t tint, uint8_t alpha, int blend) {
    vs_frame white;
    if (vs_atlas_find(&g->atl_characters, "whiteDot.png", &white) != 0) return;
    vs_sprite s = {0};
    s.tex = g->textures[g->tex_characters]; s.sx = (int16_t)white.x; s.sy = (int16_t)white.y; s.sw = 1; s.sh = 1;
    s.dx = x; s.dy = y; s.scale_x = w; s.scale_y = h; s.tint = tint; s.alpha = alpha; s.blend = (uint8_t)blend;
    vs_plat_draw(&s);
}
void vs_ui_rect(vs_game *g, float x, float y, float w, float h, uint32_t tint, uint8_t alpha) { vs_ui_rect_blend(g, x, y, w, h, tint, alpha, VS_BLEND_ALPHA); }

/* Draws `text` word-wrapped to `max_w`; honours '\n'. Returns lines drawn. */
static int text_wrapped(float x, float y, float scale, uint32_t tint, const char *text, float max_w, float line_h, int max_lines) {
    char line[160];
    int lines = 0, len = 0, last_space = -1;
    const float adv = (VS_TEXT_GLYPH_W + 1) * scale;
    for (const char *p = text;; p++) {
        char c = *p;
        int flush = c == 0 || c == '\n';
        if (!flush) {
            if (len == 0 && c == ' ') continue;
            if (len < (int)sizeof line - 1) { line[len++] = c; if (c == ' ') last_space = len - 1; }
            if ((len + 1) * adv > max_w) {
                int cut = last_space > 0 ? last_space : len;
                char rest[160]; int rest_len = len - cut;
                memcpy(rest, line + cut, (size_t)rest_len);
                line[cut] = 0;
                if (lines < max_lines) vs_text_draw(x, y + lines * line_h, scale, tint, line);
                lines++;
                len = 0; last_space = -1;
                for (int i = 0; i < rest_len; i++) { if (len == 0 && rest[i] == ' ') continue; line[len++] = rest[i]; if (rest[i] == ' ') last_space = len - 1; }
            }
            continue;
        }
        if (len > 0) {
            line[len] = 0;
            if (lines < max_lines) vs_text_draw(x, y + lines * line_h, scale, tint, line);
            lines++;
        }
        len = 0; last_space = -1;
        if (c == 0) break;
    }
    return lines;
}

/* One choice panel of the level-up / item-found screens: icon in its frame,
 * name, the new/level marker and the description. */
static void choice_panel(vs_game *g, const char *name, const char *level, uint32_t level_tint, const char *desc,
                         const char *frame, uint32_t bg_tint, float px, float py, float pw, float ph) {
    nine_slice(g, "frame1_c2.png", px, py, pw, ph, 12, bg_tint);
    vs_frame f;
    if (vs_atlas_find(&g->atl_ui, "frameB.png", &f) == 0) {
        vs_sprite s = {0};
        s.tex = g->textures[g->tex_ui]; s.sx = (int16_t)f.x; s.sy = (int16_t)f.y; s.sw = (int16_t)f.w; s.sh = (int16_t)f.h;
        s.dx = px + 4; s.dy = py + 4; s.scale_x = s.scale_y = 2; s.tint = 0xffffff; s.alpha = 255;
        vs_plat_draw(&s);
    }
    if (frame && vs_atlas_find(&g->atl_items, frame, &f) == 0) {
        vs_sprite s = {0};
        s.tex = g->textures[g->tex_items]; s.sx = (int16_t)f.x; s.sy = (int16_t)f.y; s.sw = (int16_t)f.w; s.sh = (int16_t)f.h;
        s.scale_x = s.scale_y = 2; s.tint = 0xffffff; s.alpha = 255;
        /* centred on (28, 28) like the original, using the untrimmed size */
        s.dx = px + 28 + (f.off_x - 0.5f * f.src_w) * 2; s.dy = py + 28 + (f.off_y - 0.5f * f.src_h) * 2;
        vs_plat_draw(&s);
    }
    vs_text_draw(px + 64, py + 8, 2, 0xffffff, name);
    vs_text_draw(px + 0.7f * pw, py + 8, 2, level_tint, level);
    text_wrapped(px + 64, py + 28, 1.5f, 0xffffff, desc, pw - 64 - 10, 12, 3);
}

/* The panel for a weapon or passive: "NEW" or the level it would reach, and
 * the description (for an upgrade: what that level adds). */
void vs_ui_weapon_panel(vs_game *g, int type, float px, float py, float pw, float ph) {
    const char *name = "?", *level = "", *desc = "", *frame = NULL;
    uint32_t level_tint = 0xffffff, bg_tint = 0xffffff;
    char level_buf[32];
    int slot = vs_weapon_find(g, type);
    int next = slot >= 0 ? g->weapons[slot].level : 0;   /* table index of the level offered */
    vs_weapons_rec r0, rn;
    if (vs_weapon_record(g, type, 0, &r0) == 0) {
        if (VS_HAS(&r0, VS_WEAPONS_F_NAME)) name = vs_table_str(&g->weapons_table, r0.name);
        if (VS_HAS(&r0, VS_WEAPONS_F_DESCRIPTION)) desc = vs_table_str(&g->weapons_table, r0.description);
        if (VS_HAS(&r0, VS_WEAPONS_F_FRAMENAME)) frame = vs_table_str(&g->weapons_table, r0.frameName);
        if (r0.isEvolution) bg_tint = 0xffff00;
    }
    if (next == 0) { level = vs_lang_get(&g->lang, "weapon_new", "NEW"); level_tint = 0xffff00; }
    else {
        snprintf(level_buf, sizeof level_buf, "%s%d", vs_lang_get(&g->lang, "weapon_level_", "LV "), next + 1);
        level = level_buf;
        if (vs_weapon_record(g, type, next, &rn) == 0 && VS_HAS(&rn, VS_WEAPONS_F_LEVELUPTEXT)) desc = vs_table_str(&g->weapons_table, rn.levelUpText);
    }
    choice_panel(g, name, level, level_tint, desc, frame, bg_tint, px, py, pw, ph);
}

/* Level-up screen laid out like the original's (menu frame, header, one
 * panel per choice: icon, name, new/level marker, what it does). Text comes
 * from the pack's LNG strings; the fallbacks here are our own wording. */
static void render_levelup(vs_game *g) {
    vs_levelup *lu = &g->levelup;
    const float bw = 0.8f * g->logical_w, bh = 0.8f * g->logical_h;
    const float left = 0.5f * (g->logical_w - bw), top = 0.5f * (g->logical_h - bh);
    const float pw = 0.95f * bw, ph = 0.2f * bh, px = left + 0.025f * bw;
    nine_slice(g, "frame5_c4.png", left, top, bw, bh, 12, 0xffffff);
    const char *header = vs_lang_get(&g->lang, "levelup_header", "LEVEL UP");
    vs_text_draw(0.5f * (g->logical_w - vs_text_width(3, header)), top + 14, 3, 0xffffff, header);
    if (lu->option_count < 4) {
        /* the original's footer sits where a 4th panel would be */
        const char *footer = vs_lang_get(&g->lang, "levelup_footer", "More Luck may add a fourth choice");
        char buf[160]; int n = 0; float fy = top + 3.85f * ph;
        for (const char *p = footer;; p++) {
            if (*p == '\n' || *p == 0) {
                buf[n] = 0;
                if (n > 0) vs_text_draw(0.5f * (g->logical_w - vs_text_width(1.5f, buf)), fy, 1.5f, 0xffffff, buf);
                fy += 7; n = 0;
                if (*p == 0) break;
            } else if (n < (int)sizeof buf - 1) buf[n++] = *p;
        }
    }
    for (int i = 0; i < lu->option_count; i++) {
        float py = top + ph * i + ph - 0.025f * bw;
        if (lu->items_mode) {
            const char *name = "?", *desc = "", *frame = NULL;
            int gi = vs_table_find_group_enum(&g->pickups_table, lu->item_options[i]);
            if (gi >= 0) {
                vs_group grp; vs_table_group(&g->pickups_table, (uint32_t)gi, &grp);
                vs_pickups_rec r; vs_table_record(&g->pickups_table, grp.first, &r);
                if (VS_HAS(&r, VS_PICKUPS_F_NAME)) name = vs_table_str(&g->pickups_table, r.name);
                if (VS_HAS(&r, VS_PICKUPS_F_DESCRIPTION)) desc = vs_table_str(&g->pickups_table, r.description);
                if (VS_HAS(&r, VS_PICKUPS_F_FRAMENAME)) frame = vs_table_str(&g->pickups_table, r.frameName);
            }
            choice_panel(g, name, "", 0xffffff, desc, frame, 0xffffff, px, py, pw, ph);
        } else vs_ui_weapon_panel(g, lu->options[i], px, py, pw, ph);
        if (i == lu->selected) nine_slice(g, "selectionSquareActive_03.png", px - 4, py - 4, pw + 8, ph + 8, 7, 0xffffff);
    }
}

static void render_hud(vs_game *g) {
    char buf[160];
    vs_player *p = &g->player;
    float xp0 = g->levelup.previous_xp_factor, xp1 = g->levelup.current_xp_factor;
    float frac = xp1 > xp0 ? (p->xp - xp0) / (xp1 - xp0) : 0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    /* XP bar across the top, HP bar under the player (as the original) */
    vs_sprite bar = {0};
    vs_frame white;
    if (vs_atlas_find(&g->atl_characters, "whiteDot.png", &white) == 0) {
        bar.tex = g->textures[g->tex_characters]; bar.sx = (int16_t)white.x; bar.sy = (int16_t)white.y; bar.sw = 1; bar.sh = 1; bar.alpha = 255;
        bar.dx = 0; bar.dy = 0; bar.scale_x = (float)g->logical_w; bar.scale_y = 12; bar.tint = 0x101030; vs_plat_draw(&bar);
        bar.dx = 4; bar.dy = 4; bar.scale_x = (g->logical_w - 8) * frac; bar.scale_y = 6; bar.tint = 0x2266dd; vs_plat_draw(&bar);
        float hx = g->logical_w * 0.5f - 16, hy = g->logical_h * 0.5f + 2;
        bar.dx = hx; bar.dy = hy; bar.scale_x = 32; bar.scale_y = 4; bar.tint = 0x000000; vs_plat_draw(&bar);
        bar.scale_x = 32 * (p->hp > 0 ? p->hp / p->maxHp : 0); bar.tint = 0xe00808; vs_plat_draw(&bar);
    }
    snprintf(buf, sizeof buf, "LV %d", p->level);
    vs_text_draw((float)(g->logical_w - 40), 3, 1, 0xffffff, buf);
    snprintf(buf, sizeof buf, "%02d:%02d", g->survived_seconds / 60, g->survived_seconds % 60);
    vs_text_draw(g->logical_w * 0.5f - 15, 16, 1.5f, 0xffffff, buf);
    snprintf(buf, sizeof buf, "KILLS %u   GOLD %.0f", g->kills, g->coins);
    vs_text_draw((float)(g->logical_w - 150), 16, 1, 0xffffff, buf);
    float wy = 16;
    for (int i = 0; i < VS_MAX_WEAPONS; i++) {
        if (!g->weapons[i].active || g->weapons[i].hidden) continue;
        snprintf(buf, sizeof buf, "%s %d", vs_weapon_name(g, g->weapons[i].type), g->weapons[i].level);
        vs_text_draw(4, wy, 1, g->weapons[i].is_power_up ? 0xc0ffc0 : 0xc0c0ff, buf);
        wy += 9;
    }
    if (g->game_over) vs_text_draw(g->logical_w * 0.5f - 80, g->logical_h * 0.5f - 10, 3, 0xff4040, "GAME OVER");
}

static void render_menu(vs_game *g) {
    char buf[256];
    vs_plat_begin_frame(0x000000);
    int title_art = g->screen == VS_SCREEN_TITLE && g->tex_title_bg >= 0;
    if (g->screen == VS_SCREEN_TITLE) vs_ui_menu_backdrop(g, 0);
    else if (g->screen != VS_SCREEN_GAMEOVER) vs_ui_menu_backdrop(g, 150);   /* the other menus: the art, dimmed */
    if (title_art) {
        /* the art carries its own VSDEMAKE logo; dark panels behind the text keep it readable */
        vs_ui_rect(g, 0.5f * g->logical_w - 120, 244, 240, 112, 0x000000, 170);
        vs_ui_rect(g, 0.5f * g->logical_w - 230, 391, 460, 46, 0x000000, 170);
        vs_ui_rect(g, (float)g->logical_w - 230, 0, 230, 18, 0x000000, 170);
        vs_ui_rect(g, (float)g->logical_w - 90, (float)g->logical_h - 18, 90, 18, 0x000000, 170);
    }
    if (g->screen == VS_SCREEN_TITLE && !title_art) {
        if (g->tex_title >= 0) {
            vs_sprite s = {0};
            /* widest fit in 3/4 of the screen and 200 px of height, at most 2x */
            float sc = 0.75f * g->logical_w / g->title_w, sh = 200.0f / g->title_h;
            if (sh < sc) sc = sh;
            if (sc > 2.0f) sc = 2.0f;
            s.tex = g->textures[g->tex_title]; s.sw = (int16_t)g->title_w; s.sh = (int16_t)g->title_h;
            s.scale_x = s.scale_y = sc; s.alpha = 255; s.tint = 0xffffff;
            s.dx = 0.5f * (g->logical_w - g->title_w * sc); s.dy = 130.0f - 0.5f * g->title_h * sc; vs_plat_draw(&s);
        }
    }
    if (g->screen == VS_SCREEN_TITLE || g->screen == VS_SCREEN_SELECT || g->screen == VS_SCREEN_STAGE || g->screen == VS_SCREEN_POWERUP) {
        snprintf(buf, sizeof buf, "GOLD %.0f   ACHIEVEMENTS %d/%d", g->save.coins, vs_save_achievement_count(g), VS_ACHIEVEMENTS_COUNT);
        vs_text_draw(g->logical_w - vs_text_width(1, buf) - 8, 6, 1, 0xffd040, buf);
    }
    if (g->screen == VS_SCREEN_TITLE) {
        static const char *const fallback[TITLE_ITEMS] = { "START", "POWER UP", "COLLECTION", "ACHIEVEMENTS", "OPTIONS" };
        static const char *const keys[TITLE_ITEMS] = { "menu_start", "menu_power_up", "menu_collection", "menu_achievements", "options_header" };
        for (int i = 0; i < TITLE_ITEMS; i++) {
            char label[64];
            snprintf(label, sizeof label, "%s%s", g->title_sel == i ? "> " : "", vs_lang_get(&g->lang, keys[i], fallback[i]));
            for (char *c = label; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
            vs_text_draw(0.5f * (g->logical_w - vs_text_width(1.5f, label)), 255.0f + 18 * i, 1.5f, g->title_sel == i ? 0xffff80 : 0xffffff, label);
        }
        {
            const char *hint = "unofficial demake - stick/d-pad move, A select, B back";
            vs_text_draw(0.5f * (g->logical_w - vs_text_width(1, hint)), 398, 1, 0x808080, hint);
            const char *ver = "v" VS_VERSION;
            vs_text_draw(g->logical_w - vs_text_width(1, ver) - 8, (float)g->logical_h - 14, 1, 0x808080, ver);
        }
        const char *st = g->save_boot_status[0] ? g->save_boot_status : "Save: off";
        vs_text_draw(0.5f * (g->logical_w - vs_text_width(1, st)), 411, 1, strncmp(st, "SAVE FAILED", 11) == 0 ? 0xff4040 : 0x60a060, st);
        /* sound state in one line; the sound test is in Options */
        if (g->audio_status[0])
            vs_text_draw(0.5f * (g->logical_w - vs_text_width(1, g->audio_status)), 424, 1, strstr(g->audio_status, "NO ") ? 0xff4040 : 0x60a060, g->audio_status);
    } else if (g->screen == VS_SCREEN_SELECT) {
        vs_char_select_render(g);
    } else if (g->screen == VS_SCREEN_STAGE) {
        vs_stage_select_render(g);
    } else if (g->screen == VS_SCREEN_OPTIONS) {
        vs_options_render(g);
    } else if (g->screen == VS_SCREEN_POWERUP) {
        vs_shop_render(g);
    } else if (g->screen == VS_SCREEN_COLLECTION) {
        vs_collection_render(g);
    } else if (g->screen == VS_SCREEN_ACHIEVEMENTS) {
        vs_achievements_render(g);
    } else if (g->screen == VS_SCREEN_GAMEOVER) {
        if (g->quit_run) {
            const char *t = "RESULTS";
            vs_text_draw(0.5f * (g->logical_w - vs_text_width(3, t)), 120, 3, 0xffffff, t);
        } else vs_text_draw(g->logical_w * 0.5f - 80, 120, 3, 0xff4040, "GAME OVER");
        snprintf(buf, sizeof buf, "SURVIVED %02d:%02d   LEVEL %d   KILLS %u   GOLD %.0f   CHESTS %d", g->survived_seconds / 60, g->survived_seconds % 60, g->player.level, g->kills, g->coins, g->chests_opened);
        vs_text_draw(g->logical_w * 0.5f - 200, 170, 1.2f, 0xffffff, buf);
        /* new achievements of this run and what they unlocked (the original
         * cycles them in a popup; here they are listed) */
        if (g->new_achievement_count > 0) {
            snprintf(buf, sizeof buf, "NEW UNLOCKS: %d", g->new_achievement_count);
            vs_text_draw(0.5f * (g->logical_w - vs_text_width(1.5f, buf)), 200, 1.5f, 0xffd040, buf);
            for (int i = 0; i < g->new_achievement_count && i < 8; i++) {
                char desc[128], unlock[64];
                vs_save_achievement_text(g, g->new_achievements[i], desc, sizeof desc, unlock, sizeof unlock);
                snprintf(buf, sizeof buf, "%s  ->  %s", desc, unlock[0] ? unlock : "-");
                vs_text_draw(0.5f * (g->logical_w - vs_text_width(1, buf)), 222.0f + 13 * i, 1, 0xffffff, buf);
            }
            if (g->new_achievement_count > 8) vs_text_draw(0.5f * g->logical_w - 20, 222.0f + 13 * 8, 1, 0xa0a0a0, "...");
        }
        if (g->save_status[0]) vs_text_draw(0.5f * (g->logical_w - vs_text_width(1, g->save_status)), 335, 1, strncmp(g->save_status, "SAVE FAILED", 11) == 0 ? 0xff4040 : 0x80ff80, g->save_status);
        else if (!g->save_path[0]) vs_text_draw(0.5f * (g->logical_w - vs_text_width(1, "Saving is off")), 335, 1, 0xa0a0a0, "Saving is off");
        vs_text_draw(g->logical_w * 0.5f - 60, 360, 1.5f, 0xffffff, "PRESS START");
    }
    vs_plat_end_frame();
}

/* Dev aid (--hitboxes): square outline around every collider. Green =
 * enemies, yellow = bullets, cyan = player. */
static void outline(vs_game *g, float x, float y, float w, float h, uint32_t tint) {
    vs_frame white;
    if (vs_atlas_find(&g->atl_characters, "whiteDot.png", &white) != 0) return;
    vs_sprite s = {0};
    s.tex = g->textures[g->tex_characters]; s.sx = (int16_t)white.x; s.sy = (int16_t)white.y; s.sw = 1; s.sh = 1;
    s.alpha = 255; s.tint = tint;
    s.dx = x; s.dy = y; s.scale_x = w; s.scale_y = 1; vs_plat_draw(&s);
    s.dy = y + h - 1; vs_plat_draw(&s);
    s.dy = y; s.scale_x = 1; s.scale_y = h; vs_plat_draw(&s);
    s.dx = x + w - 1; vs_plat_draw(&s);
}

static void render_hitboxes(vs_game *g, float cam_x, float cam_y) {
    for (int i = 0; i < VS_MAX_ENEMIES; i++) {
        const vs_enemy *e = &g->enemies[i];
        if (!e->active || e->is_dead) continue;
        float cx = e->x + e->body_ox - cam_x, cy = e->y + e->body_oy - cam_y, r = e->body_r;
        outline(g, cx - r, cy - r, 2 * r, 2 * r, 0x00ff00);
        outline(g, e->x - cam_x - 1, e->y - cam_y - 1, 3, 3, 0xff00ff);   /* sprite anchor */
    }
    for (int i = 0; i < VS_MAX_BULLETS; i++) {
        const vs_bullet *b = &g->bullets[i];
        if (!b->active) continue;
        if (b->body_r > 0) outline(g, b->x - b->body_r - cam_x, b->y - b->body_r - cam_y, 2 * b->body_r, 2 * b->body_r, 0xffff00);
        else if (b->body_w > 0) outline(g, b->x - 0.5f * b->body_w - cam_x, b->y - 0.5f * b->body_h - cam_y, b->body_w, b->body_h, 0xffff00);
    }
    const vs_player *p = &g->player;
    outline(g, p->x + p->body_ox - p->body_r - cam_x, p->y + p->body_oy - p->body_r - cam_y, 2 * p->body_r, 2 * p->body_r, 0x00ffff);
}

/* Map bonus item (WEAPON pickup, line 1274): the item's icon floating 8 px
 * up and down (1 s, Sine), a shadow spot that breathes with it, a glow
 * whose colour cycles and fades in and out every 500 ms, and a puff of 10
 * sparkles every 200 ms (falling, 100–400 ms). The glow is additive. */
static float hash01(uint32_t v) { v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16; return (v & 0xffffff) / 16777216.0f; }
static void render_map_item(vs_game *g, const vs_pickup *pk, int index) {
    const float t = pk->anim_ms, dy = g->player.y;
    float s = 0.5f - 0.5f * cosf(3.14159265f * t / 1000.0f);   /* 0 → 1 → 0 over 2 s */
    float y = pk->y - 8 * s;
    vs_frame f;
    if (vs_atlas_find(&g->atl_items, "ShadowSpot.png", &f) == 0)
        vs_render_sprite(g, g->tex_items, &f, pk->x, pk->y, 0.5f, 0.5f, 1 - 0.25f * s, 0, 0, 0, 0xffffff, 128, pk->y - dy);
    if (vs_atlas_find(&g->atl_vfx, "round.png", &f) == 0) {
        float c = 0.1f * t / VS_TICK_MS;
        uint32_t r = (uint32_t)(255 * fabsf(sinf(c))), gg = (uint32_t)(255 * fabsf(cosf(0.5f * c))), b = (uint32_t)(200 + 55 * fabsf(cosf(c)));
        float a = 0.5f * (0.5f + 0.5f * cosf(3.14159265f * t / 500.0f));
        vs_render_blend(VS_BLEND_ADD);
        vs_render_sprite(g, g->tex_vfx, &f, pk->x, y, 0.5f, 0.5f, 1, 0, 0, 0, (r << 16) | (gg << 8) | b, (uint8_t)(255 * a), pk->y - dy);
        vs_render_blend(VS_BLEND_ALPHA);
    }
    vs_weapons_rec rec;
    if (vs_weapon_record(g, pk->weapon_type, 0, &rec) == 0 && VS_HAS(&rec, VS_WEAPONS_F_FRAMENAME) &&
        vs_atlas_find(&g->atl_items, vs_table_str(&g->weapons_table, rec.frameName), &f) == 0)
        vs_render_sprite(g, g->tex_items, &f, pk->x, y, 0.5f, 0.5f, 1, 0, 0, 0, 0xffffff, 255, 9 + pk->y - dy);
    for (int burst = 0; burst < 2; burst++) {
        int b = (int)(t / 200) - burst;
        if (b < 0) continue;
        float age = t - b * 200.0f;
        for (int k = 0; k < 10; k++) {
            uint32_t seed = (uint32_t)index * 7919u + (uint32_t)b * 104729u + (uint32_t)k * 31u;
            float life = 100 + 300 * hash01(seed), ang = 3.14159265f * hash01(seed + 1), sp = 25 + 25 * hash01(seed + 2);
            if (age >= life) continue;
            if (vs_atlas_find(&g->atl_vfx, hash01(seed + 3) < 0.5f ? "PfxColor1.png" : "PfxColor2.png", &f) != 0) continue;
            vs_render_sprite(g, g->tex_vfx, &f, pk->x + cosf(ang) * sp * age / 1000.0f, y + sinf(ang) * sp * age / 1000.0f, 0.5f, 0.5f, 1, 0, 0, 0,
                             0xffffff, (uint8_t)(255 * (1 - age / life)), 10 + pk->y - dy);
        }
    }
}

/* Bird bolt trail: the original's Rope of a small gradient texture along the
 * bolt's last 24 positions (one per frame). Drawn as 24 strips along the
 * flight curve, each with its share of the texture's columns (head first);
 * additive for Peachone, plain alpha for the others. Fades in over 200 ms
 * and out over 200 ms after the burst. */
static void render_bird_bolt(vs_game *g, const vs_bullet *b, int pool_index) {
    const vs_weapon *w = &g->weapons[b->weapon];
    float area = vs_weapon_p_area(g, w), ray = vs_bird_ray_ms(b->type);
    char name[32]; float trail_alpha = 0.4f;
    if (b->type == VS_WEAPONS_SILF) snprintf(name, sizeof name, "Gradient3_%dpx.png", area >= 2 ? 8 : area >= 1.5f ? 6 : 4);
    else if (b->type == VS_WEAPONS_SILF2) { snprintf(name, sizeof name, "Gradient4_%dpx.png", area >= 1.5f && area < 2 ? 6 : 4); trail_alpha = 1; }   /* the original never picks 8px here */
    else snprintf(name, sizeof name, "Prism%d_8px.png", 1 + pool_index % 9);
    vs_frame f;
    if (vs_atlas_find(&g->atl_vfx, name, &f) != 0 || f.w <= 0) return;
    float fade = b->age_ms < 200 ? b->age_ms / 200.0f : 1.0f;
    if (b->age_ms > ray) fade = 1 - (b->age_ms - ray) / 200.0f;
    if (fade <= 0) return;
    uint8_t alpha = (uint8_t)(255 * trail_alpha * fade);
    const float window = 24 * VS_TICK_MS;
    const int n = 24;
    float px = 0, py = 0;
    if (b->type == VS_WEAPONS_SILF) vs_render_blend(VS_BLEND_ADD);
    for (int k = 0; k <= n; k++) {
        float t = (b->age_ms - window * k / n) / ray, x, y;
        vs_bird_curve(b, t, &x, &y);
        if (k > 0) {
            float dx = px - x, dy = py - y, len = sqrtf(dx * dx + dy * dy);
            if (len > 0.01f) {
                vs_frame col = f; col.x = f.x + (k - 1) * f.w / n; col.w = 1;
                vs_render_stretched(g, g->tex_vfx, &col, 0.5f * (px + x), 0.5f * (py + y), len + 0.5f, (float)f.h,
                                    atan2f(y - py, x - px) * 180.0f / 3.14159265f + 180.0f, 0xffffff, alpha, 1e6f - 1);
            }
        }
        px = x; py = y;
    }
    vs_render_blend(VS_BLEND_ALPHA);
}


/* Clock Lancet's streak (Pierce1–5, 30 fps, scale 2 × 1, pale blue, fading
 * from 200 to 700 ms) and Laurel's ring (Shockwave2 at the player, tinted by
 * the shield count: 1 blue, 2 green, 3 yellow; alpha 0.3 ↔ 0.6 in 2 s,
 * a turn every 12 s). */
static void render_weapon_effects(vs_game *g) {
    const vs_player *p = &g->player;
    for (int i = 0; i < VS_MAX_WEAPONS; i++) {
        const vs_weapon *w = &g->weapons[i];
        if (!w->active) continue;
        vs_frame f;
        if (w->type == VS_WEAPONS_LANCET && w->lancet_ms >= 0 && w->lancet_ms < 700) {
            char name[32];
            int fr = 1 + (int)(w->lancet_ms * 30 / 1000.0f);
            snprintf(name, sizeof name, "Pierce%d.png", fr > 5 ? 5 : fr);
            if (vs_atlas_find(&g->atl_vfx, name, &f) == 0) {
                float a = w->lancet_ms < 200 ? 1 : 1 - (w->lancet_ms - 200) / 500.0f, len = 2.0f * f.w;
                vs_render_stretched(g, g->tex_vfx, &f, w->lancet_x + cosf(w->lancet_angle) * 0.5f * len, w->lancet_y + sinf(w->lancet_angle) * 0.5f * len,
                                    len, (float)f.h, w->lancet_angle * 180.0f / 3.14159265f, 0xcceeff, (uint8_t)(255 * a), (float)g->logical_h + 1e5f);
            }
        }
        if (w->type == VS_WEAPONS_LAUREL && p->shields > 0 && vs_atlas_find(&g->atl_vfx, "Shockwave2.png", &f) == 0) {
            uint32_t tint = p->shields == 1 ? 0x8888ff : p->shields == 2 ? 0x88ff88 : 0xffff00;
            float t = w->stats_lifetime, a = 0.3f + 0.3f * (0.5f - 0.5f * cosf(3.14159265f * t / 2000.0f));
            vs_render_sprite(g, g->tex_vfx, &f, p->x, p->y - 16, 0.5f, 0.5f, 1.25f, 0, 0, 360.0f * t / 12000.0f, tint, (uint8_t)(255 * a), -0.5f * g->logical_h);
        }
    }
}

/* The birds (on top, animated at 6 fps) and their target circles. */
static void render_birds(vs_game *g) {
    vs_frame dot;
    int have_dot = vs_atlas_find(&g->atl_characters, "whiteDot.png", &dot) == 0;
    for (int i = 0; i < VS_MAX_WEAPONS; i++) {
        const vs_weapon *w = &g->weapons[i];
        if (!w->active || (w->type != VS_WEAPONS_SILF && w->type != VS_WEAPONS_SILF2 && w->type != VS_WEAPONS_SILF3)) continue;
        int first = w->type == VS_WEAPONS_SILF ? 1 : w->type == VS_WEAPONS_SILF2 ? 4 : 7;
        char name[32]; vs_frame f;
        snprintf(name, sizeof name, "ProjectileBird%d.png", first + (int)(w->stats_lifetime / (1000.0f / 6.0f)) % 2);
        if (vs_atlas_find(&g->atl_vfx, name, &f) == 0)
            vs_render_sprite(g, g->tex_vfx, &f, w->bird_x, w->bird_y, 0.5f, 0.5f, 1, w->bird_flip, 0, 0, 0xffffff, 255, 1e6f + 1);
        if (!have_dot) continue;
        /* 1 px outline (Peachone blue, Ebony pink, Vandalier white), alpha 0.5 firing, 0.1 resting */
        uint32_t colour = w->type == VS_WEAPONS_SILF ? 0x8888ff : w->type == VS_WEAPONS_SILF2 ? 0xff88ff : 0xffffff;
        uint8_t a = w->zone_block ? 26 : 128;
        float r = vs_bird_zone_radius();
        for (int z = 0; z < (w->type == VS_WEAPONS_SILF3 ? 2 : 1); z++)
            for (int k = 0; k < 180; k++) {
                float t = 2 * 3.14159265f * k / 180;
                vs_render_stretched(g, g->tex_characters, &dot, w->zone_x[z] + cosf(t) * r, w->zone_y[z] + sinf(t) * r, 1.5f, 1.5f, 0, colour, a, -0.5f * g->logical_h);
            }
    }
}

void vs_game_render(vs_game *g) {
    if (g->screen != VS_SCREEN_GAME) { render_menu(g); return; }
    vs_player *p = &g->player;
    float cam_x = p->x - g->logical_w * 0.5f, cam_y = p->y - g->logical_h * 0.5f;
    vs_plat_begin_frame(0x000000);
    vs_plat_draw_tiled(g->textures[g->tex_bg], p->x, p->y, day_night_tint(g));
    vs_render_begin(g, cam_x, cam_y);
    vs_tilemap_render(g, cam_x, cam_y);

    for (int i = 0; i < VS_MAX_PICKUPS; i++) {
        vs_pickup *pk = &g->pickups[i];
        if (!pk->active) continue;
        const char *fname = "CoinGold.png";
        char nft[16];
        if (pk->type == VS_PICKUPS_GEM) fname = pk->value <= 2 ? "GemBlue.png" : pk->value <= 9 ? "GemGreen.png" : "GemRed.png";
        else if (pk->type == VS_PICKUPS_NFT) { snprintf(nft, sizeof nft, "Nft%d.png", 1 + (int)(g->tick / 6) % 4); fname = nft; }   /* 10 fps */
        else {
            int gi = vs_table_find_group_enum(&g->pickups_table, pk->type);
            if (gi >= 0) { vs_group grp; vs_table_group(&g->pickups_table, (uint32_t)gi, &grp); vs_pickups_rec r; vs_table_record(&g->pickups_table, grp.first, &r); if (VS_HAS(&r, VS_PICKUPS_F_FRAMENAME)) fname = vs_table_str(&g->pickups_table, r.frameName); }
        }
        if (pk->type == VS_PICKUPS_WEAPON) { render_map_item(g, pk, (int)i); continue; }
        vs_frame f;
        if (vs_atlas_find(&g->atl_items, fname, &f) != 0) continue;
        float depth = pk->type == VS_PICKUPS_GEM ? pk->y - p->y - g->logical_h : 9 + pk->y - p->y;
        vs_render_sprite(g, g->tex_items, &f, pk->x, pk->y, 0.5f, 0.5f, 1.0f, 0, 0, 0, 0xffffff, 255, depth);
    }

    for (int i = 0; i < VS_MAX_DESTRUCTIBLES; i++) {
        vs_destructible *d = &g->destructibles[i];
        if (!d->active) continue;
        char name[48]; vs_frame f;
        snprintf(name, sizeof name, "%s%d.png", d->frame_prefix, 1 + (int)(g->tick * VS_TICK_MS / 100) % 3);
        if (vs_atlas_find(&g->atl_items, name, &f) != 0) continue;
        vs_render_sprite(g, g->tex_items, &f, d->x, d->y, 0.5f, 1.0f, 1.0f, 0, 0, 0, d->blink_ms > 0 ? 0xffffff : 0xffffff, d->is_dead ? 128 : 255, d->y - p->y);
    }
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
        uint32_t tint = e->time_stopped ? 0x0000ff : (e->receiving_damage && g->save.flashing_vfx ? e->hit_tint : 0xffffff);
        vs_render_sprite(g, g->tex_enemies, &f, e->x, e->y, 0.5f, 1.0f, e->scale, e->dir_x > 0, 0, angle, tint, (uint8_t)(e->alpha * 255), e->y - p->y);
        if (e->self_destruct && e->sd_triggered && !e->dying && vs_atlas_find(&g->atl_vfx, "sPFX_ring_64.png", &f) == 0) {
            /* alert: additive red ring, alpha 1 → 0 and scale 1 → 0.9, back and forth */
            float len = 400.0f / powf(1.1f, (float)e->sd_cycle), ph = e->sd_phase_ms / len, u = ph < 0.5f ? 2 * ph : 2 - 2 * ph;
            vs_render_blend(VS_BLEND_ADD);
            vs_render_sprite(g, g->tex_vfx, &f, e->x, e->y, 0.5f, 0.75f, 1 - 0.1f * u, 0, 0, 0, 0xff0000, (uint8_t)(255 * (1 - u)), e->y - p->y + 0.5f);
            vs_render_blend(VS_BLEND_ALPHA);
        }
    }

    for (int i = 0; i < VS_MAX_WEAPONS; i++) {
        vs_weapon *w = &g->weapons[i];
        if (!w->active) continue;
        vs_frame f;
        if (w->type == VS_WEAPONS_GARLIC && vs_atlas_find(&g->atl_vfx, "Rings3.png", &f) == 0) {
            vs_render_blend(VS_BLEND_ADD);
            vs_render_sprite(g, g->tex_vfx, &f, p->x, p->y, 0.5f, 0.5f, 2.0f * vs_weapon_p_area(g, w), 0, 0, fmodf(g->tick * VS_TICK_MS / 6000.0f * 360.0f, 360.0f), 0xffffff, 60, -g->logical_h * 0.5f);
            vs_render_blend(VS_BLEND_ALPHA);
        }
        if (w->type == VS_WEAPONS_VORTEX && vs_atlas_find(&g->atl_vfx, "circle.png", &f) == 0) {
            /* Soul Eater: a black disc (alpha 0.6) and an inner one that pulses
             * 0.1..0.6 every second and shrinks to nothing every 6 s */
            float full = 2.0f * vs_weapon_p_area(g, w) * 36.0f / 256.0f;
            float ms = (float)g->tick * VS_TICK_MS;
            float pulse = fmodf(ms, 2200.0f), a = pulse < 1000 ? 0.1f + 0.5f * pulse / 1000 : pulse < 1100 ? 0.6f : pulse < 2100 ? 0.6f - 0.5f * (pulse - 1100) / 1000 : 0.1f;
            float inner = full * (1.0f - fmodf(ms, 6000.0f) / 6000.0f);
            vs_render_sprite(g, g->tex_vfx, &f, p->x, p->y, 0.5f, 0.5f, full, 0, 0, 0, 0x000000, 153, -g->logical_h * 0.5f);
            vs_render_sprite(g, g->tex_vfx, &f, p->x, p->y, 0.5f, 0.5f, inner, 0, 0, 0, 0x000000, (uint8_t)(255 * a), -g->logical_h * 0.5f + 0.01f);
        }
    }
    for (int i = 0; i < VS_MAX_BULLETS; i++) {
        vs_bullet *b = &g->bullets[i];
        if (!b->active || !b->visible) continue;
        const char *fname; float depth = 1; int tex = g->tex_vfx; const vs_atlas *atl = &g->atl_vfx;
        switch (b->type) {
        case VS_WEAPONS_WHIP: case VS_WEAPONS_VAMPIRICA: fname = "slash.png"; break;
        case VS_WEAPONS_SCYTHE: fname = "ProjectileScythe.png"; break;
        case VS_WEAPONS_HELLFIRE: fname = "ProjectileHellfireLarge.png"; depth = b->y - p->y + g->logical_h; break;
        case VS_WEAPONS_HEAVENSWORD: fname = "Sword.png"; depth = b->y - p->y + 2.0f * g->logical_h; break;
        case VS_WEAPONS_MAGIC_MISSILE: case VS_WEAPONS_HOLY_MISSILE: fname = "ProjectileHoly1.png"; break;
        case VS_WEAPONS_KNIFE: case VS_WEAPONS_THOUSAND: fname = "ProjectileKnife3.png"; break;
        case VS_WEAPONS_AXE: fname = "ProjectileAxe1.png"; break;
        case VS_WEAPONS_FIREBALL: fname = "ProjectileFireball2.png"; break;
        case VS_WEAPONS_CROSS: fname = "ProjectileCross2.png"; depth = 2.0f * g->logical_h; break;
        case VS_WEAPONS_HOLYBOOK: fname = "HolyBook.png"; break;
        case VS_WEAPONS_VESPERS: fname = "Page2.png"; break;
        case VS_WEAPONS_LIGHTNING: fname = "Lightning3.png"; depth = 1e6f; break;
        case VS_WEAPONS_SILF: case VS_WEAPONS_SILF3: fname = "HitWhite1.png"; depth = 1e6f; break;
        case VS_WEAPONS_SILF2: fname = "HitBlack1.png"; depth = 1e6f; break;
        case VS_WEAPONS_BONE: fname = "ProjectileBone2.png"; break;
        case VS_WEAPONS_NDUJA: {
            /* Nduja Fritta: an additive filled circle (red / orange) growing
             * with the hit circle, and a flame flicker */
            static const uint32_t fire[3] = { 0xff0000, 0xff6600, 0xff4400 };
            vs_frame c;
            if (vs_atlas_find(&g->atl_vfx, "circle.png", &c) == 0 && b->scale > 0.5f) {
                vs_render_blend(VS_BLEND_ADD);
                vs_render_stretched(g, g->tex_vfx, &c, b->x, b->y, 2 * b->scale, 2 * b->scale, 0, fire[b->state % 3], b->alpha, b->y - p->y);
                vs_render_blend(VS_BLEND_ALPHA);
            }
            if (vs_atlas_find(&g->atl_vfx, (g->tick + b->index) % 2 ? "Flame2.png" : "PfxYellow.png", &c) == 0)
                vs_render_sprite(g, g->tex_vfx, &c, b->x, b->y, 0.5f, 0.5f, 1.0f, 0, 0, (float)((g->tick * 37 + b->index * 53) % 360), 0xffffff, b->alpha, b->y - p->y + 1);
            continue;
        }
        case VS_WEAPONS_DIAMOND: fname = "Diamond2.png"; tex = g->tex_items; atl = &g->atl_items; break;
        case VS_WEAPONS_HOLYWATER:
            if (b->state == 0) { fname = "HolyWater.png"; tex = g->tex_items; atl = &g->atl_items; depth = (float)g->logical_h; }
            else { fname = "circle.png"; depth = -0.5f * g->logical_h; }
            break;
        default: fname = "ProjectileHoly1.png"; break;
        }
        vs_frame f;
        if (vs_atlas_find(atl, fname, &f) != 0) continue;
        float scale = b->scale;
        uint32_t tint = 0xffffff; uint8_t alpha = b->alpha;
        if ((b->type == VS_WEAPONS_WHIP || b->type == VS_WEAPONS_VAMPIRICA) && b->age_ms < 100) scale *= b->age_ms / 100.0f;
        if (b->type == VS_WEAPONS_VAMPIRICA) {
            /* Bloody Tear: near-black slash over a red ghost (+4, +2) and an additive purple one (-4, -2) */
            vs_render_sprite(g, tex, &f, b->x + 4, b->y + 2, 0.5f, 0.5f, scale, b->flip_x, b->flip_y, b->angle, 0xff0000, alpha, depth);
            vs_render_blend(VS_BLEND_ADD);
            vs_render_sprite(g, tex, &f, b->x - 4, b->y - 2, 0.5f, 0.5f, scale, b->flip_x, b->flip_y, b->angle, 0x660066, alpha, depth);
            vs_render_blend(VS_BLEND_ALPHA);
            tint = 0x110011;
        }
        if (b->type == VS_WEAPONS_HOLYWATER && b->state == 1) { scale = b->body_r * 2 / 256.0f; tint = 0x4040ff; alpha = 60; }
        if (b->type == VS_WEAPONS_SILF || b->type == VS_WEAPONS_SILF2 || b->type == VS_WEAPONS_SILF3) {
            render_bird_bolt(g, b, (int)(b - g->bullets));
            float ray = vs_bird_ray_ms(b->type), e = b->age_ms - ray;
            if (e >= 0) {   /* burst: grows and fades in 120 ms; hidden without flashing VFX */
                if (!g->save.flashing_vfx || e >= 120) continue;
                alpha = (uint8_t)(255 * (1 - e / 120.0f));
            }
        }
        if (b->type == VS_WEAPONS_LIGHTNING) {
            /* The bullet itself is invisible. The bolt is a 17 px strip of
             * Lightning3.png along its path, from where it started (above the
             * screen) to where it is now; on impact a faint circle in one of
             * four colours grows to radius 8 × area over the 60 ms hit. */
            float len = b->y - b->ay;
            if (len > 1) vs_render_stretched(g, g->tex_vfx, &f, b->x, b->ay + 0.5f * len, len, (float)f.h, 90, 0xffffff, 255, 1e6f);
            if (b->state == 1 && g->save.flashing_vfx && vs_atlas_find(&g->atl_vfx, "circle.png", &f) == 0) {
                static const uint32_t colours[4] = { 0x00ffff, 0x0000ff, 0x0088ff, 0xffffff };
                float t = (b->age_ms - b->f1) / 60.0f, d = 32.0f * b->scale * (t < 1 ? t : 1);   /* scale = area / 2 */
                vs_render_stretched(g, g->tex_vfx, &f, b->x, b->y, d, d, 0, colours[b->index & 3], 38, b->y - p->y - 0.5f * g->logical_h);
            }
            continue;
        }
        if (b->type == VS_WEAPONS_HOLYWATER && b->state == 1) vs_render_blend(VS_BLEND_ADD);   /* the pool is additive */
        vs_render_sprite(g, tex, &f, b->x, b->y, 0.5f, 0.5f, scale, b->flip_x, b->flip_y, b->angle, tint, alpha, depth);
        vs_render_blend(VS_BLEND_ALPHA);
    }

    {
        char name[64];
        int frame = 1;
        if (p->walking_frames > 0) frame = 1 + (int)(p->anim_t / 125.0f) % p->walking_frames;
        snprintf(name, sizeof name, "%s%02d.png", p->sprite_prefix, frame);
        vs_frame f;
        if (vs_atlas_find(&g->atl_characters, name, &f) == 0) {
            uint32_t tint = p->receiving_damage ? 0xff0000 : (vs_player_is_invul(g) ? 0xffffbb : 0xffffff);
            if (p->is_dead) tint = 0xff0000;
            vs_render_sprite(g, g->tex_characters, &f, p->x, p->y, 0.5f, 1.0f, 1.0f, p->facing_left, 0, 0, tint, 255, 0);
        }
    }
    render_birds(g);
    render_weapon_effects(g);
    vs_render_explosions(g);
    vs_fx_render(g, cam_x, cam_y);
    vs_render_end(g);
    vs_render_timestop_flash(g);
    if (g->debug_hitboxes) render_hitboxes(g, cam_x, cam_y);
    render_hud(g);
    if (g->levelup.pending) render_levelup(g);
    if (g->treasure.active) vs_treasure_ui_render(g);
    if (g->item_found.active) vs_item_found_render(g);
    if (g->paused) vs_options_render(g);
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
