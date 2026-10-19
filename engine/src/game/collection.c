/* Collection and achievements screens (the original's CollectionWeaponScene,
 * line 8991, and CollectionAchievementsScene, line 10591; behaviour in
 * notes/behaviour/collection.md).
 *
 * Collection: every weapon and pickup that is not hidden, 8 to a row, the
 * ones never found shown as a question mark; the panel at the bottom
 * describes the selected one. Achievements: one row per achievement with a
 * tick when done and the icon of what it unlocks; the panel at the bottom
 * says what it unlocks. Both scroll to keep the selection in view and go
 * back to the title with B. */
#include "game/game.h"

#include <stdio.h>
#include <string.h>

#include "core/vs_text.h"

#define COLS 8
#define CELL 60.0f              /* icon spacing (48 + 12) */
#define PANEL_H 104.0f          /* description panel */
#define ROW_H 44.0f             /* achievement rows (32 + 12) */
#define ROW_W 460.0f

/* ---- small drawing helpers ---- */

static void frame_at(vs_game *g, int tex, const vs_atlas *atl, const char *name, float cx, float cy, float ox, float oy, float scale, uint32_t tint, uint8_t alpha) {
    vs_frame f;
    if (!name || !*name || vs_atlas_find(atl, name, &f) != 0) return;
    vs_sprite s = {0};
    s.tex = g->textures[tex]; s.sx = (int16_t)f.x; s.sy = (int16_t)f.y; s.sw = (int16_t)f.w; s.sh = (int16_t)f.h;
    s.scale_x = s.scale_y = scale; s.tint = tint; s.alpha = alpha;
    s.dx = cx + (f.off_x - ox * f.src_w) * scale; s.dy = cy + (f.off_y - oy * f.src_h) * scale;
    vs_plat_draw(&s);
}

static void text_centered(float cx, float y, float scale, uint32_t tint, const char *t) {
    vs_text_draw(cx - 0.5f * vs_text_width(scale, t), y, scale, tint, t);
}

/* Draws `text` word-wrapped to `max_w`, at most `max_lines` lines. */
void vs_ui_text_wrapped(float x, float y, float scale, uint32_t tint, const char *text, float max_w, int max_lines) {
    char line[200];
    const float adv = (VS_TEXT_GLYPH_W + 1) * scale;
    int per_line = (int)(max_w / adv), lines = 0;
    if (per_line < 4) per_line = 4;
    if (per_line > (int)sizeof line - 1) per_line = (int)sizeof line - 1;
    const char *p = text;
    while (*p && lines < max_lines) {
        while (*p == ' ') p++;
        int n = 0, cut = -1;
        while (p[n] && p[n] != '\n' && n < per_line) { if (p[n] == ' ') cut = n; n++; }
        if (p[n] && p[n] != '\n' && p[n] != ' ' && cut > 0) n = cut;
        memcpy(line, p, (size_t)n); line[n] = 0;
        vs_text_draw(x, y + lines * (VS_TEXT_GLYPH_H + 3) * scale, scale, tint, line);
        p += n;
        if (*p == '\n') p++;
        lines++;
    }
}

/* "%0" / "%1" in a lang string replaced by a0 / a1. */
static void lang_format(char *out, size_t n, const char *tpl, const char *a0, const char *a1) {
    size_t o = 0;
    for (const char *p = tpl; *p && o + 1 < n; p++) {
        if (p[0] == '%' && (p[1] == '0' || p[1] == '1')) {
            const char *a = p[1] == '0' ? a0 : a1;
            for (; a && *a && o + 1 < n; a++) out[o++] = *a;
            p++;
        } else out[o++] = *p;
    }
    out[o] = 0;
}

/* The description panel at the bottom (confirmationPanel): background (grey
 * when not found / not done), framed icon, yellow name, description, orange
 * tips. Returns the icon centre. */
static void detail_panel(vs_game *g, int found, const char *name, const char *desc, const char *tips, float *icx, float *icy) {
    const float W = (float)g->logical_w, pw = 0.96f * W, px = 0.5f * (W - pw), py = (float)g->logical_h - 10 - PANEL_H;
    vs_ui_nine_slice(g, "frame1_c2.png", px, py, pw, PANEL_H, 12, 12, found ? 0xffffff : 0x888888);
    *icx = px + 52; *icy = py + 52;
    if (name && *name) text_centered(px + 0.5f * pw, py + 8, 2, 0xffff00, name);
    if (desc) vs_ui_text_wrapped(px + 0.25f * pw - 0.5f * 0.25f * pw + 52, py + 32, 1.5f, 0xffffff, desc, 0.75f * pw - 40, 2);
    if (tips) vs_ui_text_wrapped(px + 0.25f * pw - 0.5f * 0.25f * pw + 52, py + 66, 1.5f, 0xffa500, tips, 0.75f * pw - 40, 2);
}

