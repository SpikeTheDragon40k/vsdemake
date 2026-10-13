/* Game state and simulation. Fixed 60 Hz step; rendering reads state only. */
#ifndef VS_GAME_H
#define VS_GAME_H

#include <stdint.h>

#include "core/vs_pack.h"
#include "core/vs_platform.h"
#include "core/vs_rng.h"

#define VS_TICK_MS (1000.0f / 60.0f)

typedef struct {
    float x, y, vx, vy;
    int facing_left;
    float anim_t;         /* ms into the walk animation */
    int walking_frames;
    char sprite_prefix[40]; /* e.g. "Antonio_" */
    float move_speed;     /* stat multiplier */
    int character_enum;
} vs_player;

typedef struct {
    const vs_pack *pack;
    vs_rng rng;
    uint32_t tick;
    float survived_ms;

    /* constants */
    float player_px_speed, enemy_speed, projectile_speed;

    vs_player player;

    /* render resources */
    vs_atlas chars_atlas;
    vs_texture *chars_tex;
    vs_texture *bg_tex;
    int logical_w, logical_h;
} vs_game;

int vs_game_init(vs_game *g, const vs_pack *pack, int logical_w, int logical_h, uint64_t seed);
void vs_game_update(vs_game *g, const vs_pad *pad);
void vs_game_render(const vs_game *g);
void vs_game_shutdown(vs_game *g);

#endif
