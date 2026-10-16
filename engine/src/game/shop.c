/* PowerUp selection (the original's shop, scene line 8210) and character
 * purchase (character select, line 7690). Behaviour in
 * notes/behaviour/shop.md.
 *
 * - A power-up rank costs (1 + its rank) × price × PowerUpMarkup, where the
 *   markup is 1 + 0.1 × all ranks bought (line 6725). Refund gives back
 *   everything: gold becomes the lifetime gold (line 2997).
 * - Bought ranks are applied at the start of every run, rank by rank, from
 *   the table's rows (ApplyBoughtPowerup, line 6735).
 * - A character costs price × CharMarkup (1 + 0.1 × (characters bought − 1));
 *   it is paid from the gold and from the lifetime gold, so a refund can't
 *   return it. */
#include "game/game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/vs_audio.h"
#include "core/vs_text.h"

#define BASE_MARKUP 0.1f
#define COLS 7

/* ---- data ---- */

static int powerup_group(const vs_game *g, int idx, vs_group *grp, vs_powerups_rec *r0) {
    vs_table_group(&g->powerups_table, (uint32_t)idx, grp);
    vs_table_record(&g->powerups_table, grp->first, r0);
    return !(VS_HAS(r0, VS_POWERUPS_F_HIDDEN) && r0->hidden);
}

/* the visible power-ups in table order: group indices */
static int powerup_list(const vs_game *g, int *out, int max) {
    int n = 0;
    for (uint32_t i = 0; i < g->powerups_table.group_count && n < max; i++) {
        vs_group grp; vs_powerups_rec r;
        if (powerup_group(g, (int)i, &grp, &r)) out[n++] = (int)i;
    }
    return n;
}

static uint32_t rank_of(const vs_game *g, const vs_group *grp) {
    int t = grp->enum_index;
    uint32_t r = t >= 0 && t < VS_WEAPONS_COUNT ? g->save.powerup_level[t] : 0;
    return r > grp->count ? grp->count : r;
}

float vs_powerup_markup(const vs_game *g) {
    uint32_t total = 0;
    for (int t = 0; t < VS_WEAPONS_COUNT; t++) total += g->save.powerup_level[t];
    return 1 + BASE_MARKUP * (float)total;
}

static double powerup_price(const vs_game *g, int idx) {
    vs_group grp; vs_powerups_rec r0;
    powerup_group(g, idx, &grp, &r0);
    double price = VS_HAS(&r0, VS_POWERUPS_F_PRICE) ? r0.price : 0;
    return (1.0 + rank_of(g, &grp)) * price * vs_powerup_markup(g);
}

/* ApplyBoughtPowerup for every rank bought (called once the player, the
 * stage and the starting weapon exist). */
void vs_powerups_apply(vs_game *g) {
    vs_player *p = &g->player;
    uint32_t total = 0;
    for (uint32_t i = 0; i < g->powerups_table.group_count; i++) {
        vs_group grp; vs_table_group(&g->powerups_table, i, &grp);
        uint32_t ranks = rank_of(g, &grp);
        for (uint32_t k = 0; k < ranks; k++) {
            vs_powerups_rec r; vs_table_record(&g->powerups_table, grp.first + k, &r);
#define ADD(field, F) if (VS_HAS(&r, VS_POWERUPS_F_##F)) p->field += r.field;
            ADD(power, POWER) ADD(area, AREA) ADD(speed, SPEED) ADD(cooldown, COOLDOWN) ADD(duration, DURATION)
            ADD(amount, AMOUNT) ADD(moveSpeed, MOVESPEED) ADD(growth, GROWTH) ADD(luck, LUCK) ADD(armor, ARMOR)
            ADD(regen, REGEN) ADD(greed, GREED)
#undef ADD
            if (VS_HAS(&r, VS_POWERUPS_F_MAXHP)) p->maxHp += p->maxHp * r.maxHp;
            p->hp = p->maxHp;
            if (VS_HAS(&r, VS_POWERUPS_F_MAGNET)) { g->magnet.radius += g->magnet.radius * r.magnet; g->magnet.percentage += r.magnet; }
            total++;
        }
    }
    if (total) vs_plat_log("power-ups: %u ranks (might %.2f, armor %.0f, max hp %.0f, magnet %.0f)", total, p->power, p->armor, p->maxHp, g->magnet.radius);
}