/* ---- collection ---- */

typedef struct { int is_item, type; } coll_entry;

static int weapon_rec0(const vs_game *g, uint32_t gi, vs_weapons_rec *r, int *type) {
    vs_group grp; vs_table_group(&g->weapons_table, gi, &grp);
    vs_table_record(&g->weapons_table, grp.first, r);
    *type = grp.enum_index;
    return !(VS_HAS(r, VS_WEAPONS_F_HIDDEN) && r->hidden);
}
static int pickup_rec(const vs_game *g, uint32_t gi, vs_pickups_rec *r, int *type) {
    vs_group grp; vs_table_group(&g->pickups_table, gi, &grp);
    vs_table_record(&g->pickups_table, grp.first, r);
    *type = grp.enum_index;
    return !(VS_HAS(r, VS_PICKUPS_F_HIDDEN) && r->hidden);
}

/* weapons, then pickups, in table order */
static int coll_list(const vs_game *g, coll_entry *out, int max) {
    int n = 0, type;
    for (uint32_t i = 0; i < g->weapons_table.group_count && n < max; i++) {
        vs_weapons_rec r;
        if (weapon_rec0(g, i, &r, &type)) { out[n].is_item = 0; out[n].type = type; n++; }
    }
    for (uint32_t i = 0; i < g->pickups_table.group_count && n < max; i++) {
        vs_pickups_rec r;
        if (pickup_rec(g, i, &r, &type)) { out[n].is_item = 1; out[n].type = type; n++; }
    }
    return n;
}

typedef struct { int found; const char *name, *desc, *tips, *frame; } coll_info;

static void coll_get(const vs_game *g, const coll_entry *e, coll_info *ci) {
    memset(ci, 0, sizeof *ci);
    if (!e->is_item) {
        int gi = vs_table_find_group_enum(&g->weapons_table, e->type), type;
        vs_weapons_rec r;
        if (gi < 0) return;
        weapon_rec0(g, (uint32_t)gi, &r, &type);
        ci->found = g->unlock_all || (type >= 0 && type < VS_WEAPONS_COUNT && g->save.weapon_seen[type]);
        const vs_table *t = &g->weapons_table;
        ci->name = VS_HAS(&r, VS_WEAPONS_F_NAME) ? vs_table_str(t, r.name) : "";
        ci->desc = VS_HAS(&r, VS_WEAPONS_F_DESCRIPTION) ? vs_table_str(t, r.description) : "";
        ci->tips = VS_HAS(&r, VS_WEAPONS_F_TIPS) ? vs_table_str(t, r.tips) : "";
        ci->frame = VS_HAS(&r, VS_WEAPONS_F_FRAMENAME) ? vs_table_str(t, r.frameName) : NULL;
    } else {
        int gi = vs_table_find_group_enum(&g->pickups_table, e->type), type;
        vs_pickups_rec r;
        if (gi < 0) return;
        pickup_rec(g, (uint32_t)gi, &r, &type);
        ci->found = g->unlock_all || (VS_HAS(&r, VS_PICKUPS_F_SEEN) && r.seen) || (type >= 0 && type < VS_PICKUPS_COUNT && g->save.item_seen[type]);
        const vs_table *t = &g->pickups_table;
        ci->name = VS_HAS(&r, VS_PICKUPS_F_NAME) ? vs_table_str(t, r.name) : "";
        ci->desc = VS_HAS(&r, VS_PICKUPS_F_DESCRIPTION) ? vs_table_str(t, r.description) : "";
        ci->tips = VS_HAS(&r, VS_PICKUPS_F_TIPS) ? vs_table_str(t, r.tips) : "";
        ci->frame = VS_HAS(&r, VS_PICKUPS_F_FRAMENAME) ? vs_table_str(t, r.frameName) : NULL;
    }
}

void vs_collection_open(vs_game *g) {
    g->screen = VS_SCREEN_COLLECTION;
    g->coll_sel = 0; g->coll_scroll = 0;
}

