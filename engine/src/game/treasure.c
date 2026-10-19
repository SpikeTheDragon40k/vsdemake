/* Treasure chest screen and item-found screen (notes/behaviour/treasure.md,
 * "Presentation"; original treasure scene line 9385, item-found scene
 * 11333). The prizes are decided in pickup.c; these screens only show them
 * and hand them over when they close. The run is frozen meanwhile.
 *
 * Layout: the original is laid out for a larger menu resolution; positions
 * and sizes here are the original's times K. Additive parts are drawn with
 * VS_BLEND_ADD (vs_render_blend also applies to these immediate draws). */
#include "game/game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/vs_audio.h"
#include "core/vs_text.h"

#define K 0.6f
#define TPI 6.2831853f

/* ---- small drawing helpers ---- */

static void sprite_at(vs_game *g, int tex, const vs_atlas *atl, const char *name, float x, float y, float ox, float oy,
                      float scale, float angle_deg, uint32_t tint, float alpha) {
    vs_frame f;
    if (alpha <= 0 || scale <= 0 || vs_atlas_find(atl, name, &f) != 0) return;
    vs_sprite s = {0};
    s.tex = g->textures[tex]; s.sx = (int16_t)f.x; s.sy = (int16_t)f.y; s.sw = (int16_t)f.w; s.sh = (int16_t)f.h;
    s.scale_x = s.scale_y = scale;
    s.dx = x + (f.off_x - ox * f.src_w) * scale;
    s.dy = y + (f.off_y - oy * f.src_h) * scale;
    s.angle_deg = angle_deg; s.tint = tint;
    s.alpha = (uint8_t)(alpha >= 1 ? 255 : alpha * 255);
    s.blend = (uint8_t)vs_render_current_blend();
    vs_plat_draw(&s);
}

/* A frame stretched to w × h, centred on (cx, cy), turned about its centre
 * (drawn now, screen space). */
static void quad(vs_game *g, int tex, const vs_frame *f, float cx, float cy, float w, float h, float angle_deg, uint32_t tint, uint8_t alpha) {
    if (f->w <= 0 || f->h <= 0 || alpha == 0) return;
    vs_sprite s = {0};
    s.tex = g->textures[tex]; s.sx = (int16_t)f->x; s.sy = (int16_t)f->y; s.sw = (int16_t)f->w; s.sh = (int16_t)f->h;
    s.dx = cx - 0.5f * w; s.dy = cy - 0.5f * h; s.scale_x = w / f->w; s.scale_y = h / f->h;
    s.angle_deg = angle_deg; s.tint = tint; s.alpha = alpha;
    s.blend = (uint8_t)vs_render_current_blend();
    vs_plat_draw(&s);
}

