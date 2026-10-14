#include <math.h>
#include <string.h>

#include "core/vs_enums.h"
#include "game/game.h"

/* notes/behaviour/player.md */

static void apply_row(vs_player *p, const vs_characters_rec *c, int set) {
#define STAT(field, name) if (VS_HAS(c, VS_CHARACTERS_F_##name)) { if (set) p->field = c->field; else p->field += c->field; }
    STAT(maxHp, MAXHP) STAT(power, POWER) STAT(area, AREA) STAT(speed, SPEED) STAT(cooldown, COOLDOWN) STAT(amount, AMOUNT)
    STAT(moveSpeed, MOVESPEED) STAT(growth, GROWTH) STAT(duration, DURATION) STAT(luck, LUCK) STAT(greed, GREED)
    STAT(armor, ARMOR) STAT(regen, REGEN)
#undef STAT
}

int vs_player_init(vs_game *g, int character_enum) {
    vs_player *p = &g->player;
    memset(p, 0, sizeof *p);
    /* construction defaults */
    p->hp = p->maxHp = 100; p->power = p->area = p->speed = p->cooldown = p->moveSpeed = p->growth = p->duration = p->luck = p->greed = 1;
    p->shieldInvulTime = 240; p->level = 1; p->startingWeapon = VS_WEAPONS_WHIP; p->last_dir_x = 1;
    p->character_enum = character_enum;

    int gi = vs_table_find_group_enum(&g->characters_table, character_enum);
    if (gi < 0) return -1;
    vs_group grp;
    vs_table_group(&g->characters_table, (uint32_t)gi, &grp);
    vs_characters_rec c;
    vs_table_record(&g->characters_table, grp.first, &c);

    /* MakeLevelOne: level 0, copy row 0, hp = maxHp */
    p->level = 0;
    apply_row(p, &c, 1);
    if (VS_HAS(&c, VS_CHARACTERS_F_LEVEL)) p->level = c.level;
    if (VS_HAS(&c, VS_CHARACTERS_F_STARTINGWEAPON)) p->startingWeapon = c.startingWeapon;
    p->hp = p->maxHp;
    if (VS_HAS(&c, VS_CHARACTERS_F_MAGNET)) p->magnet_bonus = c.magnet;
    p->walking_frames = VS_HAS(&c, VS_CHARACTERS_F_WALKINGFRAMES) ? c.walkingFrames : 0;
    const char *sprite = vs_table_str(&g->characters_table, c.spriteName);
    const char *suffix = strstr(sprite, "01.png");
    size_t plen = suffix ? (size_t)(suffix - sprite) : strlen(sprite);
    if (plen >= sizeof p->sprite_prefix) plen = sizeof p->sprite_prefix - 1;
    memcpy(p->sprite_prefix, sprite, plen);
    p->sprite_prefix[plen] = 0;

    /* Collider: circle r=9 at offset (6,10) from the sprite's top-left, sprite
     * origin (0.5, 1). Centre relative to (x, y): (-w/2 + 6 + 9, -h + 10 + 9). */
    vs_frame f;
    if (vs_atlas_find(&g->atl_characters, sprite, &f) == 0) {
        p->body_r = 9;
        p->body_ox = -f.src_w * 0.5f + 15;
        p->body_oy = (float)(-f.src_h + 19);
    } else {
        p->body_r = 9; p->body_ox = 0; p->body_oy = -9;
    }
    return 0;
}