/* Moves a grid selection; returns the new index. */
static int grid_move(int sel, int n, int cols, uint32_t pressed) {
    int rows = (n + cols - 1) / cols, col = sel % cols, row = sel / cols;
    if (pressed & VS_BTN_LEFT) col = (col + cols - 1) % cols;
    if (pressed & VS_BTN_RIGHT) col = (col + 1) % cols;
    if (pressed & VS_BTN_UP) row = (row + rows - 1) % rows;
    if (pressed & VS_BTN_DOWN) row = (row + 1) % rows;
    sel = row * cols + col;
    if (sel >= n) sel = (pressed & (VS_BTN_UP | VS_BTN_DOWN)) ? (pressed & VS_BTN_UP ? sel - cols : col) : row * cols + ((pressed & VS_BTN_LEFT) ? (n - 1) % cols : 0);
    return sel < 0 ? 0 : sel >= n ? n - 1 : sel;
}

/* The visible list area (between the header and the panel). */
static void list_area(const vs_game *g, float *top, float *bottom) {
    *top = 56; *bottom = (float)g->logical_h - 20 - PANEL_H;
}

/* scroll so that [y, y + h) (list coordinates) is visible */
static void keep_visible(const vs_game *g, float *scroll, float y, float h) {
    float top, bottom;
    list_area(g, &top, &bottom);
    if (y - *scroll < 0) *scroll = y;
    if (y + h - *scroll > bottom - top) *scroll = y + h - (bottom - top);
    if (*scroll < 0) *scroll = 0;
}

void vs_collection_input(vs_game *g, const vs_pad *pad) {
    uint32_t pressed = pad->buttons & ~g->prev_buttons;
    g->prev_buttons = pad->buttons;
    if (pressed & VS_BTN_B) { g->screen = VS_SCREEN_TITLE; vs_sfx(g, VS_SND_CLICKOUT, -1, -400, 1, 0, 0); return; }
    coll_entry list[128];
    int n = coll_list(g, list, 128);
    if (n == 0) return;
    int sel = grid_move(g->coll_sel, n, COLS, pressed);
    if (sel != g->coll_sel) vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0);
    g->coll_sel = sel;
    keep_visible(g, &g->coll_scroll, (sel / COLS) * CELL, CELL);
}

void vs_collection_render(vs_game *g) {
    const float W = (float)g->logical_w;
    vs_ui_nine_slice(g, "frame5_c4.png", 8, 22, W - 16, (float)g->logical_h - 30, 12, 12, 0xffffff);
    char header[64];
    snprintf(header, sizeof header, "%s", vs_lang_get(&g->lang, "weapon_header", "Collection"));
    text_centered(0.5f * W, 30, 2.5f, 0xffffff, header);
    coll_entry list[128];
    int n = coll_list(g, list, 128);
    float top, bottom;
    list_area(g, &top, &bottom);
    const float x0 = 0.5f * (W - COLS * CELL) + 6;
    for (int i = 0; i < n; i++) {
        float x = x0 + (i % COLS) * CELL, y = top + (i / COLS) * CELL - g->coll_scroll;
        if (y < top - 0.5f || y + CELL > bottom + 0.5f) continue;   /* only whole rows (the original masks the list) */
        coll_info ci;
        coll_get(g, &list[i], &ci);
        const char *bg = list[i].is_item ? "frameC.png" : "frameB.png";
        if (ci.found) {
            frame_at(g, g->tex_ui, &g->atl_ui, bg, x + 32, y + 32, 0.5f, 0.5f, 2, 0xffffff, 255);
            frame_at(g, g->tex_items, &g->atl_items, ci.frame, x + 32, y + 32, 0.5f, 0.5f, 2, 0xffffff, 255);
        } else {
            frame_at(g, g->tex_ui, &g->atl_ui, bg, x + 32, y + 32, 0.5f, 0.5f, 1.6f, 0xffffff, 128);
            frame_at(g, g->tex_items, &g->atl_items, "QuestionMark.png", x + 32, y + 32, 0.5f, 0.5f, 1, 0xffffff, 255);
        }
        if (i == g->coll_sel) vs_ui_nine_slice(g, "selectionSquareActive_03.png", x + 5, y + 5, 54, 54, 7, 7, 0xffffff);
    }
    if (n > 0 && g->coll_sel < n) {
        coll_info ci;
        coll_get(g, &list[g->coll_sel], &ci);
        float icx, icy;
        detail_panel(g, ci.found, ci.found ? ci.name : "???", ci.found ? ci.desc : vs_lang_get(&g->lang, "weaponCollectionPanel_notFound", "Not yet found."), ci.found ? ci.tips : "", &icx, &icy);
        frame_at(g, g->tex_ui, &g->atl_ui, "frameB.png", icx, icy, 0.5f, 0.5f, 2, 0xffffff, 255);
        frame_at(g, g->tex_items, &g->atl_items, ci.found ? ci.frame : "QuestionMark.png", icx, icy, 0.5f, 0.5f, ci.found ? 2.0f : 1.0f, 0xffffff, 255);
    }
    text_centered(0.5f * W, (float)g->logical_h - 9, 1, 0xa0a0a0, "arrows choose, B back");
}