static void text_centered(float cx, float y, float scale, uint32_t tint, const char *t) {
    vs_text_draw(cx - 0.5f * vs_text_width(scale, t), y, scale, tint, t);
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static const char *item_frame(vs_game *g, int type) {
    vs_weapons_rec r;
    if (type >= 0 && vs_weapon_record(g, type, 0, &r) == 0 && VS_HAS(&r, VS_WEAPONS_F_FRAMENAME)) return vs_table_str(&g->weapons_table, r.frameName);
    return "MoneyBagGreen.png";
}

/* The original's panel is 0.8 × the screen; here it sits near the top so
 * the prize panel shown with DONE fits under it. */
static void panel_rect(const vs_game *g, float *x, float *y, float *w, float *h) {
    *w = 0.8f * g->logical_w; *h = 0.76f * g->logical_h;
    *x = 0.5f * (g->logical_w - *w); *y = 8;
}

/* Burst1–6 at 30 fps, scale 8, yellow (OnEnterAnimation), if flashing VFX are on */
static void enter_burst(vs_game *g, float t_ms) {
    if (!g->save.flashing_vfx || t_ms >= 200) return;
    char name[32];
    snprintf(name, sizeof name, "Burst%d.png", 1 + (int)(t_ms / (1000.0f / 30)));
    vs_render_blend(VS_BLEND_ADD);
    sprite_at(g, g->tex_vfx, &g->atl_vfx, name, 0.5f * g->logical_w, 0.5f * g->logical_h, 0.5f, 0.5f, 8 * K, 0, 0xffff00, 0.8f);
    vs_render_blend(VS_BLEND_ALPHA);
}

/* Background grows from nothing in 150 ms (the original also turns it from 180°). */
static void zoom_panel(vs_game *g, float t_ms, float closing_ms) {
    float x, y, w, h;
    panel_rect(g, &x, &y, &w, &h);
    float k = clamp01(t_ms / 150.0f);
    if (closing_ms >= 0) k = 1 - clamp01(closing_ms / 150.0f);
    if (k <= 0) return;
    float cx = x + 0.5f * w, cy = y + 0.5f * h;
    vs_ui_nine_slice(g, "frame5_c4.png", cx - 0.5f * w * k, cy - 0.5f * h * k, w * k, h * k, 12, 12, 0xffffff);
}

static void button(vs_game *g, const char *frame, const char *label, float cx, float cy, float w, float h, float k, int selected) {
    if (k <= 0) return;
    vs_ui_nine_slice(g, frame, cx - 0.5f * w * k, cy - 0.5f * h * k, w * k, h * k, 9, 14, 0xffffff);
    if (k > 0.8f) text_centered(cx, cy - 7, 2, 0xffffff, label);
    if (selected && k >= 1) vs_ui_nine_slice(g, "selectionSquareActive_03.png", cx - 0.5f * w - 4, cy - 0.5f * h - 4, w + 8, h + 8, 7, 7, 0xffffff);
}

/* ---- treasure chest ---- */

/* chest anchor (origin 0.5, 1) and the points the prizes appear at */
static void chest_pos(const vs_game *g, float *x, float *y) {
    float px, py, pw, ph;
    panel_rect(g, &px, &py, &pw, &ph);
    *x = 0.5f * g->logical_w; *y = py + ph - 110 * K;
}
static const float WIN_POS[5][2] = { { 0, -250 }, { -115, -325 }, { 115, -325 }, { -150, -190 }, { 150, -190 } };
static const float REEL_ANGLE[5] = { 0, 20, -20, 40, -40 };

/* Phaser.Curves.Spline.getPoint over n points (Catmull-Rom). */
static void spline(float (*p)[2], int n, float t, float *x, float *y) {
    float f = (n - 1) * t; int i = (int)f; float w = f - (float)i;
    if (i >= n - 1) { i = n - 2; w = 1; }
    int i0 = i == 0 ? 0 : i - 1, i2 = i + 1 > n - 1 ? n - 1 : i + 1, i3 = i + 2 > n - 1 ? n - 1 : i + 2;
    for (int c = 0; c < 2; c++) {
        float p0 = p[i0][c], p1 = p[i][c], p2 = p[i2][c], p3 = p[i3][c];
        float v0 = (p2 - p0) * 0.5f, v1 = (p3 - p1) * 0.5f, t2 = w * w, t3 = w * t2;
        float r = (2 * p1 - 2 * p2 + v0 + v1) * t3 + (-3 * p1 + 3 * p2 - 2 * v0 - v1) * t2 + v0 * w + p1;
        if (c == 0) *x = r; else *y = r;
    }
}

/* Ribbons (MakeRibbons, line 9833): Ribbon1 trails following a fixed spline
 * (jittered, mirrored for every other ribbon, sometimes turned 10°), back
 * and forth in 1.5 s with Sine.easeInOut, 100 ms apart. */
static const float RIBBON_PATH[10][2] = { { 0, 0 }, { 0, 0 }, { 28, -41 }, { -23, -81 }, { 31, -143 }, { -33, -189 }, { -96, -158 }, { -81, -95 }, { 19, -73 }, { 157, -169 } };

static void make_ribbons(vs_game *g, int n) {
    vs_treasure_ui *t = &g->treasure;
    t->ribbon_n = n > VS_TREASURE_RIBBONS ? VS_TREASURE_RIBBONS : n;
    for (int r = 0; r < t->ribbon_n; r++) {
        float rot = vs_rng_f32(&g->ui_rng) > 0.5f ? (vs_rng_f32(&g->ui_rng) < 0.5f ? 10.0f : -10.0f) : 0.0f;
        float c = cosf(rot * TPI / 360), s = sinf(rot * TPI / 360);
        for (int i = 0; i < 10; i++) {
            float x = (RIBBON_PATH[i][0] + 24 * (vs_rng_f32(&g->ui_rng) - 0.5f)) * 4, y = RIBBON_PATH[i][1] * 4;
            if (r % 2 == 0) x = -x;
            t->ribbon_pts[r][i][0] = (x * c - y * s) * K;
            t->ribbon_pts[r][i][1] = (x * s + y * c) * K;
        }
        t->ribbon_hist_n[r] = 0;
    }
}

/* Reel contents (MakeTexture_*, line 9085): power-ups for POWERUP slots,
 * owned weapons for EXISTING slots, anything for EVOLUTION / FIXED; at
 * least 12 entries, padded with money bags (-1). */
static void make_reel_icons(vs_game *g, int slot, int prize_type) {
    vs_treasure_ui *t = &g->treasure;
    int *icons = t->reel_icons[slot], n = 0;
    for (int type = 1; type < VS_WEAPONS_COUNT && n < 12; type++) {
        vs_weapons_rec r;
        if (vs_weapon_record(g, type, 0, &r) != 0 || !VS_HAS(&r, VS_WEAPONS_F_FRAMENAME)) continue;
        int owned = vs_weapon_find(g, type) >= 0;
        int ok = prize_type == VS_PRIZES_POWERUP ? r.isPowerUp
               : (prize_type == VS_PRIZES_EXISTING_WEAPON || prize_type == VS_PRIZES_EXISTING_ANY) ? owned && !r.isPowerUp
               : vs_weapon_implemented(type) || r.isPowerUp;
        if (ok) icons[n++] = type;
    }
    for (int i = n - 1; i > 0; i--) { int j = (int)vs_rng_below(&g->ui_rng, (uint32_t)(i + 1)); int tmp = icons[i]; icons[i] = icons[j]; icons[j] = tmp; }
    while (n < 12) icons[n++] = -1;
    t->reel_icon_n[slot] = n;
}

void vs_treasure_ui_start(vs_game *g, int level, int n, const int *prizes, float coins_award) {
    vs_treasure_ui *t = &g->treasure;
    memset(t, 0, sizeof *t);
    t->active = 1; t->level = level; t->n = n; t->coins_award = coins_award;
    t->open_ms = -1; t->close_ms = -1; t->time_scale = 1;
    for (int i = 0; i < n; i++) t->prize[i] = prizes[i];
    /* timeline (SetupTreasureLevel1/2/3, lines 9573–9727) */
    static const float delays1[1] = { 0 }, delays3[3] = { 600, 0, 300 }, delays5[5] = { 600, 300, 300, 0, 0 };
    const float *d = n == 5 ? delays5 : n == 3 ? delays3 : delays1;
    for (int i = 0; i < n; i++) t->stop_delay[i] = d[i];
    if (level == 1) { t->stop_at = 6300; t->c_end = 7500; t->coins_end = 6300; }
    else if (level == 2) { t->stop_at = 9700; t->c_end = 11000; t->coins_end = 11000; t->reel_start[0] = 80; t->reel_start[1] = 160; }
    else {
        t->open_delay = 1600; t->stop_at = 1600 + 12100; t->c_end = 1600 + 13900; t->coins_end = 1600 + 14400;
        for (int i = 0; i < n; i++) t->reel_start[i] = 1600 + (i >= 3 ? 7900 : 0);
    }
    t->c_t0 = t->open_delay; t->c_v0 = 0;
    t->ok_at = t->stop_at + 6000;
    /* quick skip, once the player has seen enough chests (line 9589) */
    const uint32_t *runp = g->run_pickups, *svp = g->save.pickups;
    uint32_t t1 = svp[VS_PICKUPS_STATS_TREASURE_1] + runp[VS_PICKUPS_STATS_TREASURE_1];
    uint32_t t3 = svp[VS_PICKUPS_STATS_TREASURE_3] + runp[VS_PICKUPS_STATS_TREASURE_3];
    uint32_t all = svp[VS_PICKUPS_TREASURE] + runp[VS_PICKUPS_TREASURE];
    if (level == 1) t->skippable = t1 >= 5 || t3 >= 1 || all >= 50;
    else if (level == 2) t->skippable = t1 >= 3 || t3 >= 1 || all >= 50;
    /* reels and ribbons */
    static const int prize_kind_default = VS_PRIZES_POWERUP;
    for (int i = 0; i < n; i++) {
        int kind = prize_kind_default;
        if (prizes[i] >= 0) {
            vs_weapons_rec r;
            if (vs_weapon_record(g, prizes[i], 0, &r) == 0) kind = r.isEvolution ? VS_PRIZES_EVOLUTION : r.isPowerUp ? VS_PRIZES_POWERUP : VS_PRIZES_EXISTING_ANY;
        }
        t->reel_type[i] = kind;
        make_reel_icons(g, i, kind);
    }
    make_ribbons(g, level == 1 ? 2 : level == 2 ? 6 : 8);
    /* bubbles: 15 circles anywhere on the screen, radius 12.5–37.5 */
    for (int i = 0; i < 15; i++) {
        t->bubble[i][0] = g->logical_w * vs_rng_f32(&g->ui_rng);
        t->bubble[i][1] = g->logical_h * vs_rng_f32(&g->ui_rng);
        t->bubble[i][2] = (12.5f + 25 * vs_rng_f32(&g->ui_rng)) * K;
    }
    t->bubble_count = 1;
    /* fireworks (StartFireworks, line 9728): 5 emitters above the chest, in
     * the original's units relative to the chest */
    if (level == 3) {
        static const int frames[4] = { 0, 1, 2, 0 };   /* blue, pink, purple, blue */
        float px, py, pw, ph;
        panel_rect(g, &px, &py, &pw, &ph);
        for (int i = 0; i < 5; i++) {
            t->fw_frame[i] = frames[vs_rng_below(&g->ui_rng, 4)];
            t->fw_x[i] = (vs_rng_f32(&g->ui_rng) - 0.5f) * 0.5f * (px + pw) / K;
            t->fw_y[i] = -225 - 75 * vs_rng_f32(&g->ui_rng);
        }
    }
    vs_sfx(g, VS_SND_THINGFOUND, 0.7f, 0, 1, 0, 0);
    vs_audio_music_fade(0, 500);
}

static void open_chest(vs_game *g) {
    vs_treasure_ui *t = &g->treasure;
    t->open_ms = 0;
    if (g->snd[VS_SND_THINGFOUND] >= 0) vs_audio_stop(g->snd[VS_SND_THINGFOUND]);
    if (t->level == 3) vs_sfx(g, VS_SND_TREASURE3, 1.1f, 0, 1, 0, 0);
    else vs_sfx(g, t->level == 2 ? VS_SND_TREASURE2 : VS_SND_TREASURE1, 1.0f, 0, 1, 0, 0);
}

static float counter_target(const vs_game *g) {
    return ceilf(g->treasure.coins_award * g->gold_multiplier * g->player.greed);
}
static float counter_value(const vs_game *g) {
    const vs_treasure_ui *t = &g->treasure;
    if (t->open_ms < t->c_t0) return 0;
    float k = t->c_end > t->c_t0 ? clamp01((t->open_ms - t->c_t0) / (t->c_end - t->c_t0)) : 1;
    return t->c_v0 + (counter_target(g) - t->c_v0) * k;
}

/* SkipAnimation (line 9518): the counter finishes 1.2 / 1.3 s later, coins
 * stop, the reels stop now, the wait before DONE is 3 times shorter, and
 * the chest sound jumps to its ending. */
static void quick_skip(vs_game *g) {
    vs_treasure_ui *t = &g->treasure;
    t->skipped = 1;
    t->c_v0 = counter_value(g); t->c_t0 = t->open_ms; t->c_end = t->open_ms + (t->level == 1 ? 1200 : 1300);
    if (t->coins_end > t->open_ms) t->coins_end = t->open_ms;
    t->stop_at = t->open_ms;
    t->ok_at = t->stop_at + 6000 / 3.0f;
    int snd = t->level == 2 ? VS_SND_TREASURE2 : VS_SND_TREASURE1;
    if (g->snd[snd] >= 0) vs_audio_stop(g->snd[snd]);
    vs_sfx(g, snd, 1.0f, 0, 1, 0, 0);
    vs_audio_seek_last(t->level == 2 ? 9700 : 6300);
}

static void close_treasure(vs_game *g) {
    vs_treasure_ui *t = &g->treasure;
    t->active = 0;
    vs_treasure_award(g, t->n, t->prize, t->coins_award);
    vs_audio_music_fade(0.3f, 500);
}

/* coins (MakeCoinsEmitter, line 10111): two back emitters (gold, scale 2 → 1)
 * and two front ones (silver, 2 → 2.1); 225–315°, 450–600 px/s, gravity
 * 500, 4 s; walls at the screen edges bounce them back (0.9) */
static void emit_coin(vs_game *g, int kind) {
    vs_treasure_ui *t = &g->treasure;
    float cx, cy;
    chest_pos(g, &cx, &cy);
    vs_ui_particle *c = NULL;
    float oldest = -1;
    for (int i = 0; i < VS_TREASURE_COINS; i++) {
        if (t->coin[i].life <= 0) { c = &t->coin[i]; break; }
        if (t->coin[i].age > oldest) { oldest = t->coin[i].age; c = &t->coin[i]; }
    }
    float a = (225 + 90 * vs_rng_f32(&g->ui_rng)) * TPI / 360, v = (450 + 150 * vs_rng_f32(&g->ui_rng)) * K;
    memset(c, 0, sizeof *c);
    c->x = cx; c->y = cy - 30 * K; c->vx = cosf(a) * v; c->vy = sinf(a) * v;
    c->life = 4000; c->kind = kind; c->front = kind >= 2;
    c->s0 = 2 * K; c->s1 = (c->front ? 2.1f : 1.0f) * K;
    c->rot = 360 * vs_rng_f32(&g->ui_rng);
}

static void update_coins(vs_game *g) {
    vs_treasure_ui *t = &g->treasure;
    const float dt = VS_TICK_MS / 1000.0f;
    if (t->open_ms >= t->open_delay && t->open_ms < t->coins_end) {
        static const float back[3] = { 100, 50, 25 }, front[3] = { 200, 100, 50 };
        for (int k = 0; k < 4; k++) {
            float every = 2 * (k < 2 ? back[t->level - 1] : front[t->level - 1]);
            t->emit_acc[k] += VS_TICK_MS;
            while (t->emit_acc[k] >= every) { t->emit_acc[k] -= every; emit_coin(g, k); }
        }
    }
    for (int i = 0; i < VS_TREASURE_COINS; i++) {
        vs_ui_particle *c = &t->coin[i];
        if (c->life <= 0) continue;
        c->age += VS_TICK_MS;
        if (c->age >= c->life) { c->life = 0; continue; }
        c->vy += 500 * K * dt;
        c->x += c->vx * dt; c->y += c->vy * dt;
        if ((c->x < 0 && c->vx < 0) || (c->x > g->logical_w && c->vx > 0)) c->vx = -0.9f * c->vx;
        c->rot += 5;
    }
}

static void update_ribbons(vs_game *g) {
    vs_treasure_ui *t = &g->treasure;
    float cx, cy;
    chest_pos(g, &cx, &cy);
    for (int r = 0; r < t->ribbon_n; r++) {
        float ph = (t->open_ms - t->open_delay - 100.0f * r) / 1500.0f;
        if (ph < 0) continue;
        float u = fmodf(ph, 2.0f), s = u <= 1 ? u : 2 - u, e = 0.5f - 0.5f * cosf(s * TPI * 0.5f), x, y;
        spline(t->ribbon_pts[r], 10, e, &x, &y);
        float (*h)[2] = t->ribbon_hist[r];
        memmove(&h[1], &h[0], sizeof h[0] * (VS_TREASURE_TRAIL - 1));
        h[0][0] = cx + x; h[0][1] = cy - 50 * K + y;
        if (t->ribbon_hist_n[r] < VS_TREASURE_TRAIL) t->ribbon_hist_n[r]++;
    }
}

/* Bubbles (line 9402): shown when the chest opens (graphics in 300 ms, the
 * backgrounds over 6 / 9.7 / 5.2 s by level), hidden when the reels stop
 * (300 ms / 1 s). Each frame they rise by 10 × count + index (count grows
 * by 0.001 a frame) and wrap around the screen with a 72 px margin. */
static const float BUBBLE_SHOW[3] = { 6000, 9700, 5200 };
static const uint32_t BUBBLE_TINT[3] = { 0x666600, 0xcccc00, 0xffff00 };

static void bubble_alpha(const vs_treasure_ui *t, float *graphics, float *background) {
    float om = t->open_ms, t0 = t->open_delay;
    *graphics = *background = 0;
    if (om < t0) return;
    if (om < t->stop_at) {
        *graphics = clamp01((om - t0) / 300.0f);
        *background = clamp01((om - t0) / BUBBLE_SHOW[t->level - 1]);
    } else {   /* Hide completes the show tweens first, then fades */
        *graphics = 1 - clamp01((om - t->stop_at) / 300.0f);
        *background = 1 - clamp01((om - t->stop_at) / 1000.0f);
    }
}

static void update_bubbles(vs_game *g) {
    vs_treasure_ui *t = &g->treasure;
    if (t->open_ms < t->open_delay || t->open_ms >= t->stop_at + 1000) return;
    t->bubble_count += 0.001f;
    float pad = 72 * K, h = g->logical_h + 2 * pad;
    for (int i = 0; i < 15; i++) {
        float *b = t->bubble[i];
        b[1] -= (10 * t->bubble_count + i) * K;
        if (b[1] < -pad) b[1] += h;
    }
}

/* Fireworks: emitter i runs while the panel flashes at 100, 400, 700, 1000
 * and 1300 ms after OPEN (30 ms up, 30 down), each frame 32 + i/5 × 32
 * particles round the circle in 16 steps, 100 to 150 + i × 30 px/s, 3 s,
 * scale 2 + i/5 × 2 → 0, additive; one gravity well 100 px above the chest
 * (power 1 × gravity 150, epsilon 25). */
static const float FW_AT[5] = { 100, 400, 700, 1000, 1300 };

static void update_fireworks(vs_game *g) {
    vs_treasure_ui *t = &g->treasure;
    if (t->level != 3) return;
    for (int i = 0; i < 5; i++) {
        if (t->open_ms < FW_AT[i] || t->open_ms >= FW_AT[i] + 60) continue;
        int q = (int)ceilf(32 + i / 5.0f * 32);
        for (int k = 0; k < q; k++) {
            vs_pfx *p = &t->fw[t->fw_next];
            t->fw_next = (t->fw_next + 1) % VS_TREASURE_FW;
            float a = 22.5f * (float)(t->fw_emitted[i]++ % 16) * TPI / 360, sp = 100 + (50 + i * 30.0f) * vs_rng_f32(&g->ui_rng);
            memset(p, 0, sizeof *p);
            p->x = t->fw_x[i]; p->y = t->fw_y[i]; p->vx = cosf(a) * sp; p->vy = sinf(a) * sp;
            p->life = 3000; p->rot = 360 * vs_rng_f32(&g->ui_rng);
            p->s0 = 2 + i / 5.0f * 2; p->s1 = 0; p->a0 = p->a1 = 1;
            p->wx = 0; p->wy = -100; p->wpow = 150; p->weps2 = 25 * 25;
            p->frame = t->fw_frame[i]; p->add = 1;
        }
    }
    for (int i = 0; i < VS_TREASURE_FW; i++) vs_pfx_step(&t->fw[i], VS_TICK_MS);
}

static float firework_flash(const vs_treasure_ui *t) {
    float best = 0;
    if (t->level != 3) return 0;
    for (int i = 0; i < 5; i++) {
        float e = t->open_ms - FW_AT[i];
        if (e >= 0 && e < 60) { float v = e < 30 ? e / 30 : (60 - e) / 30; if (v > best) best = v; }
    }
    return best;
}

static void draw_bubbles(vs_game *g) {
    vs_treasure_ui *t = &g->treasure;
    float ga, ba;
    bubble_alpha(t, &ga, &ba);
    vs_frame f;
    if (ba > 0 && vs_atlas_find(&g->atl_vfx, "Gradient2.png", &f) == 0) {
        float w = (float)g->logical_w, h = (float)g->logical_h;
        quad(g, g->tex_vfx, &f, 0.5f * w, 0.5f * h, w, h, 0, BUBBLE_TINT[t->level - 1], (uint8_t)(255 * ba));
        vs_render_blend(VS_BLEND_ADD);
        quad(g, g->tex_vfx, &f, 0.5f * w, 0.5f * h, w, h, 0, 0xffffff, (uint8_t)(255 * ba));
        vs_render_blend(VS_BLEND_ALPHA);
    }
    if (ga > 0 && vs_atlas_find(&g->atl_vfx, "circle.png", &f) == 0) {
        vs_render_blend(VS_BLEND_ADD);
        for (int i = 0; i < 15; i++) {
            const float *b = t->bubble[i];
            uint32_t colour = (uint32_t)(i % 15) * 0x111100u + (uint32_t)(i % 5) * 0x33u;
            /* the graphics object is scaled (1, 1.3), positions included */
            quad(g, g->tex_vfx, &f, b[0], 1.3f * b[1], 2 * b[2], 2.6f * b[2], 0, colour, (uint8_t)(127 * ga));
        }
        vs_render_blend(VS_BLEND_ALPHA);
    }
}

static void draw_fireworks(vs_game *g, float cx, float cy) {
    static const char *const frames[3] = { "PfxBlue.png", "PfxPink.png", "PfxPurple.png" };
    vs_treasure_ui *t = &g->treasure;
    if (t->level != 3) return;
    vs_render_blend(VS_BLEND_ADD);
    for (int i = 0; i < VS_TREASURE_FW; i++) {
        const vs_pfx *p = &t->fw[i];
        if (p->life <= 0) continue;
        float s = p->s0 + (p->s1 - p->s0) * (p->age / p->life);
        sprite_at(g, g->tex_vfx, &g->atl_vfx, frames[p->frame], cx + K * p->x, cy + K * p->y, 0.5f, 0.5f, K * s, p->rot, 0xffffff, 1);
    }
    vs_render_blend(VS_BLEND_ALPHA);
}

void vs_treasure_ui_update(vs_game *g, const vs_pad *pad) {
    vs_treasure_ui *t = &g->treasure;
    uint32_t pressed = pad->buttons & ~g->prev_buttons;
    g->prev_buttons = pad->buttons;
    t->t_ms += VS_TICK_MS;
    int automatic = g->auto_pick;   /* headless --show-chest: press the buttons by itself */
    if (t->close_ms >= 0) {
        t->close_ms += VS_TICK_MS;
        if (t->close_ms >= 500) close_treasure(g);
        return;
    }
    if (t->open_ms < 0) {
        if (t->t_ms >= 300 && ((pressed & VS_BTN_A) || (automatic && t->t_ms >= 800))) open_chest(g);
        return;
    }
    t->open_ms += VS_TICK_MS;
    if (!t->skipped && t->skippable && t->open_ms >= (t->level == 1 ? 900 : 1800) && t->open_ms < t->stop_at && (pressed & (VS_BTN_B | VS_BTN_START)))
        quick_skip(g);
    if (!t->done_shown && t->open_ms >= t->ok_at) t->done_shown = 1;
    if (t->done_shown && t->open_ms >= t->ok_at + 150 && ((pressed & VS_BTN_A) || (automatic && t->open_ms >= t->ok_at + 1500))) {
        vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0);
        t->close_ms = 0;
    }
    update_coins(g);
    update_ribbons(g);
    update_bubbles(g);
    update_fireworks(g);
}

