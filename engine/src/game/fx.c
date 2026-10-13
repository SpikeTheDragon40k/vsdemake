#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/vs_enums.h"
#include "game/game.h"

/* Damage numbers (original: BitmapText pool, line 6174) and hit flashes. */

void vs_fx_damage_number(vs_game *g, float x, float y, float dmg) {
    int alive = 0;
    for (int i = 0; i < VS_MAX_FX; i++) alive += g->fx[i].active && g->fx[i].kind == VS_FX_NUMBER;
    float dur = 300;
    if (dmg > 0 && alive > 500) dur = 150;
    else if (dmg > 0 && alive > 1000) return;
    for (int i = 0; i < VS_MAX_FX; i++) {
        vs_fx *f = &g->fx[i];
        if (f->active) continue;
        memset(f, 0, sizeof *f);
        float j = vs_rng_f32(&g->rng) - 0.5f;
        f->active = 1; f->kind = VS_FX_NUMBER;
        f->x = x + 32 * j; f->y = y - 16; f->y0 = f->y; f->rise = 12 * vs_rng_f32(&g->rng);
        f->value = (int)floorf(10 * (dmg + j) + 0.5f);
        f->dur = dur;
        f->tint = 0xffffff;
        if (dmg >= 10) f->tint = 0xffff00;
        else if (dmg >= 6) f->tint = 0xff0000 | ((uint32_t)(dmg / 10 * 255) << 8) | 0x80;
        else if (dmg >= 3) f->tint = 0xff0000 | ((uint32_t)(dmg / 10 * 255) << 8) | (uint32_t)(dmg / 6 * 128);
        else if (dmg < 0) { f->tint = 0x00ff00; f->value = -f->value; }
        return;
    }
}

void vs_fx_hit(vs_game *g, float x, float y, int hit_type) {
    int gi = vs_table_find_group_enum(&g->hitvfx_table, hit_type);
    if (gi < 0) return;
    vs_group grp;
    vs_table_group(&g->hitvfx_table, (uint32_t)gi, &grp);
    vs_hitvfx_rec h;
    vs_table_record(&g->hitvfx_table, grp.first, &h);
    const char *frame = vs_table_str(&g->hitvfx_table, h.hitFrameName);
    if (!frame[0] || strcmp(frame, "NoDraw.png") == 0) return;
    for (int i = 0; i < VS_MAX_FX; i++) {
        vs_fx *f = &g->fx[i];
        if (f->active) continue;
        memset(f, 0, sizeof *f);
        f->active = 1; f->kind = VS_FX_HIT; f->x = x; f->y = y; f->dur = 120; f->tint = 0xffffff;
        strncpy(f->frame, frame, sizeof f->frame - 1);
        return;
    }
}

void vs_fx_update(vs_game *g) {
    for (int i = 0; i < VS_MAX_FX; i++) {
        vs_fx *f = &g->fx[i];
        if (!f->active) continue;
        f->age += VS_TICK_MS;
        if (f->kind == VS_FX_NUMBER) {
            if (f->age >= f->dur + 600) f->active = 0;
        } else if (f->age >= f->dur) f->active = 0;
    }
}

void vs_fx_render(vs_game *g, float cam_x, float cam_y) {
    for (int i = 0; i < VS_MAX_FX; i++) {
        vs_fx *f = &g->fx[i];
        if (!f->active) continue;
        if (f->kind == VS_FX_HIT) {
            vs_frame fr;
            if (vs_atlas_find(&g->atl_vfx, f->frame, &fr) == 0)
                vs_render_sprite(g, g->tex_vfx, &fr, f->x, f->y, 0.5f, 0.5f, 1.0f, 0, 0, 0, f->tint, (uint8_t)(255 * (1 - f->age / f->dur)), 1e6f);
            continue;
        }
        float t = f->age < f->dur ? f->age / f->dur : 1;
        float scale = 1 + t;
        float y = f->y0 - f->rise * t;
        uint8_t alpha = 255;
        if (f->age > f->dur) { float t2 = (f->age - f->dur) / 600.0f; alpha = (uint8_t)(255 * (1 - t2)); scale = 2 - t2; }
        char txt[16];
        snprintf(txt, sizeof txt, "%d", f->value);
        /* BMFont digits, origin (0.5, 1) */
        float w = 0;
        for (const char *c = txt; *c; c++) { vs_glyph gl; if (vs_font_glyph(&g->damage_font, (uint32_t)*c, &gl) == 0) w += gl.xadvance * scale; }
        float x = f->x - w * 0.5f;
        for (const char *c = txt; *c; c++) {
            vs_glyph gl;
            if (vs_font_glyph(&g->damage_font, (uint32_t)*c, &gl) != 0) continue;
            vs_frame fr = {0};
            fr.x = gl.x; fr.y = gl.y; fr.w = gl.w; fr.h = gl.h; fr.src_w = gl.w; fr.src_h = gl.h;
            vs_render_sprite(g, g->tex_damage, &fr, x + gl.xoff * scale, y - gl.h * scale, 0, 0, scale, 0, 0, 0, f->tint, alpha, 1e6f);
            x += gl.xadvance * scale;
        }
        (void)cam_x; (void)cam_y;
    }
}