/* ---- achievements ---- */

static int ach_list(const vs_game *g, int *out, int max) {
    int n = 0;
    for (uint32_t i = 0; i < g->achievements_table.group_count && n < max; i++) {
        vs_group grp; vs_table_group(&g->achievements_table, i, &grp);
        out[n++] = (int)i;
    }
    return n;
}

typedef struct { int tex; const vs_atlas *atl; const char *name; float ox; } icon_ref;
typedef struct {
    int done, framed, gold;     /* framed: frameB behind the panel icon; gold: money bag over it */
    const char *desc;
    char tips[128];
    icon_ref panel, row;        /* the panel and the list row use different pictures for characters */
} ach_info;

static void icon_set(icon_ref *ic, int tex, const vs_atlas *atl, const char *name, float ox) { ic->tex = tex; ic->atl = atl; ic->name = name; ic->ox = ox; }

/* A stage's picture in the UI atlas: frameNameUnlock (stages) or frameName (hypers). */
static const char *stage_frame(const vs_game *g, int s, int hyper) {
    int gi = vs_table_find_group_enum(&g->stages_table, s);
    if (gi < 0) return "QuestionMark.png";
    vs_group grp; vs_table_group(&g->stages_table, (uint32_t)gi, &grp);
    vs_stages_rec r; vs_table_record(&g->stages_table, grp.first, &r);
    uint32_t off = hyper ? r.frameName : r.frameNameUnlock;
    int has = hyper ? VS_HAS(&r, VS_STAGES_F_FRAMENAME) : VS_HAS(&r, VS_STAGES_F_FRAMENAMEUNLOCK);
    const char *f = has ? vs_table_str(&g->stages_table, off) : "";
    return *f ? f : "QuestionMark.png";
}

