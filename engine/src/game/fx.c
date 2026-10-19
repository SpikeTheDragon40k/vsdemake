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
    if (g->timestop_fx_ms >= 0) { g->timestop_fx_ms += VS_TICK_MS; if (g->timestop_fx_ms >= 10000) g->timestop_fx_ms = -1; }
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

/* ---- particles ---- */

int vs_pfx_step(vs_pfx *p, float dt_ms) {
    if (p->life <= 0) return 0;
    p->age += dt_ms;
    if (p->age >= p->life) { p->life = 0; return 0; }
    if (p->wpow > 0) {   /* Phaser GravityWell */
        float x = p->wx - p->x, y = p->wy - p->y, dsq = x * x + y * y;
        if (dsq > 0) {
            float d = sqrtf(dsq);
            if (dsq < p->weps2) dsq = p->weps2;
            float f = p->wpow * dt_ms / (dsq * d) * 100;
            p->vx += x * f; p->vy += y * f;
        }
    }
    p->x += p->vx * dt_ms / 1000.0f;
    p->y += p->vy * dt_ms / 1000.0f;
    return 1;
}

/* ---- explosions (self-destructing enemies) ---- */

void vs_explosion_make(vs_game *g, float x, float y, float damage, float radius) {
    for (int i = 0; i < VS_MAX_EXPLOSIONS; i++) {
        vs_explosion *e = &g->explosions[i];
        if (e->active) continue;
        memset(e, 0, sizeof *e);
        e->active = 1; e->x = x; e->y = y; e->damage = damage; e->radius = radius;
        vs_sfx(g, VS_SND_EXPLOSION, 0.5f, 500 * (vs_sfx_random(g) - 0.5f), 1, 150, 3);
        return;
    }
}

/* Two emitters run while the explosion lasts, 4 particles each per frame:
 * smoke (Smoke1/2, 0–180° in 16 steps, 100–200 px/s, alpha 0.5 → 0) and an
 * additive flash (HitSmoke1/2, 0–360° in 16 steps, 50–80 px/s); 1 s, scale
 * 1 → 0, pulled by a gravity well 20 px above (power 1 × gravity 20,
 * epsilon 50). */
static void emit_smoke(vs_game *g, vs_explosion *e) {
    for (int k = 0; k < 8; k++) {
        vs_pfx *p = &g->smoke[g->smoke_next];
        g->smoke_next = (g->smoke_next + 1) % VS_MAX_SMOKE;
        int flash = k >= 4, n = e->emitted / 2 + (k & 3);   /* each emitter's own step counter */
        float ang = (flash ? 22.5f : 11.25f) * (float)(n % 16) * 3.14159265f / 180.0f;
        float sp = flash ? 50 + 30 * vs_rng_f32(&g->ui_rng) : 100 + 100 * vs_rng_f32(&g->ui_rng);
        memset(p, 0, sizeof *p);
        p->x = e->x; p->y = e->y; p->z = e->y;
        p->vx = cosf(ang) * sp; p->vy = sinf(ang) * sp;
        p->life = 1000; p->rot = 360 * vs_rng_f32(&g->ui_rng);
        p->s0 = 1; p->s1 = 0; p->a0 = flash ? 1 : 0.5f; p->a1 = 0;
        p->wx = e->x; p->wy = e->y - 20; p->wpow = 20; p->weps2 = 50 * 50;
        p->frame = (flash ? 2 : 0) + (vs_rng_f32(&g->ui_rng) < 0.5f ? 0 : 1);
        p->add = flash;
    }
    e->emitted += 8;
}

void vs_explosions_update(vs_game *g) {
    const vs_player *pl = &g->player;
    for (int i = 0; i < VS_MAX_EXPLOSIONS; i++) {
        vs_explosion *e = &g->explosions[i];
        if (!e->active) continue;
        if (e->age_ms >= 120) { e->active = 0; continue; }
        float dx = pl->x - e->x, dy = pl->y - e->y;
        if (!e->has_hit && dx * dx + dy * dy <= e->radius * e->radius) {
            e->has_hit = 1;
            vs_player_get_damaged(g, e->damage);
        }
        emit_smoke(g, e);
        e->age_ms += VS_TICK_MS;
    }
    for (int i = 0; i < VS_MAX_SMOKE; i++) vs_pfx_step(&g->smoke[i], VS_TICK_MS);
}

