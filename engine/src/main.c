/* Entry point: fixed 60 Hz simulation, render every frame.
 *
 *   vsdemake-sdl2 <vsdata.vspack> [--scale N] [--headless --frames N --screenshot out.bmp]
 *                                 [--input "RRRRDDLL"] [--seed N]
 *
 * --input feeds one direction per tick (U/D/L/R, '.' = none) for headless
 * runs and replays. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/vs_pack.h"
#include "core/vs_platform.h"
#include "game/game.h"

#define LOGICAL_W 810
#define LOGICAL_H 456

int main(int argc, char **argv) {
    const char *pack_path = NULL, *screenshot = NULL, *input = NULL;
    int scale = 2, headless = 0, frames = -1;
    uint64_t seed = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) scale = atoi(argv[++i]);
        else if (strcmp(argv[i], "--headless") == 0) headless = 1;
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot = argv[++i];
        else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) input = argv[++i];
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (argv[i][0] != '-') pack_path = argv[i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
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
    if (vs_game_init(&game, &pack, LOGICAL_W, LOGICAL_H, seed) != 0) { fprintf(stderr, "game init failed\n"); return 1; }

    const float tick_ms = VS_TICK_MS;
    uint64_t prev = vs_plat_time_ms();
    float acc = 0;
    size_t input_pos = 0;
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
            if (input) {
                char c = input[input_pos] ? input[input_pos++] : '.';
                pad.buttons = 0;
                if (c == 'U') pad.buttons |= VS_BTN_UP;
                if (c == 'D') pad.buttons |= VS_BTN_DOWN;
                if (c == 'L') pad.buttons |= VS_BTN_LEFT;
                if (c == 'R') pad.buttons |= VS_BTN_RIGHT;
            }
            vs_game_update(&game, &pad);
        }
        vs_game_render(&game);
        rendered++;
        if (frames >= 0 && rendered >= frames) break;
        if (!headless && steps == 0) vs_plat_sleep_ms(1);
    }
    if (screenshot) {
        if (vs_plat_screenshot(screenshot) == 0) vs_plat_log("wrote %s", screenshot);
        else vs_plat_log("screenshot failed");
    }
    vs_plat_log("ticks %u, player at (%.1f, %.1f)", game.tick, game.player.x, game.player.y);
    vs_game_shutdown(&game);
    vs_plat_free(data);
    vs_plat_shutdown();
    return 0;
}
