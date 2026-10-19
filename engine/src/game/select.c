/* Character and stage selection (the original's CharSelectionScene, line
 * 7681, and StageSelectionScene, line 10785; notes/behaviour/select.md).
 *
 * Character select: a grid of character cards (name, sprite, starting
 * weapon; characters not bought yet are black silhouettes), the stats panel
 * on the left and a panel at the bottom with the selected character's full
 * name, description and button. A on a bought character confirms it (the
 * panel turns yellow and the button reads START), A again goes on to the
 * stage select; on one not bought, A buys it. Stage select: one card per
 * stage with its picture, name and description, the same panel and the
 * same confirm-then-START. B goes back one screen. */
#include "game/game.h"

#include <stdio.h>
#include <string.h>

#include "core/vs_text.h"

#define COLS 5
#define CARD_W 104.0f
#define CARD_H 86.0f
#define GAP 8.0f
#define LIST_TOP 52.0f
#define PANEL_Y 334.0f          /* bottom panel */

/* ---- helpers ---- */

static void frame_at(vs_game *g, int tex, const vs_atlas *atl, const char *name, float x, float y, float ox, float oy, float scale, uint32_t tint, uint8_t alpha) {
    vs_frame f;
    if (!name || !*name || vs_atlas_find(atl, name, &f) != 0) return;
    vs_sprite s = {0};
    s.tex = g->textures[tex]; s.sx = (int16_t)f.x; s.sy = (int16_t)f.y; s.sw = (int16_t)f.w; s.sh = (int16_t)f.h;
    s.scale_x = s.scale_y = scale; s.tint = tint; s.alpha = alpha;
    s.dx = x + (f.off_x - ox * f.src_w) * scale; s.dy = y + (f.off_y - oy * f.src_h) * scale;
    vs_plat_draw(&s);
}

static void text_centered(float cx, float y, float scale, uint32_t tint, const char *t) {
    vs_text_draw(cx - 0.5f * vs_text_width(scale, t), y, scale, tint, t);
}

/* A green button (GreenButton: button_c5_normal) with a centred label. */
static void green_button(vs_game *g, float cx, float cy, float w, float h, const char *label) {
    vs_ui_nine_slice(g, "button_c5_normal.png", cx - 0.5f * w, cy - 0.5f * h, w, h, 9, 14, 0xffffff);
    text_centered(cx, cy - 7, 2, 0xffffff, label);
}

static int char_rec(const vs_game *g, int c, vs_characters_rec *r) {
    int gi = vs_table_find_group_enum(&g->characters_table, c);
    if (gi < 0) return -1;
    vs_group grp; vs_table_group(&g->characters_table, (uint32_t)gi, &grp);
    vs_table_record(&g->characters_table, grp.first, r);
    return 0;
}

static const char *char_str(const vs_game *g, uint32_t off, int present) { return present ? vs_table_str(&g->characters_table, off) : ""; }

static const char *weapon_frame(const vs_game *g, int type) {
    vs_weapons_rec w;
    if (vs_weapon_record(g, type, 0, &w) != 0 || !VS_HAS(&w, VS_WEAPONS_F_FRAMENAME)) return NULL;
    return vs_table_str(&g->weapons_table, w.frameName);
}

/* The character's starting stats as the original's StatsPanel shows them:
 * the character's own values plus every power-up rank bought (Max Health
 * as a share of the character's); values raised by power-ups are yellow. */