/* Smoke at the explosion's depth, and the ground flash: an additive red
 * circle (alpha 0.4) growing to the blast radius (flashing VFX only). */
void vs_render_explosions(vs_game *g) {
    static const char *const frames[4] = { "Smoke1.png", "Smoke2.png", "HitSmoke1.png", "HitSmoke2.png" };
    vs_frame f[4]; int ok[4];
    for (int k = 0; k < 4; k++) ok[k] = vs_atlas_find(&g->atl_vfx, frames[k], &f[k]) == 0;
    const float py = g->player.y;
    for (int i = 0; i < VS_MAX_SMOKE; i++) {
        const vs_pfx *p = &g->smoke[i];
        if (p->life <= 0 || !ok[p->frame]) continue;
        float t = p->age / p->life, a = p->a0 + (p->a1 - p->a0) * t, s = p->s0 + (p->s1 - p->s0) * t;
        if (s <= 0 || a <= 0) continue;
        vs_render_blend(p->add ? VS_BLEND_ADD : VS_BLEND_ALPHA);
        vs_render_sprite(g, g->tex_vfx, &f[p->frame], p->x, p->y, 0.5f, 0.5f, s, 0, 0, p->rot, 0xffffff, (uint8_t)(255 * a), p->z - py + 1);
    }
    vs_frame c;
    if (g->save.flashing_vfx && vs_atlas_find(&g->atl_vfx, "circle.png", &c) == 0) {
        vs_render_blend(VS_BLEND_ADD);
        for (int i = 0; i < VS_MAX_EXPLOSIONS; i++) {
            const vs_explosion *e = &g->explosions[i];
            if (!e->active) continue;
            float r = 1 + (e->radius - 1) * (e->age_ms < 120 ? e->age_ms / 120.0f : 1);
            vs_render_stretched(g, g->tex_vfx, &c, e->x, e->y, 2 * r, 2 * r, 0, 0xff0000, 102, e->y - py + 1);
        }
    }
    vs_render_blend(VS_BLEND_ALPHA);
}

/* Orologion flash (line 6085), over everything in the world: an additive
 * blue Shockwave3 at the screen centre, turning 359° a second, growing to
 * 1.2 × the screen height in 200 ms and fading from 9 to 10 s; and the
 * whole screen tinted blue (additive, alpha 0 → 0.2 in 500 ms, back to 0
 * from 9.5 to 10 s). */
void vs_render_timestop_flash(vs_game *g) {
    float t = g->timestop_fx_ms;
    if (t < 0) return;
    vs_frame f;
    float w = (float)g->logical_w, h = (float)g->logical_h;
    float sa = t < 500 ? 0.2f * t / 500 : t < 9500 ? 0.2f : 0.2f * (1 - (t - 9500) / 500);
    if (sa > 0) vs_ui_rect_blend(g, 0, 0, w, h, 0x0000ff, (uint8_t)(255 * sa), VS_BLEND_ADD);
    if (vs_atlas_find(&g->atl_vfx, "Shockwave3.png", &f) == 0 && f.src_h > 0) {
        float end = 1.2f * h / f.src_h, s = t < 200 ? 1 + (end - 1) * t / 200 : end;
        float a = t < 9000 ? 1 : 1 - (t - 9000) / 1000;
        vs_sprite sp;
        memset(&sp, 0, sizeof sp);
        sp.tex = g->textures[g->tex_vfx]; sp.sx = (int16_t)f.x; sp.sy = (int16_t)f.y; sp.sw = (int16_t)f.w; sp.sh = (int16_t)f.h;
        sp.scale_x = sp.scale_y = s;
        sp.dx = 0.5f * w + (f.off_x - 0.5f * f.src_w) * s; sp.dy = 0.5f * h + (f.off_y - 0.5f * f.src_h) * s;
        sp.angle_deg = 359 * fmodf(t, 1000) / 1000; sp.tint = 0x0000ff; sp.alpha = (uint8_t)(255 * a); sp.blend = VS_BLEND_ADD;
        vs_plat_draw(&sp);
    }
}
