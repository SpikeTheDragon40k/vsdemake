#include "game/game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/vs_enums.h"
#include "core/vs_tables.h"

static float table_constant(const vs_pack *pack, const char *name, float dflt) {
    vs_table t;
    if (vs_pack_table(pack, "constants", &t) != 0) return dflt;
    int g = vs_table_find_group(&t, name);
    if (g < 0) return dflt;
    vs_group grp;
    vs_table_group(&t, (uint32_t)g, &grp);
    vs_constants_rec r;
    vs_table_record(&t, grp.first, &r);
    return r.value;
}

static vs_texture *load_texture(const vs_pack *pack, int entry) {
    vs_tex_info ti;
    if (vs_pack_tex(pack, entry, &ti) != 0) return NULL;
    return vs_plat_texture_create(ti.w, ti.h, ti.pixels);
}

int vs_game_init(vs_game *g, const vs_pack *pack, int lw, int lh, uint64_t seed) {
    memset(g, 0, sizeof *g);
    g->pack = pack;
    g->logical_w = lw;
    g->logical_h = lh;
    vs_rng_seed(&g->rng, seed);

    g->player_px_speed = table_constant(pack, "PlayerPxSpeed", 82.5f);
    g->enemy_speed = table_constant(pack, "EnemySpeed", 0.231f);
    g->projectile_speed = table_constant(pack, "ProjectileSpeed", 165.0f);

    /* Character: Antonio, row 0 (notes/behaviour/player.md, MakeLevelOne). */
    vs_table chars;
    if (vs_pack_table(pack, "characters", &chars) != 0 || vs_table_check_fields(&chars, VS_CHARACTERS_FIELD_NAMES) != 0) {
        vs_plat_log("characters table missing or layout mismatch");
        return -1;
    }
    int gi = vs_table_find_group_enum(&chars, VS_CHARACTERS_ANTONIO);
    if (gi < 0) return -1;
    vs_group grp;
    vs_table_group(&chars, (uint32_t)gi, &grp);
    vs_characters_rec c;
    vs_table_record(&chars, grp.first, &c);
    g->player.character_enum = VS_CHARACTERS_ANTONIO;
    g->player.move_speed = VS_HAS(&c, VS_CHARACTERS_F_MOVESPEED) ? c.moveSpeed : 1.0f;
    g->player.walking_frames = VS_HAS(&c, VS_CHARACTERS_F_WALKINGFRAMES) ? c.walkingFrames : 0;
    /* spriteName "Antonio_01.png" → prefix "Antonio_" */
    const char *sprite = vs_table_str(&chars, c.spriteName);
    const char *suffix = strstr(sprite, "01.png");
    size_t plen = suffix ? (size_t)(suffix - sprite) : strlen(sprite);
    if (plen >= sizeof g->player.sprite_prefix) plen = sizeof g->player.sprite_prefix - 1;
    memcpy(g->player.sprite_prefix, sprite, plen);
    g->player.sprite_prefix[plen] = 0;

    int atl = vs_pack_find(pack, "ATL", "characters");
    if (atl < 0 || vs_pack_atlas(pack, atl, &g->chars_atlas) != 0) return -1;
    g->chars_tex = load_texture(pack, g->chars_atlas.tex_entry);
    g->bg_tex = load_texture(pack, vs_pack_find(pack, "TEX", "bg_forest"));
    if (!g->chars_tex || !g->bg_tex) return -1;
    return 0;
}

void vs_game_update(vs_game *g, const vs_pad *pad) {
    vs_player *p = &g->player;
    /* Input → direction (notes/behaviour/loop.md step 2): d-pad/keys
     * normalised; stick axes with a 0.3 dead zone, not normalised. */
    float dx = 0, dy = 0;
    if (pad->buttons & VS_BTN_LEFT) dx = -1;
    else if (pad->buttons & VS_BTN_RIGHT) dx = 1;
    if (pad->buttons & VS_BTN_UP) dy = -1;
    else if (pad->buttons & VS_BTN_DOWN) dy = 1;
    float max_speed = g->player_px_speed * p->move_speed;
    if (dx != 0 || dy != 0) {
        float len = sqrtf(dx * dx + dy * dy);
        p->vx = dx / len * max_speed;
        p->vy = dy / len * max_speed;
    } else {
        p->vx = fabsf(pad->lx) > 0.3f ? pad->lx * max_speed : 0;
        p->vy = fabsf(pad->ly) > 0.3f ? pad->ly * max_speed : 0;
    }
    p->x += p->vx * (VS_TICK_MS / 1000.0f);
    p->y += p->vy * (VS_TICK_MS / 1000.0f);
    if (p->vx < 0) p->facing_left = 1;
    if (p->vx > 0) p->facing_left = 0;
    if (p->vx != 0 || p->vy != 0) p->anim_t += VS_TICK_MS; /* walk anim at 8 fps while moving, paused otherwise */

    g->tick++;
    g->survived_ms += VS_TICK_MS;
}

void vs_game_render(const vs_game *g) {
    const vs_player *p = &g->player;
    float cam_x = p->x - g->logical_w * 0.5f, cam_y = p->y - g->logical_h * 0.5f;

    vs_plat_begin_frame(0x000000);
    /* Background scrolls 1:1 with the player (notes/behaviour/stage-map.md). */
    vs_plat_draw_tiled(g->bg_tex, p->x, p->y, 0xffffff);

    /* Player sprite: origin (0.5, 1), frames <prefix>01..NN at 8 fps. */
    char name[64];
    int frame = 1;
    if (p->walking_frames > 0) frame = 1 + (int)(p->anim_t / 125.0f) % p->walking_frames;
    snprintf(name, sizeof name, "%s%02d.png", p->sprite_prefix, frame);
    vs_frame f;
    if (vs_atlas_find(&g->chars_atlas, name, &f) == 0) {
        vs_sprite s = {0};
        s.tex = g->chars_tex;
        s.sx = (int16_t)f.x; s.sy = (int16_t)f.y; s.sw = (int16_t)f.w; s.sh = (int16_t)f.h;
        /* trimmed frame offset inside the source sprite, mirrored when flipped */
        float off_x = p->facing_left ? (float)(f.src_w - f.off_x - f.w) : (float)f.off_x;
        s.dx = p->x - cam_x - f.src_w * 0.5f + off_x;
        s.dy = p->y - cam_y - f.src_h + f.off_y;
        s.scale_x = s.scale_y = 1.0f;
        s.alpha = 255;
        s.tint = 0xffffff;
        s.flip = p->facing_left ? VS_FLIP_X : 0;
        vs_plat_draw(&s);
    }
    vs_plat_end_frame();
}

void vs_game_shutdown(vs_game *g) {
    vs_plat_texture_destroy(g->chars_tex);
    vs_plat_texture_destroy(g->bg_tex);
    g->chars_tex = g->bg_tex = NULL;
}