static void char_stats(const vs_game *g, int c, float *v, uint8_t *hi) {
    vs_characters_rec r;
    int have = char_rec(g, c, &r) == 0;
    for (int i = 0; i < VS_UI_STAT_ROWS; i++) {
        int type = VS_UI_STATS[i];
        float base = 0;
        hi[i] = 0;
        switch (type) {
#define F(T, field, NAME, dflt) case VS_WEAPONS_##T: base = have && VS_HAS(&r, VS_CHARACTERS_F_##NAME) ? r.field : dflt; break;
        F(MAXHEALTH, maxHp, MAXHP, 100) F(REGEN, regen, REGEN, 0) F(ARMOR, armor, ARMOR, 0) F(MOVESPEED, moveSpeed, MOVESPEED, 1)
        F(MIGHT, power, POWER, 1) F(AREA, area, AREA, 1) F(SPEED, speed, SPEED, 1) F(DURATION, duration, DURATION, 1)
        F(AMOUNT, amount, AMOUNT, 0) F(COOLDOWN, cooldown, COOLDOWN, 1) F(LUCK, luck, LUCK, 1) F(GROWTH, growth, GROWTH, 1)
        F(GREED, greed, GREED, 1) F(MAGNET, magnet, MAGNET, 0)
#undef F
        default: break;
        }
        v[i] = base;
        if (type < 0 || type >= VS_WEAPONS_COUNT) continue;
        int gi = vs_table_find_group_enum(&g->powerups_table, type);
        if (gi < 0) continue;
        vs_group grp; vs_table_group(&g->powerups_table, (uint32_t)gi, &grp);
        uint32_t ranks = g->save.powerup_level[type] < grp.count ? g->save.powerup_level[type] : grp.count;
        for (uint32_t k = 0; k < ranks; k++) {
            vs_powerups_rec p; vs_table_record(&g->powerups_table, grp.first + k, &p);
            float add = 0; int has = 0;
            switch (type) {
#define P(T, field, NAME) case VS_WEAPONS_##T: has = VS_HAS(&p, VS_POWERUPS_F_##NAME); add = p.field; break;
            P(MAXHEALTH, maxHp, MAXHP) P(REGEN, regen, REGEN) P(ARMOR, armor, ARMOR) P(MOVESPEED, moveSpeed, MOVESPEED)
            P(MIGHT, power, POWER) P(AREA, area, AREA) P(SPEED, speed, SPEED) P(DURATION, duration, DURATION)
            P(AMOUNT, amount, AMOUNT) P(COOLDOWN, cooldown, COOLDOWN) P(LUCK, luck, LUCK) P(GROWTH, growth, GROWTH)
            P(GREED, greed, GREED) P(MAGNET, magnet, MAGNET)
#undef P
            default: break;
            }
            if (!has) continue;
            v[i] += type == VS_WEAPONS_MAXHEALTH ? base * add : add;
            hi[i] = 1;
        }
    }
}

static void notice(vs_game *g, const char *msg) {
    snprintf(g->shop_msg, sizeof g->shop_msg, "%s", msg);
    g->shop_msg_ms = 1500;
}

/* ---- character select ---- */

void vs_char_select_open(vs_game *g) {
    vs_build_character_list(g);   /* unlocks of the last run */
    g->screen = VS_SCREEN_SELECT;
    g->menu_sel = 0; g->menu_ready = 0; g->menu_scroll = 0; g->shop_msg_ms = 0;
    for (int i = 0; i < g->menu_char_count; i++) if (g->menu_char_list[i] == g->selected_character) g->menu_sel = i;
}

static void card_pos(const vs_game *g, int i, float *x, float *y) {
    const float grid_w = COLS * CARD_W + (COLS - 1) * GAP, left = 222, right = (float)g->logical_w - 10;
    *x = left + 0.5f * (right - left - grid_w) + (i % COLS) * (CARD_W + GAP);
    *y = LIST_TOP + (i / COLS) * (CARD_H + GAP) - g->menu_scroll;
}

