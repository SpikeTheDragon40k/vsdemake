/* Pause screen (during a run) and options screen (from the title), after the
 * original's PauseScene (line 8802): sound and music volume sliders, the
 * Flashing VFX and Damage Numbers switches, Quit and Resume. The original's
 * "Visible joystick" switch is for touch screens and is left out; the
 * demake adds a sound test row (diagnostics for new platforms).
 * Options are written to the save when the screen closes. */
#include "game/game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/vs_audio.h"
#include "core/vs_text.h"

enum { ROW_SOUNDS, ROW_MUSIC, ROW_FLASH, ROW_DAMAGE, ROW_TEST, ROW_BUTTONS, ROW_COUNT };
enum { BTN_QUIT, BTN_RESUME };      /* button row in the pause screen */

#define VOLUME_STEP 0.05f
#define REPEAT_DELAY_MS 300.0f
#define REPEAT_EVERY_MS 50.0f

void vs_options_open(vs_game *g) {
    g->opt_sel = ROW_BUTTONS;
    g->opt_btn = BTN_RESUME;
    g->opt_hold_ms = 0;
    g->opt_dirty = 0;
}

static void close_screen(vs_game *g) {
    if (g->opt_dirty && g->save_path[0]) vs_save_write(g);
    g->opt_dirty = 0;
    if (g->paused) g->paused = 0;
    else g->screen = VS_SCREEN_TITLE;
}

/* The original's QuitFromPauseMenu: the run ends and the results screen
 * (PostGameScene, which also checks achievements) follows; music stops. */
static void quit_run(vs_game *g) {
    close_screen(g);
    vs_sfx(g, VS_SND_CLICKOUT, -1, 0, 1, 0, 0);
    vs_save_run_end(g);
    vs_audio_music_stop();
    g->quit_run = 1;
    g->screen = VS_SCREEN_GAMEOVER;
}

static void set_volume(vs_game *g, float *v, float delta, int is_sound) {
    float nv = *v + delta;
    nv = nv < 0 ? 0 : nv > 1 ? 1 : nv;
    nv = floorf(nv / VOLUME_STEP + 0.5f) * VOLUME_STEP;   /* no float drift: exact 5 % steps */
    if (nv == *v) return;
    *v = nv;
    g->opt_dirty = 1;
    vs_audio_set_volumes(g->save.sound_volume, g->save.music_volume);
    if (is_sound) vs_sfx(g, VS_SND_GEM, 1.0f, 0, 1, 60, 1);   /* hear the new level */
}

void vs_options_input(vs_game *g, const vs_pad *pad) {
    uint32_t held = pad->buttons, pressed = held & ~g->prev_buttons;
    g->prev_buttons = held;
    if (pressed & (VS_BTN_B | VS_BTN_START)) { vs_sfx(g, VS_SND_CLICKOUT, -1, 0, 1, 0, 0); close_screen(g); return; }
    if (pressed & (VS_BTN_UP | VS_BTN_DOWN)) {
        g->opt_sel = (g->opt_sel + ((pressed & VS_BTN_UP) ? ROW_COUNT - 1 : 1)) % ROW_COUNT;
        g->opt_hold_ms = 0;
        vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0);
        return;
    }
    /* left/right: one step on press, then repeats while held */
    int dir = 0;
    if (pressed & (VS_BTN_LEFT | VS_BTN_RIGHT)) { dir = (pressed & VS_BTN_LEFT) ? -1 : 1; g->opt_hold_ms = 0; }
    else if (held & (VS_BTN_LEFT | VS_BTN_RIGHT)) {
        g->opt_hold_ms += VS_TICK_MS;
        if (g->opt_hold_ms >= REPEAT_DELAY_MS) { g->opt_hold_ms -= REPEAT_EVERY_MS; dir = (held & VS_BTN_LEFT) ? -1 : 1; }
    }
    int repeat = !(pressed & (VS_BTN_LEFT | VS_BTN_RIGHT));
    switch (g->opt_sel) {
    case ROW_SOUNDS: if (dir) set_volume(g, &g->save.sound_volume, dir * VOLUME_STEP, 1); break;
    case ROW_MUSIC: if (dir) set_volume(g, &g->save.music_volume, dir * VOLUME_STEP, 0); break;
    case ROW_FLASH: case ROW_DAMAGE:
        if ((dir && !repeat) || (pressed & VS_BTN_A)) {
            int *v = g->opt_sel == ROW_FLASH ? &g->save.flashing_vfx : &g->save.damage_numbers;
            *v = !*v;
            g->opt_dirty = 1;
            vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0);
        }
        break;
    case ROW_TEST:
        if (pressed & VS_BTN_A) vs_sfx(g, VS_SND_LEVELUP, 1.5f, 0, 1, 0, 0);   /* a game sound */
        if (pressed & VS_BTN_X) vs_audio_test_tone();                         /* plain tone, no decoder */
        if (pressed & VS_BTN_Y) g->audio_swap = vs_plat_audio_toggle_swap();  /* output byte order */
        break;
    case ROW_BUTTONS:
        if (g->paused && dir && !repeat) { g->opt_btn ^= 1; vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0); }
        if (pressed & VS_BTN_A) {
            if (g->paused && g->opt_btn == BTN_QUIT) quit_run(g);
            else { vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0); close_screen(g); }
        }
        break;
    }
}