static void draw_coins(vs_game *g, int front) {
    vs_treasure_ui *t = &g->treasure;
    static const float fps[4] = { 30, 20, 24, 14 };
    for (int i = 0; i < VS_TREASURE_COINS; i++) {
        vs_ui_particle *c = &t->coin[i];
        if (c->life <= 0 || c->front != front) continue;
        char name[32];
        snprintf(name, sizeof name, "coin-spin-%s_%02d.png", c->kind < 2 ? "gold" : "silver", 1 + (int)(c->age * fps[c->kind] / 1000.0f) % 4);
        float s = c->s0 + (c->s1 - c->s0) * (c->age / c->life);
        sprite_at(g, g->tex_items, &g->atl_items, name, c->x, c->y, 0.5f, 0.5f, s, c->rot, 0xffffff, 1);
    }
}

void vs_treasure_ui_render(vs_game *g) {
    vs_treasure_ui *t = &g->treasure;
    char buf[64];
    float px, py, pw, ph, cx, cy;
    panel_rect(g, &px, &py, &pw, &ph);
    chest_pos(g, &cx, &cy);
    float om = t->open_ms, closing = t->close_ms;
    if (closing < 0) draw_bubbles(g);
    zoom_panel(g, t->t_ms, closing);
    if (t->t_ms < 150 || closing >= 150) { enter_burst(g, t->t_ms); return; }
    float upper_top = py + 15 * K, upper_bottom = cy - 70 * K;   /* UpperMask */
    float hide = closing >= 0 ? 1 - clamp01(closing / 150.0f) : 1;

    /* background glow (PanelBackgroundAdd): levels 2 and 3 */
    if (om >= 0) {
        float a = 0;
        float stop = t->stop_at;
        if (t->level == 2) a = om < stop ? 0.7f * clamp01(om / stop) : 0.7f * (1 - clamp01((om - stop) / 1000.0f));
        else if (t->level == 3 && om >= t->open_delay) a = om < stop ? 1.0f : 1 - clamp01((om - stop) / 1000.0f);
        float flash = firework_flash(t);
        if (flash > a) a = flash;
        vs_render_blend(VS_BLEND_ADD);
        if (a > 0) vs_ui_nine_slice_alpha(g, "frame5_c4_ad.png", px, py, pw, ph, 12, 12, 0xffffff, (uint8_t)(a * 200));
        vs_render_blend(VS_BLEND_ALPHA);
    }
    if (om >= 0) draw_fireworks(g, cx, cy);

    /* header, shrinking away when the chest opens */
    float hk = om < 0 ? 1 : 1 - clamp01(om / 300.0f);
    if (hk > 0.05f) text_centered(0.5f * g->logical_w, py + 45 * K - 7 * hk, 2.0f * hk + 0.01f, 0xffffff, vs_lang_get(&g->lang, "treasure_header", "Treasure found!"));

    /* reels: coloured strips of item icons rising from the chest */
    static const uint32_t colours[3][5] = { { 0x0000ff }, { 0x8800ff, 0xff00ff, 0xff00ff }, { 0xff0000, 0xff8800, 0xff8800, 0xffff00, 0xffff00 } };
    vs_frame white;
    int have_white = vs_atlas_find(&g->atl_characters, "whiteDot.png", &white) == 0;
    float reels_hide = 1;
    if (om >= t->ok_at) reels_hide = 1 - clamp01((om - t->ok_at) / 300.0f);
    for (int i = 0; i < t->n && om >= 0 && reels_hide > 0; i++) {
        float rs = t->reel_start[i];
        if (om < rs) continue;
        float stop = t->stop_at + t->stop_delay[i];
        float a = REEL_ANGLE[i] * TPI / 360, dx = sinf(a), dy = -cosf(a);
        float sx = cx, sy = cy - 10 * K;
        /* beam from the chest to the top of the upper area */
        float len = (sy - upper_top) / -dy;
        float y0 = upper_bottom, l0 = (sy - y0) / -dy;
        float w = 64 * K * reels_hide;
        uint32_t col = colours[t->level - 1][i];
        int flashing = om >= stop && om < stop + 50;
        if (have_white) {
            float mid = 0.5f * (l0 + len);
            quad(g, g->tex_characters, &white, sx + dx * mid, sy + dy * mid, len - l0, w, atan2f(dy, dx) * 360 / TPI, flashing ? 0xffffff : col, (uint8_t)(128 * reels_hide));
        }
        if (om < stop) {
            /* icons every 64 px (×K), moving up the beam by its length every 2 s */
            float spacing = 64 * K, run = (om - rs) * 1536 * K / 2000.0f;
            for (int k = 0; ; k++) {
                float d = fmodf(run, spacing) + spacing * k;
                if (d > len) break;
                if (d < l0) continue;
                int ic = t->reel_icons[i][((int)(run / spacing) - k + 1200) % t->reel_icon_n[i]];
                sprite_at(g, g->tex_items, &g->atl_items, item_frame(g, ic), sx + dx * d, sy + dy * d, 0.5f, 0.5f, 2 * K, 0, 0xffffff, 1);
            }
        }
    }

    /* ribbons (above the chest only), additive */
    vs_frame rib;
    if (om >= 0 && vs_atlas_find(&g->atl_vfx, "Ribbon1.png", &rib) == 0) {
        vs_render_blend(VS_BLEND_ADD);
        float maxd = 0;
        for (int i = 0; i < t->n; i++) if (t->stop_delay[i] > maxd) maxd = t->stop_delay[i];
        for (int r = 0; r < t->ribbon_n; r++) {
            float alpha = 1;
            float fade_at = t->stop_at + (float)r / t->ribbon_n * maxd;
            if (om >= fade_at) alpha = 1 - clamp01((om - fade_at) / 100.0f);
            if (alpha <= 0) continue;
            int n = t->ribbon_hist_n[r];
            for (int k = 1; k < n; k++) {
                float *a = t->ribbon_hist[r][k - 1], *b = t->ribbon_hist[r][k];
                if (a[1] > cy - 50 * K || b[1] > cy - 50 * K) continue;
                float ddx = b[0] - a[0], ddy = b[1] - a[1], l = sqrtf(ddx * ddx + ddy * ddy);
                if (l < 0.01f) continue;
                vs_frame row = rib; row.y = rib.y + (k - 1) * rib.h / (VS_TREASURE_TRAIL - 1); row.h = 1;
                quad(g, g->tex_vfx, &row, 0.5f * (a[0] + b[0]), 0.5f * (a[1] + b[1]), 32 * 0.5f * K, l + 0.5f,
                     atan2f(ddy, ddx) * 360 / TPI - 90, 0xffffff, (uint8_t)(200 * alpha));
            }
        }
        vs_render_blend(VS_BLEND_ALPHA);
    }

    draw_coins(g, 0);

    /* the chest: idle animation (15 fps, again every 3 s), then open (30 fps) */
    char name[48];
    if (om < t->open_delay || om < 0) snprintf(name, sizeof name, "TreasureIdle_%02d.png", 1 + ((int)(fmodf(t->t_ms, 3000) * 15 / 1000.0f) > 7 ? 7 : (int)(fmodf(t->t_ms, 3000) * 15 / 1000.0f)));
    else snprintf(name, sizeof name, "TreasureOpen_%02d.png", 1 + ((int)((om - t->open_delay) * 30 / 1000.0f) > 7 ? 7 : (int)((om - t->open_delay) * 30 / 1000.0f)));
    sprite_at(g, g->tex_ui, &g->atl_ui, name, cx, cy, 0.5f, 1, 5 * K * hide, 0, 0xffffff, hide);

    draw_coins(g, 1);
    if (om >= t->open_delay && om >= 0) {
        snprintf(name, sizeof name, "TreasureOpenFront_%02d.png", 1 + ((int)((om - t->open_delay) * 30 / 1000.0f) > 7 ? 7 : (int)((om - t->open_delay) * 30 / 1000.0f)));
        sprite_at(g, g->tex_ui, &g->atl_ui, name, cx, cy, 0.5f, 1, 5 * K * hide, 0, 0xffffff, hide);
    }

    /* prizes: background, item and two turning stars, popping in when the reels stop */
    for (int i = 0; i < t->n && om >= 0; i++) {
        float at = t->stop_at + t->stop_delay[i];
        if (om < at) continue;
        float e = om - at, pop = clamp01(e / 50.0f) * hide;
        float x = cx + WIN_POS[i][0] * K, y = cy + WIN_POS[i][1] * K;
        if (closing >= 0) { x += (0.5f * g->logical_w - x) * (1 - hide); y += (0 - y) * (1 - hide); }
        float p1 = 2 + 2 * (0.5f - 0.5f * cosf(TPI * e / 1000.0f)), p2 = 2 + 2 * (0.5f - 0.5f * cosf(TPI * e / 1200.0f));
        const char *star = e < 50 ? "HitStarWhite2.png" : "HitStar2.png";
        vs_render_blend(VS_BLEND_ADD);
        sprite_at(g, g->tex_vfx, &g->atl_vfx, star, x, y, 0.5f, 0.5f, (e < 50 ? 6 : p1) * K * pop, 360 * e / 10000.0f, 0xffffff, 0.7f);
        sprite_at(g, g->tex_vfx, &g->atl_vfx, star, x, y, 0.5f, 0.5f, (e < 50 ? 8 : p2) * K * pop, 45 + 405 * e / 7000.0f, 0xffffff, 0.7f);
        vs_render_blend(VS_BLEND_ALPHA);
        sprite_at(g, g->tex_vfx, &g->atl_vfx, "PrizeBG.png", x, y, 0.5f, 0.5f, 2 * K * pop, 0, 0xffffff, 0.8f);
        sprite_at(g, g->tex_items, &g->atl_items, item_frame(g, t->prize[i]), x, y, 0.5f, 0.5f, 2 * K * pop, 0, 0xffffff, 1);
    }

    /* coin counter: at the button, then at the header once DONE shows */
    if (om >= t->c_t0 && om >= 0) {
        float v = counter_value(g);
        int done = om >= t->c_end;
        float sc = 2 * clamp01((om - t->c_t0) / 300.0f);
        float yy = py + ph - 64 * K;
        if (om >= t->ok_at) {
            float m = om - t->ok_at;
            sc = m < 250 ? 2 * (1 - m / 250) : 2 * clamp01((m - 250) / 250);
            if (m >= 250) yy = py + 45 * K;
        } else if (done) sc = 2 + 0.2f * (0.5f - 0.5f * cosf(TPI * (om - t->c_end) / 2000.0f));
        snprintf(buf, sizeof buf, "%.2f", v);
        if (sc * hide > 0.05f) {
            float tw = vs_text_width(sc * hide, buf);
            vs_text_draw(0.5f * g->logical_w - 0.5f * tw, yy - 3.5f * sc * hide, sc * hide, done ? 0xffff00 : 0xffffff, buf);
            sprite_at(g, g->tex_items, &g->atl_items, "coin-spin-gold_01.png", 0.5f * g->logical_w + 0.5f * tw + 10, yy, 0.5f, 0.5f, sc * hide * 0.6f, 0, 0xffffff, 1);
        }
        if (done && om - t->c_end < 100) {
            vs_render_blend(VS_BLEND_ADD);
            sprite_at(g, g->tex_vfx, &g->atl_vfx, "sPFX_ring_64.png", 0.5f * g->logical_w, yy, 0.5f, 0.5f, 8 * K * (1 - (om - t->c_end) / 100.0f), 0, 0xffffff, 0.5f);
            vs_render_blend(VS_BLEND_ALPHA);
        }
    }

    /* OPEN / DONE button */
    float bk = 0;
    if (om < 0) bk = clamp01((t->t_ms - 300) / 150.0f);
    else if (om < 150 && !t->done_shown) bk = 1 - clamp01(om / 150.0f);
    else if (t->done_shown) bk = clamp01((om - t->ok_at) / 150.0f) * hide;
    button(g, "button_c9_normal.png", !t->done_shown ? vs_lang_get(&g->lang, "treasure_open", "OPEN") : "DONE",
           0.5f * g->logical_w, py + ph - 64 * K + (t->done_shown ? 0 : 0), 120, 40, bk, bk >= 1);
    if (t->skippable && !t->skipped && om >= (t->level == 1 ? 900 : 1800) && om < t->stop_at)
        text_centered(0.5f * g->logical_w, py + ph + 6, 1, 0xa0a0a0, "B: skip");

    /* the prizes, one panel at a time (CyclePrizes): each for 4.5 s, next every 5 s */
    if (t->done_shown && closing < 0) {
        int list[5], m = 0;
        for (int i = 0; i < t->n; i++) if (t->prize[i] >= 0) list[m++] = t->prize[i];
        if (m > 0) {
            float e = om - t->ok_at;
            int idx = (int)(e / 5000.0f) % m;
            float local = fmodf(e, 5000.0f), k = local < 300 ? local / 300 : local > 4500 ? 1 - clamp01((local - 4500) / 300) : 1;
            if (k > 0) {
                float bw = 0.96f * g->logical_w, bh = 0.2f * 0.8f * g->logical_h;
                vs_ui_weapon_panel(g, list[idx], 0.02f * g->logical_w, g->logical_h - bh * k - 4, bw, bh * k);
                if (m > 1) { snprintf(buf, sizeof buf, "%d/%d", idx + 1, m); vs_text_draw(0.98f * g->logical_w - vs_text_width(1.5f, buf) - 8, g->logical_h - bh * k + 4, 1.5f, 0x000000, buf); }
            }
        }
    }
    enter_burst(g, t->t_ms);
}