void vs_char_select_input(vs_game *g, const vs_pad *pad) {
    uint32_t pressed = pad->buttons & ~g->prev_buttons;
    g->prev_buttons = pad->buttons;
    if (g->shop_msg_ms > 0) g->shop_msg_ms -= VS_TICK_MS;
    if (pressed & VS_BTN_B) { g->screen = VS_SCREEN_TITLE; vs_sfx(g, VS_SND_CLICKOUT, -1, -400, 1, 0, 0); return; }
    int n = g->menu_char_count;
    if (n == 0) return;
    int sel = g->menu_sel, rows = (n + COLS - 1) / COLS, col = sel % COLS, row = sel / COLS;
    if (pressed & VS_BTN_LEFT) col = (col + COLS - 1) % COLS;
    if (pressed & VS_BTN_RIGHT) col = (col + 1) % COLS;
    if (pressed & VS_BTN_UP) row = (row + rows - 1) % rows;
    if (pressed & VS_BTN_DOWN) row = (row + 1) % rows;
    sel = row * COLS + col;
    if (sel >= n) sel = (pressed & (VS_BTN_LEFT | VS_BTN_RIGHT)) ? row * COLS + ((pressed & VS_BTN_LEFT) ? (n - 1) % COLS : 0) : n - 1;
    if (sel != g->menu_sel) { g->menu_sel = sel; g->menu_ready = 0; vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0); }
    /* keep the selected row in view */
    float view = PANEL_Y - 8 - LIST_TOP, ry = (sel / COLS) * (CARD_H + GAP);
    if (ry - g->menu_scroll < 0) g->menu_scroll = ry;
    if (ry + CARD_H - g->menu_scroll > view) g->menu_scroll = ry + CARD_H - view;
    if (!(pressed & (VS_BTN_A | VS_BTN_START))) return;
    int c = g->menu_char_list[sel];
    if (!vs_char_bought(g, c)) {
        /* the original: buying selects the character; the next press confirms */
        if (vs_char_buy(g, c)) { g->selected_character = c; g->menu_ready = 0; vs_sfx(g, VS_SND_CLICKOUT, -1, -400, 1, 0, 0); }
        else { notice(g, "Not enough gold"); vs_sfx(g, VS_SND_CLICKOUT, -1, -400, 1, 0, 0); }
        return;
    }
    if (!g->menu_ready) {
        vs_characters_rec r;
        g->selected_character = c; g->menu_ready = 1;
        if (char_rec(g, c, &r) == 0 && VS_HAS(&r, VS_CHARACTERS_F_STARTINGWEAPON)) vs_save_count_weapon(g, r.startingWeapon);   /* SetSeenWeapon */
        vs_sfx(g, VS_SND_CLICKIN, -1, 100, 1, 0, 0);
    } else {
        vs_sfx(g, VS_SND_CLICKIN, -1, 200, 1, 0, 0);
        vs_stage_select_open(g);
    }
}

static void char_card(vs_game *g, int c, float x, float y, int selected) {
    vs_characters_rec r;
    if (char_rec(g, c, &r) != 0) return;
    int bought = vs_char_bought(g, c);
    vs_ui_nine_slice(g, "frame1_c2.png", x, y, CARD_W, CARD_H, 12, 12, selected && g->menu_ready ? 0xffff00 : 0xffffff);
    vs_text_draw(x + 6, y + 7, 1.4f, bought ? 0xffffff : 0x444444, char_str(g, r.charName, VS_HAS(&r, VS_CHARACTERS_F_CHARNAME)));
    const char *wf = weapon_frame(g, VS_HAS(&r, VS_CHARACTERS_F_STARTINGWEAPON) ? r.startingWeapon : VS_WEAPONS_WHIP);
    frame_at(g, g->tex_items, &g->atl_items, wf, x + CARD_W - 9, y + CARD_H - 9, 1, 1, 2, 0x111111, 255);   /* shadow */
    if (bought) frame_at(g, g->tex_items, &g->atl_items, wf, x + CARD_W - 12, y + CARD_H - 12, 1, 1, 2, 0xffffff, 255);
    frame_at(g, g->tex_characters, &g->atl_characters, char_str(g, r.spriteName, VS_HAS(&r, VS_CHARACTERS_F_SPRITENAME)),
             x + 6, y + CARD_H - 5, 0, 1, 2, bought ? 0xffffff : 0x000000, 255);
    if (selected) vs_ui_nine_slice(g, "selectionSquareActive_03.png", x - 7, y - 7, CARD_W + 14, CARD_H + 14, 7, 7, 0xffffff);
}