int vs_char_bought(const vs_game *g, int c) {
    if (g->unlock_all) return 1;
    if (c >= 0 && c < VS_CHARACTERS_COUNT && g->save.char_bought[c]) return 1;
    int gi = vs_table_find_group_enum(&g->characters_table, c);
    if (gi < 0) return 0;
    vs_group grp; vs_table_group(&g->characters_table, (uint32_t)gi, &grp);
    vs_characters_rec r; vs_table_record(&g->characters_table, grp.first, &r);
    return VS_HAS(&r, VS_CHARACTERS_F_ISBOUGHT) && r.isBought;
}

double vs_char_price(const vs_game *g, int c) {
    int gi = vs_table_find_group_enum(&g->characters_table, c);
    if (gi < 0) return 0;
    vs_group grp; vs_table_group(&g->characters_table, (uint32_t)gi, &grp);
    vs_characters_rec r; vs_table_record(&g->characters_table, grp.first, &r);
    int bought = 0;
    for (int k = 0; k < VS_CHARACTERS_COUNT; k++) bought += vs_char_bought(g, k);
    return (VS_HAS(&r, VS_CHARACTERS_F_PRICE) ? r.price : 0) * (1 + BASE_MARKUP * (bought - 1));
}

int vs_char_buy(vs_game *g, int c) {
    double price = vs_char_price(g, c);
    if (vs_char_bought(g, c) || g->save.coins < price || c < 0 || c >= VS_CHARACTERS_COUNT) return 0;
    g->save.coins -= price;
    g->save.lifetime_coins -= price;
    g->save.char_bought[c] = 1;
    vs_save_write(g);
    return 1;
}

/* ---- screen ---- */

static void notice(vs_game *g, const char *msg) {
    snprintf(g->shop_msg, sizeof g->shop_msg, "%s", msg);
    g->shop_msg_ms = 1500;
}

void vs_shop_open(vs_game *g) {
    g->screen = VS_SCREEN_POWERUP;
    g->shop_sel = 0;
    g->shop_msg_ms = 0;
}

static void buy(vs_game *g, int idx) {
    vs_group grp; vs_powerups_rec r0;
    powerup_group(g, idx, &grp, &r0);
    if (rank_of(g, &grp) >= grp.count) { notice(g, "Maximum level reached"); return; }
    double price = powerup_price(g, idx);
    if (g->save.coins < price) { notice(g, "Not enough gold"); vs_sfx(g, VS_SND_CLICKOUT, -1, -400, 1, 0, 0); return; }
    g->save.coins -= price;
    g->save.powerup_level[grp.enum_index]++;
    vs_save_write(g);
    vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0);
}

static void refund(vs_game *g) {
    memset(g->save.powerup_level, 0, sizeof g->save.powerup_level);
    g->save.coins = g->save.lifetime_coins;
    vs_save_write(g);
    vs_sfx(g, VS_SND_CLICKOUT, -1, 0, 1, 0, 0);
}

void vs_shop_input(vs_game *g, const vs_pad *pad) {
    uint32_t pressed = pad->buttons & ~g->prev_buttons;
    g->prev_buttons = pad->buttons;
    if (g->shop_msg_ms > 0) g->shop_msg_ms -= VS_TICK_MS;
    int list[32], n = powerup_list(g, list, 32);
    if (n == 0) { if (pressed & (VS_BTN_B | VS_BTN_A)) g->screen = VS_SCREEN_TITLE; return; }
    int sel = g->shop_sel, rows = (n + COLS - 1) / COLS;
    static int last_col;
    if (pressed & VS_BTN_B) { g->screen = VS_SCREEN_TITLE; vs_sfx(g, VS_SND_CLICKOUT, -1, -400, 1, 0, 0); return; }
    if (sel < 0) {   /* on the refund button */
        if (pressed & VS_BTN_DOWN) sel = last_col < n ? last_col : 0;
        if (pressed & VS_BTN_A) { refund(g); return; }
    } else {
        int col = sel % COLS, row = sel / COLS;
        if (pressed & VS_BTN_LEFT) col = (col + COLS - 1) % COLS;
        if (pressed & VS_BTN_RIGHT) col = (col + 1) % COLS;
        if (pressed & VS_BTN_DOWN) row = (row + 1) % rows;
        if (pressed & VS_BTN_UP) { if (row == 0) { last_col = col; sel = -1; goto moved; } row--; }
        sel = row * COLS + col;
        if (sel >= n) sel = (pressed & (VS_BTN_LEFT | VS_BTN_RIGHT)) ? row * COLS + ((pressed & VS_BTN_LEFT) ? (n - 1) % COLS : 0) : n - 1;
        if (pressed & VS_BTN_A) { buy(g, list[sel]); return; }
    }
moved:
    if (sel != g->shop_sel) vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0);
    g->shop_sel = sel;
}