static void ui_frame(vs_game *g, const char *name, float x, float y, float scale) {
    vs_frame f;
    if (vs_atlas_find(&g->atl_ui, name, &f) != 0) return;
    vs_sprite s = {0};
    s.tex = g->textures[g->tex_ui]; s.sx = (int16_t)f.x; s.sy = (int16_t)f.y; s.sw = (int16_t)f.w; s.sh = (int16_t)f.h;
    s.dx = x; s.dy = y; s.scale_x = s.scale_y = scale; s.tint = 0xffffff; s.alpha = 255;
    vs_plat_draw(&s);
}

static void text_centered(vs_game *g, float cx, float y, float scale, uint32_t tint, const char *t) {
    (void)g;
    vs_text_draw(cx - 0.5f * vs_text_width(scale, t), y, scale, tint, t);
}

static void button(vs_game *g, const char *frame, const char *label, float x, float y, float w, float h, int selected) {
    vs_ui_nine_slice(g, frame, x, y, w, h, 9, 14, 0xffffff);
    text_centered(g, x + 0.5f * w, y + 0.5f * h - 7, 2, 0xffffff, label);
    if (selected) vs_ui_nine_slice(g, "selectionSquareActive_03.png", x - 4, y - 4, w + 8, h + 8, 7, 7, 0xffffff);
}

/* ---- the pause screen's weapons and stats panels (classes 7432 and
 * 1779): weapons and passives with their level pips, then the player's
 * stats as the original formats them. The original shows them only once
 * more than 5 weapons are unlocked in the save; here always. ---- */

static void ui_item(vs_game *g, int tex, const vs_atlas *atl, const char *name, float cx, float cy, float scale, uint32_t tint) {
    vs_frame f;
    if (!name || vs_atlas_find(atl, name, &f) != 0) return;
    vs_sprite s = {0};
    s.tex = g->textures[tex]; s.sx = (int16_t)f.x; s.sy = (int16_t)f.y; s.sw = (int16_t)f.w; s.sh = (int16_t)f.h;
    s.scale_x = s.scale_y = scale; s.tint = tint; s.alpha = 255;
    s.dx = cx + (f.off_x - 0.5f * f.src_w) * scale; s.dy = cy + (f.off_y - 0.5f * f.src_h) * scale;
    vs_plat_draw(&s);
}

static float weapons_panel(vs_game *g, float x, float y, float w) {
    int list[2][VS_MAX_WEAPONS], n[2] = { 0, 0 };
    for (int i = 0; i < VS_MAX_WEAPONS; i++) {
        const vs_weapon *wp = &g->weapons[i];
        if (wp->active && !wp->hidden) list[wp->is_power_up ? 1 : 0][n[wp->is_power_up ? 1 : 0]++] = i;
    }
    float rows0 = (float)((n[0] + 5) / 6), rows1 = (float)((n[1] + 5) / 6), h = 64 * (rows0 + rows1);
    if (h <= 0) return 0;
    vs_ui_nine_slice_alpha(g, "frame1_c2.png", x, y, w, h, 12, 12, 0xffffff, 191);
    for (int kind = 0; kind < 2; kind++) {
        float y0 = y + 20 + (kind ? 64 * rows0 : 0);
        for (int k = 0; k < n[kind]; k++) {
            const vs_weapon *wp = &g->weapons[list[kind][k]];
            vs_group grp; int gi = vs_table_find_group_enum(&g->weapons_table, wp->type);
            if (gi < 0) continue;
            vs_table_group(&g->weapons_table, (uint32_t)gi, &grp);
            vs_weapons_rec r; vs_table_record(&g->weapons_table, grp.first, &r);
            float cx = x + 20 + (k % 6) * 32, cy = y0 + 64 * (k / 6);
            ui_item(g, g->tex_ui, &g->atl_ui, kind ? "passiveBG.png" : "weaponBG.png", cx, cy, 2, (uint32_t)wp->level >= grp.count ? 0xffff00 : 0xffffff);
            ui_item(g, g->tex_items, &g->atl_items, VS_HAS(&r, VS_WEAPONS_F_FRAMENAME) ? vs_table_str(&g->weapons_table, r.frameName) : NULL, cx, cy, 2, 0xffffff);
            for (uint32_t l = 0; l < grp.count; l++)
                ui_item(g, g->tex_ui, &g->atl_ui, (uint32_t)wp->level <= l ? "weaponLevelEmpty.png" : "weaponLevelFull.png", cx - 8 + (l % 3) * 8, cy + 20 + 8 * (l / 3), 1, 0xffffff);
        }
    }
    return h;
}