void vs_char_select_render(vs_game *g) {
    const float W = (float)g->logical_w;
    char buf[160];
    text_centered(0.5f * W, 24, 2.5f, 0xffffff, vs_lang_get(&g->lang, "character_header", "Character Selection"));
    int n = g->menu_char_count;
    if (n == 0) return;
    int c = g->menu_char_list[g->menu_sel];
    /* stats of the selected character (the original hides them while only one character is unlocked) */
    if (n > 1) {
        float v[VS_UI_STAT_ROWS]; uint8_t hi[VS_UI_STAT_ROWS];
        char_stats(g, c, v, hi);
        vs_ui_stats_panel(g, 10, LIST_TOP, 200, v, hi, 1);
    }
    for (int i = 0; i < n; i++) {
        float x, y;
        card_pos(g, i, &x, &y);
        if (y < LIST_TOP - 0.5f || y + CARD_H > PANEL_Y - 8 + 0.5f) continue;   /* whole cards only */
        char_card(g, g->menu_char_list[i], x, y, i == g->menu_sel);
    }
    /* the selected character */
    vs_characters_rec r;
    if (char_rec(g, c, &r) != 0) return;
    int bought = vs_char_bought(g, c);
    const float px = 10, pw = W - 20, ph = (float)g->logical_h - 10 - PANEL_Y;
    vs_ui_nine_slice(g, "frame1_c2.png", px, PANEL_Y, pw, ph, 12, 12, g->menu_ready ? 0xffff00 : 0xffffff);
    const char *prefix = char_str(g, r.prefix, VS_HAS(&r, VS_CHARACTERS_F_PREFIX));
    snprintf(buf, sizeof buf, "%s%s%s %s", prefix, *prefix ? " " : "", char_str(g, r.charName, VS_HAS(&r, VS_CHARACTERS_F_CHARNAME)),
             char_str(g, r.surname, VS_HAS(&r, VS_CHARACTERS_F_SURNAME)));
    vs_text_draw(px + 14, PANEL_Y + 12, 2, 0xffffff, buf);
    frame_at(g, g->tex_characters, &g->atl_characters, char_str(g, r.spriteName, VS_HAS(&r, VS_CHARACTERS_F_SPRITENAME)),
             px + 46, PANEL_Y + ph - 8, 0.5f, 1, 2.5f, bought ? 0xffffff : 0x000000, 255);
    if (bought) {
        frame_at(g, g->tex_ui, &g->atl_ui, "frameB.png", px + 112, PANEL_Y + 64, 0.5f, 0.5f, 2, 0xffffff, 255);
        frame_at(g, g->tex_items, &g->atl_items, weapon_frame(g, VS_HAS(&r, VS_CHARACTERS_F_STARTINGWEAPON) ? r.startingWeapon : VS_WEAPONS_WHIP),
                 px + 112, PANEL_Y + 64, 0.5f, 0.5f, 2, 0xffffff, 255);
    }
    vs_ui_text_wrapped(px + 150, PANEL_Y + 40, 1.5f, 0xffffff, char_str(g, r.description, VS_HAS(&r, VS_CHARACTERS_F_DESCRIPTION)), pw - 150 - 190, 4);
    const float bx = px + pw - 90, by = PANEL_Y + ph - 34;
    const char *label = !bought ? vs_lang_get(&g->lang, "charConfirm_unlock", "Unlock")
                      : g->menu_ready ? vs_lang_get(&g->lang, "charConfirm_start", "START") : vs_lang_get(&g->lang, "charConfirm_confirm", "Confirm");
    green_button(g, bx, by, 140, 44, label);
    if (!bought) {
        double price = vs_char_price(g, c);
        snprintf(buf, sizeof buf, "%.0f", price);
        float tw = vs_text_width(2, buf);
        vs_text_draw(bx + 0.5f * tw - tw + 10, PANEL_Y + 14, 2, g->save.coins >= price ? 0xffffff : 0xff6060, buf);
        frame_at(g, g->tex_ui, &g->atl_ui, "CoinGold.png", bx - 0.5f * tw + 2, PANEL_Y + 21, 0.5f, 0.5f, 1.5f, 0xffffff, 255);
    }
    if (g->shop_msg_ms > 0) text_centered(0.5f * W, PANEL_Y - 16, 1.5f, 0xff8080, g->shop_msg);
}