/* ---- item found (map bonus items) ---- */

void vs_item_found_start(vs_game *g, int type) {
    vs_item_found_ui *f = &g->item_found;
    memset(f, 0, sizeof *f);
    f->active = 1; f->type = type;
    vs_sfx(g, VS_SND_LEVELUP, 1.25f, 1000, 1, 0, 0);
    vs_audio_music_fade(0.2f, 500);
}

void vs_item_found_update(vs_game *g, const vs_pad *pad) {
    vs_item_found_ui *f = &g->item_found;
    uint32_t pressed = pad->buttons & ~g->prev_buttons;
    g->prev_buttons = pad->buttons;
    f->t_ms += VS_TICK_MS;
    /* confetti rising from the bottom (8 per frame, 1 s, -300..-600 px/s, scale 4 → 0) */
    for (int n = 0, i = 0; n < 8 && i < 128; i++) {
        vs_ui_particle *c = &f->confetti[i];
        if (c->life > 0) continue;
        memset(c, 0, sizeof *c);
        c->x = g->logical_w * vs_rng_f32(&g->ui_rng); c->y = (float)g->logical_h;
        c->vy = -(300 + 300 * vs_rng_f32(&g->ui_rng)) * K; c->life = 1000; c->kind = (int)vs_rng_below(&g->ui_rng, 5);
        c->s0 = (2 + 2 * vs_rng_f32(&g->ui_rng)) * K; c->rot = 360 * vs_rng_f32(&g->ui_rng);
        n++;
    }
    for (int i = 0; i < 128; i++) {
        vs_ui_particle *c = &f->confetti[i];
        if (c->life <= 0) continue;
        c->age += VS_TICK_MS;
        if (c->age >= c->life) { c->life = 0; continue; }
        c->y += c->vy * VS_TICK_MS / 1000.0f; c->rot += 5;
    }
    if (f->t_ms < 500) return;   /* buttons work after 500 ms */
    if (pressed & (VS_BTN_UP | VS_BTN_DOWN)) { f->sel ^= 1; vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0); }
    if ((pressed & VS_BTN_A) || g->auto_pick) {
        if (f->sel == 0) {
            vs_level_weapon_up(g, f->type, 1);
            vs_levelup_calculate_weights(g);
            vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0);
        } else vs_sfx(g, VS_SND_CLICKOUT, -1, 0, 1, 0, 0);
        f->active = 0;
        vs_audio_music_fade(0.3f, 500);   /* ExitLevelUpScene */
    }
}

