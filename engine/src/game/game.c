#include "game/game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/vs_audio.h"
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

static int open_table(vs_game *g, const char *name, vs_table *t, const char *fields) {
    if (vs_pack_table(g->pack, name, t) != 0 || vs_table_check_fields(t, fields) != 0) { vs_plat_log("table %s missing or layout mismatch", name); return -1; }
    return 0;
}

static void build_character_list(vs_game *g) {
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
    vs_audio_set_volumes(g->save.sound_volume, g->save.music_volume);
}

int vs_game_init(vs_game *g, const vs_pack *pack, int lw, int lh, uint64_t seed) {
    int auto_pick = g->auto_pick, menu_stage = g->menu_stage, debug_hitboxes = g->debug_hitboxes, unlock_all = g->unlock_all, show_gameover = g->show_gameover;
    int selected_character = g->selected_character;
    char save_path[sizeof g->save_path], audio_status[sizeof g->audio_status];
    memcpy(save_path, g->save_path, sizeof save_path);
    memcpy(audio_status, g->audio_status, sizeof audio_status);
    memset(g, 0, sizeof *g);
    g->auto_pick = auto_pick; g->menu_stage = menu_stage; g->debug_hitboxes = debug_hitboxes; g->unlock_all = unlock_all; g->show_gameover = show_gameover;
    g->selected_character = selected_character;
    memcpy(g->audio_status, audio_status, sizeof audio_status);
    g->pack = pack;
    g->logical_w = lw;
    g->logical_h = lh;
    g->seed = seed;

    g->base_player_px_speed = table_constant(pack, "_basePlayerPxSpeed", 82.5f);
    g->base_enemy_speed = table_constant(pack, "_baseEnemySpeed", 0.231f);
    g->base_projectile_speed = table_constant(pack, "_baseProjectileSpeed", 165.0f);

    if (open_table(g, "characters", &g->characters_table, VS_CHARACTERS_FIELD_NAMES)) return -1;
    if (open_table(g, "enemies", &g->enemies_table, VS_ENEMIES_FIELD_NAMES)) return -1;
    if (open_table(g, "hitvfx", &g->hitvfx_table, VS_HITVFX_FIELD_NAMES)) return -1;
    if (open_table(g, "weapons", &g->weapons_table, VS_WEAPONS_FIELD_NAMES)) return -1;
    if (open_table(g, "pickups", &g->pickups_table, VS_PICKUPS_FIELD_NAMES)) return -1;
    if (open_table(g, "destructibles", &g->destructibles_table, VS_DESTRUCTIBLES_FIELD_NAMES)) return -1;
    if (open_table(g, "achievements", &g->achievements_table, VS_ACHIEVEMENTS_FIELD_NAMES)) return -1;
    if (open_table(g, "stages", &g->stages_table, VS_STAGES_FIELD_NAMES)) return -1;
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
    {
        int fi = vs_pack_find(pack, "FNT", "damNum-export");
        if (fi < 0 || vs_pack_font(pack, fi, &g->damage_font) != 0) return -1;
        g->tex_damage = load_texture(g, vs_pack_entry_name(pack, g->damage_font.tex_entry));
    }
    if (vs_text_init() != 0) return -1;
    sound_init(g);
    vs_sfx(g, VS_SND_BGM_INTRO, 0.7f, 0, 1, 0, 0);   /* after the start-up warning in the original */
    build_character_list(g);
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

static void menu_update(vs_game *g, const vs_pad *pad) {
    uint32_t pressed = pad->buttons & ~g->prev_buttons;
    g->prev_buttons = pad->buttons;
    switch (g->screen) {
    case VS_SCREEN_TITLE:
        if (pressed & (VS_BTN_A | VS_BTN_START)) { g->screen = VS_SCREEN_SELECT; g->menu_sel = 0; vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0); }
        if (pressed & VS_BTN_Y) vs_sfx(g, VS_SND_LEVELUP, 1.5f, 0, 1, 0, 0);   /* sound test */
        if (pressed & VS_BTN_X) vs_audio_test_tone();                         /* tone, no decoder */
        if (pressed & VS_BTN_B) g->audio_swap = vs_plat_audio_toggle_swap();  /* byte-order test */
        break;
    case VS_SCREEN_SELECT: {
        int n = g->menu_char_count;
        if (pressed & VS_BTN_UP) { g->menu_sel = (g->menu_sel + n - 1) % n; vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0); }
        if (pressed & VS_BTN_DOWN) { g->menu_sel = (g->menu_sel + 1) % n; vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0); }
        if ((pressed & (VS_BTN_LEFT | VS_BTN_RIGHT)) && vs_stage_selectable(g, VS_STAGES_LIBRARY)) { g->menu_stage ^= 1; vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0); }
        if (pressed & VS_BTN_B) { g->screen = VS_SCREEN_TITLE; vs_sfx(g, VS_SND_CLICKOUT, -1, -400, 1, 0, 0); }
        if (pressed & (VS_BTN_A | VS_BTN_START)) {
            vs_sfx(g, VS_SND_CLICKIN, -1, 200, 1, 0, 0);
            g->selected_character = g->menu_char_list[g->menu_sel];
            g->selected_stage = g->menu_stage ? VS_STAGES_LIBRARY : VS_STAGES_FOREST;
            g->seed = g->seed * 6364136223846793005ull + 1442695040888963407ull;
            if (vs_game_start_run(g, g->selected_character, g->selected_stage) != 0) g->screen = VS_SCREEN_TITLE;
        }
        break;
    }
    case VS_SCREEN_GAMEOVER:
        if (pressed & (VS_BTN_A | VS_BTN_START)) { g->screen = VS_SCREEN_TITLE; vs_audio_music_stop(); vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0); }
        break;
    default: break;
    }
}

