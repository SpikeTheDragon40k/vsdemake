/* Entry point: fixed 60 Hz simulation, render every frame.
 *
 *   vsdemake-sdl2 <vsdata.vspack> [--scale N] [--headless --frames N --screenshot out.bmp] [--auto-pick]
 *                                 [--input "RRRRDDLL"] [--seed N]
 *
 * --no-render skips drawing except the last frame (fast headless runs).
 * --input is a scripted pad for headless runs and replays: tokens like
 * "R120 D120 L120 U120" (direction letters U/D/L/R, '.' = idle, optional
 * tick count), looped when exhausted. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/vs_pack.h"
#include "core/vs_platform.h"
#include "game/game.h"

#define LOGICAL_W 810
#define LOGICAL_H 456

/* Reads the next token of the input script; returns the pad buttons for one tick. */
static uint32_t scripted_buttons(const char *script, size_t *pos, int *left) {
    static uint32_t current;
    if (*left <= 0) {
        for (int wrapped = 0; wrapped < 2; wrapped++) {
            while (script[*pos] == ' ') (*pos)++;
            if (!script[*pos]) { *pos = 0; continue; }
            current = 0;
            for (; script[*pos] && script[*pos] != ' ' && (script[*pos] < '0' || script[*pos] > '9'); (*pos)++) {
                char c = script[*pos];
                if (c == 'U') current |= VS_BTN_UP;
                if (c == 'D') current |= VS_BTN_DOWN;
                if (c == 'L') current |= VS_BTN_LEFT;
                if (c == 'R') current |= VS_BTN_RIGHT;
                if (c == 'A') current |= VS_BTN_A;
            }
            *left = script[*pos] >= '0' && script[*pos] <= '9' ? (int)strtol(script + *pos, NULL, 10) : 1;
            while (script[*pos] >= '0' && script[*pos] <= '9') (*pos)++;
            break;
        }
    }
    (*left)--;
    return current;
}

#ifdef VS_XENON
/* No command line on the console: look for the pack next to the ELF on USB or HDD. */
static const char *xenon_find_pack(void) {
    static const char *candidates[] = { "uda:/vsdemake/vsdata.vspack", "uda:/vsdata.vspack", "sda:/vsdemake/vsdata.vspack", "sda:/vsdata.vspack", "udb:/vsdata.vspack", NULL };
    for (int i = 0; candidates[i]; i++) { FILE *f = fopen(candidates[i], "rb"); if (f) { fclose(f); return candidates[i]; } }
    return NULL;
}
#endif

int main(int argc, char **argv) {
    const char *pack_path = NULL, *screenshot = NULL, *input = NULL;
    int scale = 2, headless = 0, frames = -1, auto_pick = 0, no_render = 0, stage = 0;
    uint64_t seed = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) scale = atoi(argv[++i]);
        else if (strcmp(argv[i], "--headless") == 0) headless = 1;
        else if (strcmp(argv[i], "--auto-pick") == 0) auto_pick = 1;
        else if (strcmp(argv[i], "--no-render") == 0) no_render = 1;
        else if (strcmp(argv[i], "--stage") == 0 && i + 1 < argc) stage = strcmp(argv[++i], "library") == 0;
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot = argv[++i];
        else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) input = argv[++i];
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (argv[i][0] != '-') pack_path = argv[i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
#ifdef VS_XENON
    if (!pack_path) pack_path = xenon_find_pack();
    scale = 1;
#endif
    if (!pack_path) { fprintf(stderr, "usage: vsdemake-sdl2 <vsdata.vspack> [options]\n"); return 2; }

    vs_plat_config cfg = { LOGICAL_W, LOGICAL_H, scale, headless, "vsdemake" };
    if (vs_plat_init(&cfg) != 0) return 1;

    size_t size;
    void *data = vs_plat_read_file(pack_path, &size);
    if (!data) { fprintf(stderr, "cannot read %s\n", pack_path); return 1; }
    vs_pack pack;
    int rc = vs_pack_init(&pack, data, size);
    if (rc != 0) { fprintf(stderr, "%s is not a valid pack (error %d)\n", pack_path, rc); return 1; }
    vs_plat_log("pack: %u entries, manifest %llu", pack.count, (unsigned long long)pack.manifest);

    vs_game game;
    memset(&game, 0, sizeof game);
    game.auto_pick = auto_pick || headless;
    game.menu_stage = stage;
    if (vs_game_init(&game, &pack, LOGICAL_W, LOGICAL_H, seed) != 0) { fprintf(stderr, "game init failed\n"); return 1; }

    const float tick_ms = VS_TICK_MS;
    uint64_t prev = vs_plat_time_ms();
    float acc = 0;
    size_t input_pos = 0;
    int input_left = 0;
    int rendered = 0;
    for (;;) {
        vs_pad pad = {0};
        if (!vs_plat_poll(&pad)) break;
        int steps;
        if (headless) {
            steps = 1; /* one tick per frame, no wall clock */
        } else {
            uint64_t now = vs_plat_time_ms();
            acc += (float)(now - prev);
            prev = now;
            if (acc > 250) acc = 250; /* don't spiral after a stall */
            steps = 0;
            while (acc >= tick_ms) { acc -= tick_ms; steps++; }
        }
        for (int s = 0; s < steps; s++) {
            if (input) pad.buttons = scripted_buttons(input, &input_pos, &input_left);
            vs_game_update(&game, &pad);
        }
        if (!no_render || (frames >= 0 && rendered + 1 >= frames)) vs_game_render(&game);
        rendered++;
        if (frames >= 0 && rendered >= frames) break;
        if (!headless && steps == 0) vs_plat_sleep_ms(1);
    }
    if (screenshot) {
        if (vs_plat_screenshot(screenshot) == 0) vs_plat_log("wrote %s", screenshot);
        else vs_plat_log("screenshot failed");
    }
    vs_plat_log("ticks %u, %02d:%02d, player at (%.1f, %.1f) hp %.0f lv %d kills %u gold %.0f, state hash %08x%s", game.tick, game.survived_seconds / 60, game.survived_seconds % 60, game.player.x, game.player.y, game.player.hp, game.player.level, game.kills, game.coins, vs_game_state_hash(&game), game.game_over ? " GAME OVER" : "");
    vs_game_shutdown(&game);
    vs_plat_free(data);
    vs_plat_shutdown();
    return 0;
}