static void stats_panel(vs_game *g, float x, float y, float w) {
    const vs_player *p = &g->player;
    enum { HEALTH, PER_SEC, FLAT, PCT, MAGNET, GAP };
    static const struct { int type, kind; } rows[] = {
        { VS_WEAPONS_MAXHEALTH, HEALTH }, { VS_WEAPONS_REGEN, PER_SEC }, { VS_WEAPONS_ARMOR, FLAT }, { VS_WEAPONS_MOVESPEED, PCT }, { 0, GAP },
        { VS_WEAPONS_MIGHT, PCT }, { VS_WEAPONS_AREA, PCT }, { VS_WEAPONS_SPEED, PCT }, { VS_WEAPONS_DURATION, PCT }, { VS_WEAPONS_AMOUNT, FLAT },
        { VS_WEAPONS_COOLDOWN, PCT }, { 0, GAP }, { VS_WEAPONS_LUCK, PCT }, { VS_WEAPONS_GROWTH, PCT }, { VS_WEAPONS_GREED, PCT }, { VS_WEAPONS_MAGNET, MAGNET },
    };
    const int n = (int)(sizeof rows / sizeof rows[0]);
    vs_ui_nine_slice_alpha(g, "frame1_c2.png", x, y, w, 32.0f + 16 * (n - 1), 12, 12, 0xffffff, 191);
    for (int i = 0; i < n; i++) {
        if (rows[i].kind == GAP) continue;
        float v = 0;
        switch (rows[i].type) {
        case VS_WEAPONS_MAXHEALTH: v = p->maxHp; break;   case VS_WEAPONS_REGEN: v = p->regen; break;
        case VS_WEAPONS_ARMOR: v = p->armor; break;       case VS_WEAPONS_MOVESPEED: v = p->moveSpeed; break;
        case VS_WEAPONS_MIGHT: v = p->power; break;       case VS_WEAPONS_AREA: v = p->area; break;
        case VS_WEAPONS_SPEED: v = p->speed; break;       case VS_WEAPONS_DURATION: v = p->duration; break;
        case VS_WEAPONS_AMOUNT: v = p->amount; break;     case VS_WEAPONS_COOLDOWN: v = p->cooldown; break;
        case VS_WEAPONS_LUCK: v = p->luck; break;         case VS_WEAPONS_GROWTH: v = p->growth; break;
        case VS_WEAPONS_GREED: v = p->greed; break;       case VS_WEAPONS_MAGNET: v = g->magnet.radius; break;
        }
        char val[24];
        switch (rows[i].kind) {   /* GetStatDisplayTextValue */
        case HEALTH: case MAGNET: snprintf(val, sizeof val, "%.0f", v); break;
        case PER_SEC: snprintf(val, sizeof val, "%.1f", v); if (!strcmp(val, "0.0")) strcpy(val, "-"); break;
        case FLAT: snprintf(val, sizeof val, "%s%.0f", v >= 0 ? "+" : "", v); if (!strcmp(val, "+0")) strcpy(val, "-"); break;
        default: snprintf(val, sizeof val, "%s%d%%", v >= 1 ? "+" : "", (int)floorf(100 * (v - 1) + 0.5f)); if (!strcmp(val, "+0%")) strcpy(val, "-"); break;
        }
        const char *name = "?", *frame = NULL;
        int gi = vs_table_find_group_enum(&g->powerups_table, rows[i].type);
        if (gi >= 0) {
            vs_group grp; vs_table_group(&g->powerups_table, (uint32_t)gi, &grp);
            vs_powerups_rec r; vs_table_record(&g->powerups_table, grp.first, &r);
            if (VS_HAS(&r, VS_POWERUPS_F_NAME)) name = vs_table_str(&g->powerups_table, r.name);
            if (VS_HAS(&r, VS_POWERUPS_F_FRAMENAME)) frame = vs_table_str(&g->powerups_table, r.frameName);
        }
        float ry = y + 16 + 16 * i;
        ui_item(g, g->tex_items, &g->atl_items, frame, x + 14, ry, 1, 0xffffff);
        vs_text_draw(x + 26, ry - 5, 1.4f, 0xffffff, name);
        vs_text_draw(x + w - 12 - vs_text_width(1.4f, val), ry - 5, 1.4f, 0xffffff, val);
    }
}