void vs_item_found_render(vs_game *g) {
    vs_item_found_ui *f = &g->item_found;
    static const char *const confetti[5] = { "PfxYellow.png", "PfxRed.png", "PfxPink.png", "PfxColor1.png", "PfxColor2.png" };
    for (int i = 0; i < 128; i++) {
        vs_ui_particle *c = &f->confetti[i];
        if (c->life > 0) sprite_at(g, g->tex_vfx, &g->atl_vfx, confetti[c->kind], c->x, c->y, 0.5f, 0.5f, c->s0 * (1 - c->age / c->life), c->rot, 0xffffff, 1);
    }
    float bw = 0.8f * g->logical_w, bh = 0.8f * g->logical_h;
    float left = 0.5f * (g->logical_w - bw), top = 0.5f * (g->logical_h - bh);
    float k = clamp01(f->t_ms / 150.0f);
    float cxm = 0.5f * g->logical_w, cym = 0.5f * g->logical_h;
    vs_ui_nine_slice(g, "frame5_c4.png", cxm - 0.5f * bw * k, cym - 0.5f * bh * k, bw * k, bh * k, 12, 12, 0xffffff);
    if (k >= 1) {
        text_centered(cxm, top + 14, 3, 0xffffff, vs_lang_get(&g->lang, "newItem_header", "New item!"));
        float pw = 0.95f * bw, ph = 0.2f * bh, slide = clamp01(f->t_ms / 150.0f);
        float px = left + 0.025f * bw + (1 - slide) * g->logical_w;
        vs_ui_weapon_panel(g, f->type, px, top + ph - 0.025f * bw, pw, ph);
        button(g, "button_c9_normal.png", vs_lang_get(&g->lang, "newItem_get", "TAKE"), cxm, top + 3 * ph - 0.025f * bw, 140, 40, 1, f->t_ms >= 500 && f->sel == 0);
        button(g, "button_c8_normal.png", vs_lang_get(&g->lang, "newItem_discard", "LEAVE"), cxm, top + 4 * ph - 0.025f * bw, 140, 40, 1, f->t_ms >= 500 && f->sel == 1);
    }
    enter_burst(g, f->t_ms);
}