void vs_player_update(vs_game *g, const vs_pad *pad) {
    vs_player *p = &g->player;
    if (p->is_dead) return;
    /* Input → velocity (notes/behaviour/loop.md). */
    float dx = 0, dy = 0;
    if (pad->buttons & VS_BTN_LEFT) dx = -1;
    else if (pad->buttons & VS_BTN_RIGHT) dx = 1;
    if (pad->buttons & VS_BTN_UP) dy = -1;
    else if (pad->buttons & VS_BTN_DOWN) dy = 1;
    float max_speed = g->player_px_speed * p->moveSpeed;
    if (dx != 0 || dy != 0) {
        float len = sqrtf(dx * dx + dy * dy);
        p->vx = dx / len * max_speed;
        p->vy = dy / len * max_speed;
    } else {
        p->vx = fabsf(pad->lx) > 0.3f ? pad->lx * max_speed : 0;
        p->vy = fabsf(pad->ly) > 0.3f ? pad->ly * max_speed : 0;
    }
    if (p->vx != 0 || p->vy != 0) { p->last_dir_x = p->vx; p->last_dir_y = p->vy; }
    p->x += p->vx * (VS_TICK_MS / 1000.0f);
    p->y += p->vy * (VS_TICK_MS / 1000.0f);
    { float cx = p->x + p->body_ox, cy = p->y + p->body_oy; vs_tilemap_collide(g, &cx, &cy, p->body_r, 3); p->x = cx - p->body_ox; p->y = cy - p->body_oy; }

    /* Player.Update */
    p->invul_ms -= VS_TICK_MS;
    if (p->invul_ms < 0) p->invul_ms = 0;
    if (p->vx < 0) p->facing_left = 1;
    if (p->vx > 0) p->facing_left = 0;
    if (p->vx != 0 || p->vy != 0) p->anim_t += VS_TICK_MS;
}

int vs_player_is_invul(const vs_game *g) { return g->player.invul_ms > 0; }

int vs_player_level_up(vs_game *g) {
    vs_player *p = &g->player;
    p->level++;
    int gi = vs_table_find_group_enum(&g->characters_table, p->character_enum);
    if (gi < 0) return 0;
    int had_cooldown = 0;
    vs_group grp;
    vs_table_group(&g->characters_table, (uint32_t)gi, &grp);
    for (uint32_t i = 0; i < grp.count; i++) {
        vs_characters_rec c;
        vs_table_record(&g->characters_table, grp.first + i, &c);
        if (!VS_HAS(&c, VS_CHARACTERS_F_LEVEL) || c.level != p->level) continue;
        apply_row(p, &c, 0);
        had_cooldown = VS_HAS(&c, VS_CHARACTERS_F_COOLDOWN);
        break;
    }
    return had_cooldown;
}

void vs_player_recover_hp(vs_game *g, float n) {
    vs_player *p = &g->player;
    if (p->is_dead) return;
    float heal = p->maxHp - p->hp;
    if (n < heal) heal = n;
    g->recovered_hp += heal;
    if (heal < 9000) g->run_heal += heal;
    p->hp += heal;
    if (p->hp > p->maxHp) p->hp = p->maxHp;
}

static void blink(vs_game *g, float ms) {
    g->player.receiving_damage = 1;
    vs_timer_cancel(g, VS_TIMER_PLAYER_BLINK, 0);
    vs_timer_add(g, VS_TIMER_PLAYER_BLINK, ms, 0, 0, 0);
}

void vs_player_get_damaged(vs_game *g, float dmg) {
    vs_player *p = &g->player;
    if (p->receiving_damage || vs_player_is_invul(g) || p->hp <= 0) return;
    if (p->shields > 0) {
        p->shields--;
        blink(g, p->shieldInvulTime);
        return;
    }
    if (p->armor > 0) { dmg -= p->armor; if (dmg < 1) dmg = 1; }
    p->hp -= dmg;
    vs_sfx(g, VS_SND_HIT, 0.4f, 1000 + 500 * vs_sfx_random(g), 1, 150, 3);
    vs_sfx(g, VS_SND_LOSS, 0.1f, -500 * vs_sfx_random(g) - 500, 1, 450, 1);
    if (p->hp <= 0) {
        p->hp = 0;
        p->is_dead = 1;
        g->game_over = 1;
    } else {
        blink(g, 120);
    }
}