void vs_options_render(vs_game *g) {
    char buf[256];
    const float W = (float)g->logical_w, H = (float)g->logical_h;
    const float pw = 380, ph = 310, px = 0.5f * (W - pw), py = 0.5f * (H - ph) - 16;
    const float label_x = px + 24, ctrl_x = px + 210, row0 = py + 58, row_h = 36;
    if (g->paused) vs_ui_rect(g, 0, 0, W, H, 0x000000, 128);   /* the original dims the game by half */
    if (g->paused) {
        float wh = weapons_panel(g, 8, 24, 200);
        stats_panel(g, 8, 24 + wh + 4, 200);
    }
    vs_ui_nine_slice(g, "frame5_c4.png", px, py, pw, ph, 12, 12, 0xffffff);
    text_centered(g, 0.5f * W, py + 16, 2.5f, 0xffffff, vs_lang_get(&g->lang, "options_header", "Options"));

    static const char *const keys[ROW_BUTTONS] = { "options_sounds", "options_music", "options_flashing_VFX", "options_damageNumbers", NULL };
    static const char *const fallback[ROW_BUTTONS] = { "Sounds", "Music", "Flashing VFX", "Damage Numbers", "Sound test" };
    for (int r = 0; r < ROW_BUTTONS; r++) {
        float y = row0 + row_h * r;          /* row centre */
        const char *label = keys[r] ? vs_lang_get(&g->lang, keys[r], fallback[r]) : fallback[r];
        vs_text_draw(label_x, y - 7, 2, 0xffffff, label);
        if (r == ROW_SOUNDS || r == ROW_MUSIC) {
            float v = r == ROW_SOUNDS ? g->save.sound_volume : g->save.music_volume;
            vs_ui_rect(g, ctrl_x, y - 2, 100 + 24, 4, 0x000000, 255);
            ui_frame(g, "menu_square_flat_24.png", ctrl_x + 100 * v, y - 12, 1);
            snprintf(buf, sizeof buf, "%d%%", (int)(v * 100 + 0.5f));
            vs_text_draw(ctrl_x + 132, y - 4, 1, 0xffffff, buf);
        } else if (r == ROW_FLASH || r == ROW_DAMAGE) {
            int on = r == ROW_FLASH ? g->save.flashing_vfx : g->save.damage_numbers;
            ui_frame(g, "menu_checkbox_24_bg.png", ctrl_x, y - 12, 1);
            if (on) ui_frame(g, "yes16.png", ctrl_x + 3, y - 7, 1);
        } else {
            vs_text_draw(ctrl_x, y - 4, 1, 0xc0c0c0, "A sound  X tone  Y swap");
        }
        if (g->opt_sel == r) vs_ui_nine_slice(g, "selectionSquareActive_03.png", px + 12, y - 16, pw - 24, 32, 7, 7, 0xffffff);
    }

    float by = py + ph - 58;
    if (g->paused) {
        button(g, "button_c8_normal.png", vs_lang_get(&g->lang, "pause_quit", "QUIT"), px + 30, by, 140, 40, g->opt_sel == ROW_BUTTONS && g->opt_btn == BTN_QUIT);
        button(g, "button_c9_normal.png", vs_lang_get(&g->lang, "pause_resume", "RESUME"), px + pw - 170, by, 140, 40, g->opt_sel == ROW_BUTTONS && g->opt_btn == BTN_RESUME);
    } else {
        button(g, "button_c8_normal.png", vs_lang_get(&g->lang, "topBar_back", "BACK"), 0.5f * (W - 140), by, 140, 40, g->opt_sel == ROW_BUTTONS);
    }

    /* help line, and the sound diagnostics while the test row is selected */
    float ty = py + ph + 8;
    text_centered(g, 0.5f * W, ty, 1, 0xa0a0a0, g->paused ? "up/down choose, left/right change, A select, B/start resume"
                                                          : "up/down choose, left/right change, A select, B back");
    if (g->opt_sel == ROW_TEST && g->audio_status[0]) {
        snprintf(buf, sizeof buf, "%s | %.0f s decoded | level %d (max %d) | played %lu | output %s", g->audio_status,
                 vs_audio_decoded_seconds(), vs_audio_last_peak(), vs_audio_max_peak(), (unsigned long)vs_plat_audio_played(),
                 g->audio_swap ? "SWAPPED" : "normal");
        text_centered(g, 0.5f * W, ty + 12, 1, strstr(g->audio_status, "NO ") ? 0xff4040 : 0x60a060, buf);
        vs_audio_debug("LevelUp", buf, sizeof buf);
        text_centered(g, 0.5f * W, ty + 22, 1, 0x808080, buf);
    }
}