void vs_game_update(vs_game *g, const vs_pad *pad) {
    if (g->screen != VS_SCREEN_GAME) { menu_update(g, pad); return; }
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

static const char *character_name(vs_game *g, int type, char *buf, size_t n) {
    int gi = vs_table_find_group_enum(&g->characters_table, type);
    if (gi < 0) return "?";
    vs_group grp; vs_table_group(&g->characters_table, (uint32_t)gi, &grp);
    vs_characters_rec c; vs_table_record(&g->characters_table, grp.first, &c);
    snprintf(buf, n, "%s %s", vs_table_str(&g->characters_table, c.charName), VS_HAS(&c, VS_CHARACTERS_F_SURNAME) ? vs_table_str(&g->characters_table, c.surname) : "");
    return buf;
}

/* Draws `frame` from the UI atlas stretched over (x, y, w, h) as a nine-slice
 * with corners of `c` px (the original's NineSlice configs). */
static void nine_slice(vs_game *g, const char *frame, float x, float y, float w, float h, int c, uint32_t tint) {
    vs_frame f;
    if (vs_atlas_find(&g->atl_ui, frame, &f) != 0) return;
    /* whole pixels, or the stretched rows leave 1 px seams */
    float x1 = floorf(x + w + 0.5f), y1 = floorf(y + h + 0.5f);
    x = floorf(x + 0.5f); y = floorf(y + 0.5f); w = x1 - x; h = y1 - y;
    int sx[3] = { f.x, f.x + c, f.x + f.w - c }, sw[3] = { c, f.w - 2 * c, c };
    int sy[3] = { f.y, f.y + c, f.y + f.h - c }, sh[3] = { c, f.h - 2 * c, c };
    float dx[3] = { x, x + c, x + w - c }, dw[3] = { (float)c, w - 2 * c, (float)c };
    float dy[3] = { y, y + c, y + h - c }, dh[3] = { (float)c, h - 2 * c, (float)c };
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            if (dw[i] <= 0 || dh[j] <= 0) continue;
            vs_sprite s = {0};
            s.tex = g->textures[g->tex_ui];
            s.sx = (int16_t)sx[i]; s.sy = (int16_t)sy[j]; s.sw = (int16_t)sw[i]; s.sh = (int16_t)sh[j];
            s.dx = dx[i]; s.dy = dy[j]; s.scale_x = dw[i] / sw[i]; s.scale_y = dh[j] / sh[j];
            s.tint = tint; s.alpha = 255;
            vs_plat_draw(&s);
        }
    }
}

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
        const char *name = "?", *level = "", *desc = "", *frame = NULL;
        uint32_t level_tint = 0xffffff, bg_tint = 0xffffff;
        char level_buf[32];
        if (lu->items_mode) {
            int gi = vs_table_find_group_enum(&g->pickups_table, lu->item_options[i]);
            if (gi >= 0) {
                vs_group grp; vs_table_group(&g->pickups_table, (uint32_t)gi, &grp);
                vs_pickups_rec r; vs_table_record(&g->pickups_table, grp.first, &r);
                if (VS_HAS(&r, VS_PICKUPS_F_NAME)) name = vs_table_str(&g->pickups_table, r.name);
                if (VS_HAS(&r, VS_PICKUPS_F_DESCRIPTION)) desc = vs_table_str(&g->pickups_table, r.description);
                if (VS_HAS(&r, VS_PICKUPS_F_FRAMENAME)) frame = vs_table_str(&g->pickups_table, r.frameName);
            }
        } else {
            int type = lu->options[i], slot = vs_weapon_find(g, type);
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
        }
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
        if (!g->weapons[i].active) continue;
        snprintf(buf, sizeof buf, "%s %d", vs_weapon_name(g, g->weapons[i].type), g->weapons[i].level);
        vs_text_draw(4, wy, 1, g->weapons[i].is_power_up ? 0xc0ffc0 : 0xc0c0ff, buf);
        wy += 9;
    }
    if (g->game_over) vs_text_draw(g->logical_w * 0.5f - 80, g->logical_h * 0.5f - 10, 3, 0xff4040, "GAME OVER");
}