static void ach_get(vs_game *g, int group, ach_info *ai) {
    memset(ai, 0, sizeof *ai);
    vs_group grp; vs_table_group(&g->achievements_table, (uint32_t)group, &grp);
    vs_achievements_rec r; vs_table_record(&g->achievements_table, grp.first, &r);
    const vs_table *t = &g->achievements_table;
    int a = grp.enum_index;
    ai->done = a >= 0 && a < VS_ACHIEVEMENTS_COUNT && g->save.achieved[a];
    ai->desc = VS_HAS(&r, VS_ACHIEVEMENTS_F_DESCRIPTION) ? vs_table_str(t, r.description) : "";
    const char *verb = ai->done ? vs_lang_get(&g->lang, "genericPopup_unlocked", "Unlocked: ") : vs_lang_get(&g->lang, "genericPopup_unlocks", "Unlocks: ");
    /* the original checks these in turn; a later one wins */
    if (VS_HAS(&r, VS_ACHIEVEMENTS_F_WEAPONTOUNLOCK) && r.weaponToUnlock > 0) {
        vs_weapons_rec w;
        if (vs_weapon_record(g, r.weaponToUnlock, 0, &w) == 0) {
            const char *f = VS_HAS(&w, VS_WEAPONS_F_FRAMENAME) ? vs_table_str(&g->weapons_table, w.frameName) : NULL;
            icon_set(&ai->panel, g->tex_items, &g->atl_items, f, 0.5f); ai->row = ai->panel;
            ai->framed = 1;
            snprintf(ai->tips, sizeof ai->tips, "%s%s", verb, vs_weapon_name(g, r.weaponToUnlock));
        }
    }
    if (VS_HAS(&r, VS_ACHIEVEMENTS_F_CHARACTERTOUNLOCK) && r.characterToUnlock > 0) {
        int gi = vs_table_find_group_enum(&g->characters_table, r.characterToUnlock);
        if (gi >= 0) {
            vs_group cg; vs_table_group(&g->characters_table, (uint32_t)gi, &cg);
            vs_characters_rec c; vs_table_record(&g->characters_table, cg.first, &c);
            const vs_table *ct = &g->characters_table;
            const char *prefix = VS_HAS(&c, VS_CHARACTERS_F_PREFIX) ? vs_table_str(ct, c.prefix) : "";
            const char *cname = VS_HAS(&c, VS_CHARACTERS_F_CHARNAME) ? vs_table_str(ct, c.charName) : "";
            const char *portrait = VS_HAS(&c, VS_CHARACTERS_F_PORTRAITNAME) ? vs_table_str(ct, c.portraitName) : "";
            snprintf(ai->tips, sizeof ai->tips, "%s%s%s%s", verb, prefix, *prefix ? " " : "", cname);
            icon_set(&ai->panel, g->tex_characters, &g->atl_characters, VS_HAS(&c, VS_CHARACTERS_F_SPRITENAME) ? vs_table_str(ct, c.spriteName) : NULL, 0.5f);
            icon_set(&ai->row, g->tex_ui, &g->atl_ui, *portrait ? portrait : "QuestionMark.png", 0.5f);
            ai->framed = 0;
        }
    }
    if (VS_HAS(&r, VS_ACHIEVEMENTS_F_STAGETOUNLOCK) && r.stageToUnlock > 0) {
        snprintf(ai->tips, sizeof ai->tips, "%s%s", verb, vs_stage_name(g, r.stageToUnlock));
        icon_set(&ai->panel, g->tex_ui, &g->atl_ui, stage_frame(g, r.stageToUnlock, 0), 0.5f); ai->row = ai->panel;
        ai->framed = 0;
    }
    if (VS_HAS(&r, VS_ACHIEVEMENTS_F_HYPERTOUNLOCK) && r.hyperToUnlock > 0) {
        lang_format(ai->tips, sizeof ai->tips, ai->done ? vs_lang_get(&g->lang, "genericPopup_unlockedHyper", "Unlocked: Hyper %0") : vs_lang_get(&g->lang, "genericPopup_unlocksHyper", "Unlocks: Hyper %0"), vs_stage_name(g, r.hyperToUnlock), NULL);
        icon_set(&ai->panel, g->tex_ui, &g->atl_ui, stage_frame(g, r.hyperToUnlock, 1), 0.5f); ai->row = ai->panel;
        ai->framed = 0;
    }
    if (VS_HAS(&r, VS_ACHIEVEMENTS_F_GOLDPRIZE) && r.goldPrize > 0 && VS_HAS(&r, VS_ACHIEVEMENTS_F_WEAPONICON) && r.weaponIcon > 0) {
        char num[16];
        vs_weapons_rec w;
        snprintf(num, sizeof num, "%d", r.goldPrize);
        lang_format(ai->tips, sizeof ai->tips, ai->done ? vs_lang_get(&g->lang, "genericPopup_obtainedCoin", "Obtained: %0 gold coins") : vs_lang_get(&g->lang, "genericPopup_obtainsCoin", "Obtains: %0 gold coins"), num, NULL);
        if (vs_weapon_record(g, r.weaponIcon, 0, &w) == 0)
            icon_set(&ai->panel, g->tex_items, &g->atl_items, VS_HAS(&w, VS_WEAPONS_F_FRAMENAME) ? vs_table_str(&g->weapons_table, w.frameName) : NULL, 0.75f);
        ai->row = ai->panel;
        ai->framed = 0; ai->gold = 1;
    }
}

/* An icon at scale 2 (the original's icons are all 2 × PixelScale); a gold
 * prize gets the money bag over it. */
static void ach_icon(vs_game *g, const ach_info *ai, const icon_ref *ic, int framed, float cx, float cy) {
    if (framed) frame_at(g, g->tex_ui, &g->atl_ui, "frameB.png", cx, cy, 0.5f, 0.5f, 2, 0xffffff, 255);
    if (ic->atl) frame_at(g, ic->tex, ic->atl, ic->name, cx, cy, ic->ox, 0.5f, 2, 0xffffff, 255);
    if (ai->gold) frame_at(g, g->tex_ui, &g->atl_ui, "MoneyBagGreen.png", cx, cy, 0.5f, 0.5f, 2, 0xffffff, 255);
}

void vs_achievements_open(vs_game *g) {
    g->screen = VS_SCREEN_ACHIEVEMENTS;
    g->ach_sel = 0; g->ach_scroll = 0;
}