/* ---- stage select ---- */

#define ROW_H 84.0f
#define ROW_W 580.0f

static int stage_rec(const vs_game *g, int s, vs_stages_rec *r) {
    int gi = vs_table_find_group_enum(&g->stages_table, s);
    if (gi < 0) return -1;
    vs_group grp; vs_table_group(&g->stages_table, (uint32_t)gi, &grp);
    vs_table_record(&g->stages_table, grp.first, r);
    return 0;
}
static const char *stage_str(const vs_game *g, uint32_t off, int present) { return present ? vs_table_str(&g->stages_table, off) : ""; }

/* unlocked in the data, or by an achievement (save) or --unlock-all */
static int stage_unlocked(const vs_game *g, int s, const vs_stages_rec *r) {
    if (vs_stage_selectable(g, s)) return 1;
    if (s >= 0 && s < VS_STAGES_COUNT && g->save.stage_unlocked[s]) return 1;
    return VS_HAS(r, VS_STAGES_F_UNLOCKED) && r->unlocked;
}

void vs_stage_select_open(vs_game *g) {
    g->screen = VS_SCREEN_STAGE;
    g->stage_count = 0; g->stage_sel = 0; g->menu_ready = 0;
    for (uint32_t i = 0; i < g->stages_table.group_count && g->stage_count < 8; i++) {
        vs_group grp; vs_table_group(&g->stages_table, i, &grp);
        vs_stages_rec r; vs_table_record(&g->stages_table, grp.first, &r);
        if (VS_HAS(&r, VS_STAGES_F_HIDDEN) && r.hidden) continue;
        if (grp.enum_index == g->selected_stage) g->stage_sel = g->stage_count;
        g->stage_list[g->stage_count++] = grp.enum_index;
    }
}

static void start_run(vs_game *g) {
    vs_sfx(g, VS_SND_CLICKIN, -1, 200, 1, 0, 0);
    g->seed = g->seed * 6364136223846793005ull + 1442695040888963407ull;
    if (vs_game_start_run(g, g->selected_character, g->selected_stage) != 0) g->screen = VS_SCREEN_TITLE;
}

void vs_stage_select_input(vs_game *g, const vs_pad *pad) {
    uint32_t pressed = pad->buttons & ~g->prev_buttons;
    g->prev_buttons = pad->buttons;
    if (g->shop_msg_ms > 0) g->shop_msg_ms -= VS_TICK_MS;
    if (pressed & VS_BTN_B) {
        g->screen = VS_SCREEN_SELECT; g->menu_ready = 0;
        vs_sfx(g, VS_SND_CLICKOUT, -1, -400, 1, 0, 0);
        return;
    }
    int n = g->stage_count;
    if (n == 0) return;
    int sel = g->stage_sel;
    if (pressed & (VS_BTN_UP | VS_BTN_LEFT)) sel = (sel + n - 1) % n;
    if (pressed & (VS_BTN_DOWN | VS_BTN_RIGHT)) sel = (sel + 1) % n;
    if (sel != g->stage_sel) { g->stage_sel = sel; g->menu_ready = 0; vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0); }
    if (!(pressed & (VS_BTN_A | VS_BTN_START))) return;
    int s = g->stage_list[sel];
    if (!vs_stage_selectable(g, s)) {
        vs_stages_rec r;
        notice(g, stage_rec(g, s, &r) == 0 && stage_unlocked(g, s, &r) ? "This stage is not in the demake yet" : "Not yet discovered");
        vs_sfx(g, VS_SND_CLICKOUT, -1, -400, 1, 0, 0);
        return;
    }
    if (!g->menu_ready) { g->selected_stage = s; g->menu_ready = 1; vs_sfx(g, VS_SND_CLICKIN, -1, 100, 1, 0, 0); }
    else start_run(g);
}