static void ui_frame(vs_game *g, int tex, const vs_atlas *atl, const char *name, float cx, float cy, float scale, uint32_t tint) {
    vs_frame f;
    if (!name || vs_atlas_find(atl, name, &f) != 0) return;
    vs_sprite s = {0};
    s.tex = g->textures[tex]; s.sx = (int16_t)f.x; s.sy = (int16_t)f.y; s.sw = (int16_t)f.w; s.sh = (int16_t)f.h;
    s.scale_x = s.scale_y = scale; s.tint = tint; s.alpha = 255;
    s.dx = cx + (f.off_x - 0.5f * f.src_w) * scale; s.dy = cy + (f.off_y - 0.5f * f.src_h) * scale;
    vs_plat_draw(&s);
}

static void text_centered(float cx, float y, float scale, uint32_t tint, const char *t) {
    vs_text_draw(cx - 0.5f * vs_text_width(scale, t), y, scale, tint, t);
}

void vs_shop_render(vs_game *g) {
    char buf[96];
    const float W = (float)g->logical_w;
    vs_ui_nine_slice(g, "frame5_c4.png", 8, 22, W - 16, (float)g->logical_h - 38, 12, 12, 0xffffff);
    text_centered(0.5f * W, 32, 2.5f, 0xffffff, vs_lang_get(&g->lang, "powerup_header", "PowerUp Selection"));
    /* refund button */
    float rw = 220, rh = 26, rx = 0.5f * (W - rw), ry = 56;
    vs_ui_nine_slice(g, "button_c9_normal.png", rx, ry, rw, rh, 9, 14, 0xffffff);
    text_centered(0.5f * W, ry + 0.5f * rh - 5, 1.5f, 0xffffff, vs_lang_get(&g->lang, "powerup_refund", "Refund PowerUps"));
    if (g->shop_sel < 0) vs_ui_nine_slice(g, "selectionSquareActive_03.png", rx - 4, ry - 4, rw + 8, rh + 8, 7, 7, 0xffffff);

    int list[32], n = powerup_list(g, list, 32);
    const float pw = 96, ph = 100, gap = 12, x0 = 0.5f * (W - (COLS * pw + (COLS - 1) * gap)), y0 = 92;
    for (int i = 0; i < n; i++) {
        vs_group grp; vs_powerups_rec r0;
        powerup_group(g, list[i], &grp, &r0);
        uint32_t rank = rank_of(g, &grp);
        float x = x0 + (i % COLS) * (pw + gap), y = y0 + (i / COLS) * (ph + gap), cx = x + 0.5f * pw;
        vs_ui_nine_slice(g, "frame1_c2.png", x, y, pw, ph, 12, 12, rank < grp.count ? 0xffffff : 0xcccc88);
        const char *name = VS_HAS(&r0, VS_POWERUPS_F_NAME) ? vs_table_str(&g->powerups_table, r0.name) : "?";
        text_centered(cx, y + 6, 1.2f, rank > 0 ? 0xffffff : 0x606060, name);
        ui_frame(g, g->tex_ui, &g->atl_ui, "frameD.png", cx, y + 44, 2, 0xffffff);
        ui_frame(g, g->tex_items, &g->atl_items, VS_HAS(&r0, VS_POWERUPS_F_FRAMENAME) ? vs_table_str(&g->powerups_table, r0.frameName) : NULL, cx, y + 44, 2, 0xffffff);
        for (uint32_t k = 0; k < grp.count; k++) {
            float bx = cx + 16.0f * k - 8.0f * (grp.count - 1);
            ui_frame(g, g->tex_ui, &g->atl_ui, "menu_checkbox_16_bg.png", bx, y + 84, 1, 0xffffff);
            if (k < rank) ui_frame(g, g->tex_ui, &g->atl_ui, "menu_checkbox_16_checkmark.png", bx, y + 84, 1, 0xffffff);
        }
        if (i == g->shop_sel) vs_ui_nine_slice(g, "selectionSquareActive_03.png", x - 5, y - 5, pw + 10, ph + 10, 7, 7, 0xffffff);
    }

    /* confirmation panel: the selected power-up */
    float cy0 = y0 + 2 * (ph + gap) + 4, cw = W - 40, chh = (float)g->logical_h - 26 - cy0;
    vs_ui_nine_slice(g, "frame1_c2.png", 20, cy0, cw, chh, 12, 12, 0xffffff);
    if (g->shop_sel >= 0 && g->shop_sel < n) {
        int idx = list[g->shop_sel];
        vs_group grp; vs_powerups_rec r0;
        powerup_group(g, idx, &grp, &r0);
        uint32_t rank = rank_of(g, &grp);
        ui_frame(g, g->tex_ui, &g->atl_ui, "frameD.png", 56, cy0 + 0.5f * chh, 2, 0xffffff);
        ui_frame(g, g->tex_items, &g->atl_items, VS_HAS(&r0, VS_POWERUPS_F_FRAMENAME) ? vs_table_str(&g->powerups_table, r0.frameName) : NULL, 56, cy0 + 0.5f * chh, 2, 0xffffff);
        vs_text_draw(96, cy0 + 10, 2, 0xffffff, VS_HAS(&r0, VS_POWERUPS_F_NAME) ? vs_table_str(&g->powerups_table, r0.name) : "?");
        snprintf(buf, sizeof buf, "rank %u/%u", rank, grp.count);
        vs_text_draw(96 + vs_text_width(2, VS_HAS(&r0, VS_POWERUPS_F_NAME) ? vs_table_str(&g->powerups_table, r0.name) : "?") + 16, cy0 + 13, 1.2f, 0xc0c0c0, buf);
        vs_text_draw(96, cy0 + 34, 1.5f, 0xffffff, VS_HAS(&r0, VS_POWERUPS_F_DESCRIPTION) ? vs_table_str(&g->powerups_table, r0.description) : "");
        float bx = 20 + cw - 130, by = cy0 + 0.5f * chh - 4;
        if (rank < grp.count) {
            double price = powerup_price(g, idx);
            snprintf(buf, sizeof buf, "%.0f", price);
            float tw = vs_text_width(2, buf);
            vs_text_draw(bx + 55 - 0.5f * tw + 8, cy0 + 8, 2, g->save.coins >= price ? 0xffffff : 0xff6060, buf);
            ui_frame(g, g->tex_ui, &g->atl_ui, "CoinGold.png", bx + 55 - 0.5f * tw - 6, cy0 + 15, 1.5f, 0xffffff);
            vs_ui_nine_slice(g, "button_c5_normal.png", bx, by, 110, 34, 9, 14, 0xffffff);
            text_centered(bx + 55, by + 10, 2, 0xffffff, vs_lang_get(&g->lang, "powerupConfirm_buy", "Buy"));
        } else text_centered(bx + 55, by + 4, 1.5f, 0xffff80, "MAXIMUM LEVEL");
    } else {
        text_centered(0.5f * W, cy0 + 0.5f * chh - 12, 1.5f, 0xffffff, "Gives back all the gold spent on power-ups.");
        text_centered(0.5f * W, cy0 + 0.5f * chh + 4, 1.2f, 0xc0c0c0, "(Characters are not refunded.)");
    }
    if (g->shop_msg_ms > 0) text_centered(0.5f * W, cy0 - 14, 1.5f, 0xff8080, g->shop_msg);
    text_centered(0.5f * W, (float)g->logical_h - 11, 1, 0xa0a0a0, "arrows choose, A buy, B back");
}