void vs_achievements_input(vs_game *g, const vs_pad *pad) {
    uint32_t pressed = pad->buttons & ~g->prev_buttons;
    g->prev_buttons = pad->buttons;
    if (pressed & VS_BTN_B) { g->screen = VS_SCREEN_TITLE; vs_sfx(g, VS_SND_CLICKOUT, -1, -400, 1, 0, 0); return; }
    int list[VS_ACHIEVEMENTS_COUNT + 8], n = ach_list(g, list, VS_ACHIEVEMENTS_COUNT + 8);
    if (n == 0) return;
    int sel = g->ach_sel;
    if (pressed & VS_BTN_UP) sel = (sel + n - 1) % n;
    if (pressed & VS_BTN_DOWN) sel = (sel + 1) % n;
    if (pressed & VS_BTN_LEFT) sel = sel >= 5 ? sel - 5 : 0;           /* page up / down */
    if (pressed & VS_BTN_RIGHT) sel = sel + 5 < n ? sel + 5 : n - 1;
    if (sel != g->ach_sel) vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0);
    g->ach_sel = sel;
    keep_visible(g, &g->ach_scroll, sel * ROW_H, ROW_H);
}

void vs_achievements_render(vs_game *g) {
    const float W = (float)g->logical_w;
    vs_ui_nine_slice(g, "frame5_c4.png", 8, 22, W - 16, (float)g->logical_h - 30, 12, 12, 0xffffff);
    int list[VS_ACHIEVEMENTS_COUNT + 8], n = ach_list(g, list, VS_ACHIEVEMENTS_COUNT + 8), done = 0;
    ach_info ai;
    for (int i = 0; i < n; i++) { ach_get(g, list[i], &ai); done += ai.done; }
    char a0[16], a1[16], header[96];
    snprintf(a0, sizeof a0, "%d", done); snprintf(a1, sizeof a1, "%d", n);
    lang_format(header, sizeof header, vs_lang_get(&g->lang, "achievements_header", "Completed: %0 of %1"), a0, a1);
    text_centered(0.5f * W, 30, 2.5f, 0xffffff, header);
    float top, bottom;
    list_area(g, &top, &bottom);
    const float x0 = 0.5f * (W - ROW_W);
    for (int i = 0; i < n; i++) {
        float y = top + i * ROW_H - g->ach_scroll;
        if (y < top - 0.5f || y + ROW_H > bottom + 0.5f) continue;   /* only whole rows (the original masks the list) */
        ach_get(g, list[i], &ai);
        frame_at(g, g->tex_ui, &g->atl_ui, "menu_checkbox_24_bg.png", x0 + 32, y + 20, 0.5f, 0.5f, 1, 0xffffff, 255);
        if (ai.done) frame_at(g, g->tex_ui, &g->atl_ui, "menu_checkbox_24_checkmark.png", x0 + 32, y + 20, 0.5f, 0.5f, 1, 0xffffff, 255);
        vs_ui_text_wrapped(x0 + 54, y + 9, 1.5f, ai.done ? 0xffffff : 0xb0b0b0, ai.desc, 340, 2);
        ach_icon(g, &ai, &ai.row, 0, x0 + 432, y + 20);   /* rows show no frame behind weapons */
        if (i == g->ach_sel) vs_ui_nine_slice(g, "selectionSquareActive_03.png", x0 + 10, y + 1, ROW_W - 10, 38, 7, 7, 0xffffff);
    }
    /* scroll bar */
    float content = n * ROW_H, view = bottom - top;
    if (content > view) {
        float bx = x0 + ROW_W + 16, h = view * view / content, by = top + (view - h) * (g->ach_scroll / (content - view));
        vs_ui_nine_slice(g, "sliderrail_c4.png", bx - 4, top, 8, view, 4, 4, 0xffffff);
        vs_ui_rect(g, bx - 3, by, 6, h, 0xffffff, 200);
    }
    if (n > 0 && g->ach_sel < n) {
        ach_get(g, list[g->ach_sel], &ai);
        float icx, icy;
        detail_panel(g, ai.done, ai.done ? vs_lang_get(&g->lang, "genericPopup_obtained", "Obtained") : "", ai.desc, ai.tips, &icx, &icy);
        ach_icon(g, &ai, &ai.panel, ai.framed, icx, icy);
    }
    text_centered(0.5f * W, (float)g->logical_h - 9, 1, 0xa0a0a0, "up/down choose, left/right page, B back");
}