void vs_stage_select_render(vs_game *g) {
    const float W = (float)g->logical_w;
    text_centered(0.5f * W, 24, 2.5f, 0xffffff, vs_lang_get(&g->lang, "stageSelection_header", "Stage Selection"));
    const float x0 = 0.5f * (W - ROW_W);
    for (int i = 0; i < g->stage_count; i++) {
        int s = g->stage_list[i];
        vs_stages_rec r;
        if (stage_rec(g, s, &r) != 0) continue;
        float y = LIST_TOP + i * (ROW_H + GAP);
        if (y + ROW_H > PANEL_Y - 8 + 0.5f) break;
        int unlocked = stage_unlocked(g, s, &r), playable = vs_stage_selectable(g, s);
        vs_ui_nine_slice_alpha(g, "frame1_c2.png", x0, y, ROW_W, ROW_H, 12, 12, 0xffffff, 200);
        if (i == g->stage_sel) vs_ui_rect(g, x0 + 4, y + 4, ROW_W - 8, ROW_H - 8, 0xffff00, 26);
        frame_at(g, g->tex_ui, &g->atl_ui, stage_str(g, r.uiFrame, VS_HAS(&r, VS_STAGES_F_UIFRAME)), x0 + 8, y + 4, 0, 0, 0.8f,
                 unlocked ? (playable ? 0xffffff : 0x909090) : 0x303030, 255);
        const char *name = unlocked ? stage_str(g, r.stageName, VS_HAS(&r, VS_STAGES_F_STAGENAME)) : vs_lang_get(&g->lang, "stageSelectionPanel_?", "???");
        const char *desc = unlocked ? stage_str(g, r.description, VS_HAS(&r, VS_STAGES_F_DESCRIPTION)) : vs_lang_get(&g->lang, "stageSelectionPanel_hidden", "Not yet discovered.");
        vs_text_draw(x0 + 148, y + 10, 2, 0xffff00, name);
        vs_ui_text_wrapped(x0 + 148, y + 34, 1.4f, playable || !unlocked ? 0xffffff : 0xa0a0a0, desc, ROW_W - 160, 3);
        if (unlocked && !playable) vs_text_draw(x0 + ROW_W - 12 - vs_text_width(1, "not in the demake yet"), y + 12, 1, 0xa0a0a0, "not in the demake yet");
        if (i == g->stage_sel) vs_ui_nine_slice(g, "selectionSquareActive_03.png", x0 - 6, y - 6, ROW_W + 12, ROW_H + 12, 7, 7, 0xffffff);
    }
    if (g->stage_count == 0) return;
    /* the selected stage */
    int s = g->stage_list[g->stage_sel];
    vs_stages_rec r;
    if (stage_rec(g, s, &r) != 0) return;
    int unlocked = stage_unlocked(g, s, &r), playable = vs_stage_selectable(g, s);
    const float px = 10, pw = W - 20, ph = (float)g->logical_h - 10 - PANEL_Y;
    vs_ui_nine_slice(g, "frame1_c2.png", px, PANEL_Y, pw, ph, 12, 12, g->menu_ready ? 0xffff00 : 0xffffff);
    vs_text_draw(px + 16, PANEL_Y + 14, 2.5f, 0xffffff, unlocked ? stage_str(g, r.stageName, VS_HAS(&r, VS_STAGES_F_STAGENAME)) : vs_lang_get(&g->lang, "stageConfirm_?", "???"));
    if (unlocked) vs_ui_text_wrapped(px + 16, PANEL_Y + 46, 1.5f, 0x880000, stage_str(g, r.tips, VS_HAS(&r, VS_STAGES_F_TIPS)), pw - 220, 3);
    if (playable) green_button(g, px + pw - 90, PANEL_Y + 0.5f * ph, 140, 44,
                               g->menu_ready ? vs_lang_get(&g->lang, "stageConfirm_start", "START") : vs_lang_get(&g->lang, "charConfirm_confirm", "Confirm"));
    if (g->shop_msg_ms > 0) text_centered(0.5f * W, PANEL_Y - 16, 1.5f, 0xff8080, g->shop_msg);
}