static void render_menu(vs_game *g) {
    char buf[256];
    vs_plat_begin_frame(0x000000);
    if (g->screen == VS_SCREEN_TITLE || g->screen == VS_SCREEN_SELECT) {
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
    if (g->screen == VS_SCREEN_TITLE || g->screen == VS_SCREEN_SELECT) {
        snprintf(buf, sizeof buf, "GOLD %.0f   ACHIEVEMENTS %d/%d", g->save.coins, vs_save_achievement_count(g), VS_ACHIEVEMENTS_COUNT);
        vs_text_draw(g->logical_w - vs_text_width(1, buf) - 8, 6, 1, 0xffd040, buf);
    }
    if (g->screen == VS_SCREEN_TITLE) {
        vs_text_draw(g->logical_w * 0.5f - 60, 260, 1.5f, 0xffffff, "PRESS START");
        vs_text_draw(g->logical_w * 0.5f - 140, 398, 1, 0x808080, "unofficial demake - arrows/WASD move, enter/A select, esc/B back");
        const char *st = g->save_boot_status[0] ? g->save_boot_status : "Save: off";
        vs_text_draw(0.5f * (g->logical_w - vs_text_width(1, st)), 411, 1, strncmp(st, "SAVE FAILED", 11) == 0 ? 0xff4040 : 0x60a060, st);
        if (g->audio_status[0]) {
            /* live diagnostics: decoded seconds, peak of the latest mix, frames the device played */
            snprintf(buf, sizeof buf, "%s | %.0f s decoded | level %d (max %d) | played %lu", g->audio_status, vs_audio_decoded_seconds(), vs_audio_last_peak(), vs_audio_max_peak(), (unsigned long)vs_plat_audio_played());
            vs_text_draw(0.5f * (g->logical_w - vs_text_width(1, buf)), 424, 1, strstr(g->audio_status, "NO ") ? 0xff4040 : 0x60a060, buf);
            snprintf(buf, sizeof buf, "Sound test: Y = game sound, X = plain tone, B = byte order (now %s)", g->audio_swap ? "SWAPPED" : "normal");
            vs_text_draw(0.5f * (g->logical_w - vs_text_width(1, buf)), 434, 1, 0x808080, buf);
            vs_audio_debug("LevelUp", buf, sizeof buf);
            vs_text_draw(0.5f * (g->logical_w - vs_text_width(1, buf)), 444, 1, 0x808080, buf);
        }
    } else if (g->screen == VS_SCREEN_SELECT) {
        vs_text_draw(60, 240, 1.5f, 0xffffff, "CHARACTER");
        for (int i = 0; i < g->menu_char_count; i++) {
            char nm[64];
            snprintf(buf, sizeof buf, "%s %s", i == g->menu_sel ? ">" : " ", character_name(g, g->menu_char_list[i], nm, sizeof nm));
            vs_text_draw(60, (float)(260 + 10 * i), 1, i == g->menu_sel ? 0xffff80 : 0xffffff, buf);
        }
        vs_text_draw(420, 240, 1.5f, 0xffffff, "STAGE (left/right)");
        snprintf(buf, sizeof buf, "%s %s", g->menu_stage == 0 ? ">" : " ", vs_stage_name(g, VS_STAGES_FOREST));
        vs_text_draw(420, 260, 1, g->menu_stage == 0 ? 0xffff80 : 0xffffff, buf);
        if (vs_stage_selectable(g, VS_STAGES_LIBRARY)) snprintf(buf, sizeof buf, "%s %s", g->menu_stage == 1 ? ">" : " ", vs_stage_name(g, VS_STAGES_LIBRARY));
        else snprintf(buf, sizeof buf, "  ??? (reach LV 20 in %s)", vs_stage_name(g, VS_STAGES_FOREST));
        vs_text_draw(420, 270, 1, g->menu_stage == 1 ? 0xffff80 : vs_stage_selectable(g, VS_STAGES_LIBRARY) ? 0xffffff : 0x808080, buf);
    } else if (g->screen == VS_SCREEN_GAMEOVER) {
        vs_text_draw(g->logical_w * 0.5f - 80, 120, 3, 0xff4040, "GAME OVER");
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
        if (pk->type == VS_PICKUPS_GEM) fname = pk->value <= 2 ? "GemBlue.png" : pk->value <= 9 ? "GemGreen.png" : "GemRed.png";
        else {
            int gi = vs_table_find_group_enum(&g->pickups_table, pk->type);
            if (gi >= 0) { vs_group grp; vs_table_group(&g->pickups_table, (uint32_t)gi, &grp); vs_pickups_rec r; vs_table_record(&g->pickups_table, grp.first, &r); if (VS_HAS(&r, VS_PICKUPS_F_FRAMENAME)) fname = vs_table_str(&g->pickups_table, r.frameName); }
        }
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
        uint32_t tint = e->time_stopped ? 0x0000ff : (e->receiving_damage ? e->hit_tint : 0xffffff);
        vs_render_sprite(g, g->tex_enemies, &f, e->x, e->y, 0.5f, 1.0f, e->scale, e->dir_x > 0, 0, angle, tint, (uint8_t)(e->alpha * 255), e->y - p->y);
    }

    for (int i = 0; i < VS_MAX_WEAPONS; i++) {
        vs_weapon *w = &g->weapons[i];
        if (!w->active) continue;
        vs_frame f;
        if (w->type == VS_WEAPONS_GARLIC && vs_atlas_find(&g->atl_vfx, "Rings3.png", &f) == 0)
            vs_render_sprite(g, g->tex_vfx, &f, p->x, p->y, 0.5f, 0.5f, 2.0f * vs_weapon_p_area(g, w), 0, 0, fmodf(g->tick * VS_TICK_MS / 6000.0f * 360.0f, 360.0f), 0xffffff, 60, -g->logical_h * 0.5f);
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
            /* Bloody Tear: near-black slash over a red ghost (+4, +2) and a purple one (-4, -2) */
            vs_render_sprite(g, tex, &f, b->x + 4, b->y + 2, 0.5f, 0.5f, scale, b->flip_x, b->flip_y, b->angle, 0xff0000, alpha, depth);
            vs_render_sprite(g, tex, &f, b->x - 4, b->y - 2, 0.5f, 0.5f, scale, b->flip_x, b->flip_y, b->angle, 0x660066, alpha, depth);
            tint = 0x110011;
        }
        if (b->type == VS_WEAPONS_HOLYWATER && b->state == 1) { scale = b->body_r * 2 / 256.0f; tint = 0x4040ff; alpha = 60; }
        if (b->type == VS_WEAPONS_LIGHTNING) { /* bolt from the top of the screen to the strike point */ vs_frame bolt; if (vs_atlas_find(&g->atl_vfx, "Lightning3.png", &bolt) == 0) { float len = b->y - b->ay; if (len > 1) vs_render_sprite(g, g->tex_vfx, &bolt, b->x, b->ay, 0.5f, 0.0f, len / (float)bolt.src_h, 0, 0, 0, 0xffffff, 255, 1e6f); } if (b->state == 0) continue; fname = "HitRed2.png"; if (vs_atlas_find(&g->atl_vfx, fname, &f) != 0) continue; }
        vs_render_sprite(g, tex, &f, b->x, b->y, 0.5f, 0.5f, scale, b->flip_x, b->flip_y, b->angle, tint, alpha, depth);
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
    vs_fx_render(g, cam_x, cam_y);
    vs_render_end(g);
    if (g->debug_hitboxes) render_hitboxes(g, cam_x, cam_y);
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
