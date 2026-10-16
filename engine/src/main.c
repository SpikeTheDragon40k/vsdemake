/* Entry point: fixed 60 Hz simulation, render every frame.
 *
 *   vsdemake-sdl2 <vsdata.vspack> [--scale N] [--headless --frames N --screenshot out.bmp] [--auto-pick]
 *                                 [--input "RRRRDDLL"] [--seed N]
 *
 * --no-render skips drawing except the last frame (fast headless runs).
 * --input is a scripted pad for headless runs and replays: tokens like
 * "R120 D120 L120 U120" (direction letters U/D/L/R, buttons A/B/X/Y and
 * S = start, '.' = idle, optional tick count), looped when exhausted. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/vs_audio.h"
#include "core/vs_compat.h"
#include "core/vs_pack.h"
#include "core/vs_platform.h"
#include "core/vs_text.h"
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
                if (c == 'B') current |= VS_BTN_B;
                if (c == 'X') current |= VS_BTN_X;
                if (c == 'Y') current |= VS_BTN_Y;
                if (c == 'S') current |= VS_BTN_START;
            }
            *left = script[*pos] >= '0' && script[*pos] <= '9' ? (int)strtol(script + *pos, NULL, 10) : 1;
            while (script[*pos] >= '0' && script[*pos] <= '9') (*pos)++;
            break;
        }
    }
    (*left)--;
    return current;
}

#if defined(VS_XENON) || defined(VS_XDK)
/* No command line on the console: look for the pack next to the ELF on USB or HDD. */
static const char *xenon_find_pack(void) {
#ifdef VS_XDK
    /* game:\ is the folder the XEX was launched from */
    static const char *candidates[] = { "game:\\vsdata.vspack", "game:\\data\\vsdata.vspack", "hdd1:\\vsdemake\\vsdata.vspack", "usb0:\\vsdemake\\vsdata.vspack", NULL };
#else
    static const char *candidates[] = { "uda:/vsdemake/vsdata.vspack", "uda:/vsdata.vspack", "sda:/vsdemake/vsdata.vspack", "sda:/vsdata.vspack", "udb:/vsdata.vspack", NULL };
#endif
    for (int i = 0; candidates[i]; i++) { FILE *f = fopen(candidates[i], "rb"); if (f) { fclose(f); return candidates[i]; } }
    return NULL;
}

/* Shows an error on screen until Back is pressed. On the console there is no
 * stderr, so without this a boot failure just drops back to the dashboard.
 * Uses only the built-in font, so it works without the pack. */
static int fatal_screen(const char *msg) {
    if (vs_text_init() != 0) return 1;
    for (;;) {
        vs_pad pad;
        memset(&pad, 0, sizeof pad);
        if (!vs_plat_poll(&pad) || (pad.buttons & VS_BTN_BACK)) break;
        vs_plat_begin_frame(0x400000);
        vs_text_draw(40, 40, 2, 0xffffff, "vsdemake: cannot start");
        vs_text_draw(40, 80, 1.5f, 0xffff80, msg);
        vs_text_draw(40, 400, 1.5f, 0xc0c0c0, "Press BACK to return to the dashboard");
        vs_plat_end_frame();
        vs_plat_sleep_ms(16);
    }
    vs_text_shutdown();
    return 1;
}
#endif

static int vs_main(int argc, char **argv) {
    const char *map_item = NULL, *drop = NULL;
    const char *pack_path = NULL, *screenshot = NULL, *input = NULL, *record_dir = NULL, *save_arg = NULL, *audio_dump_path = NULL;
    int mute = 0;
    int scale = 2, headless = 0, frames = -1, auto_pick = 0, no_render = 0, stage = 0, hitboxes = 0, character = 0, menu = 0, record_from = 0, unlock_all = 0, show_gameover = 0, show_chest = 0, start_level = 0, give_count = 0, chest = 0, show_levelup = 0;
    const char *give[16];
    uint64_t seed = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) scale = atoi(argv[++i]);
        else if (strcmp(argv[i], "--headless") == 0) headless = 1;
        else if (strcmp(argv[i], "--auto-pick") == 0) auto_pick = 1;
        else if (strcmp(argv[i], "--no-render") == 0) no_render = 1;
        else if (strcmp(argv[i], "--hitboxes") == 0) hitboxes = 1;
        else if (strcmp(argv[i], "--menu") == 0) menu = 1;
        else if (strcmp(argv[i], "--unlock-all") == 0) unlock_all = 1;
        else if (strcmp(argv[i], "--show-gameover") == 0) show_gameover = 1;
        else if (strcmp(argv[i], "--show-chest") == 0) show_chest = 1;
        else if (strcmp(argv[i], "--map-item") == 0 && i + 1 < argc) map_item = argv[++i];
        else if (strcmp(argv[i], "--drop") == 0 && i + 1 < argc) drop = argv[++i];
        else if (strcmp(argv[i], "--level") == 0 && i + 1 < argc) start_level = atoi(argv[++i]);
        else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) save_arg = argv[++i];
        else if (strcmp(argv[i], "--chest") == 0) { chest = 1; if (i + 1 < argc && argv[i + 1][0] >= '1' && argv[i + 1][0] <= '3' && !argv[i + 1][1]) chest = argv[++i][0] - '0'; }
        else if (strcmp(argv[i], "--show-levelup") == 0) show_levelup = 1;
        else if (strcmp(argv[i], "--give") == 0 && i + 1 < argc) { if (give_count < 16) give[give_count++] = argv[++i]; else ++i; }
        else if (strcmp(argv[i], "--char") == 0 && i + 1 < argc) character = atoi(argv[++i]);
        else if (strcmp(argv[i], "--stage") == 0 && i + 1 < argc) stage = strcmp(argv[++i], "library") == 0;
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot = argv[++i];
        else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) record_dir = argv[++i];
        else if (strcmp(argv[i], "--record-from") == 0 && i + 1 < argc) record_from = atoi(argv[++i]);
        else if (strcmp(argv[i], "--audio-dump") == 0 && i + 1 < argc) audio_dump_path = argv[++i];
        else if (strcmp(argv[i], "--mute") == 0) mute = 1;
        else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) input = argv[++i];
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (argv[i][0] != '-') pack_path = argv[i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
#if defined(VS_XENON) || defined(VS_XDK)
    scale = 1;
#else
    if (!pack_path) { fprintf(stderr, "usage: vsdemake-sdl2 <vsdata.vspack> [options]\n"); return 2; }
#endif

    vs_plat_config cfg = { LOGICAL_W, LOGICAL_H, scale, headless, "vsdemake" };
    if (vs_plat_init(&cfg) != 0) return 1;
#if defined(VS_XENON) || defined(VS_XDK)
    /* look for the pack after the platform is up, so a miss can be shown */
    if (!pack_path) pack_path = xenon_find_pack();
    if (!pack_path) return fatal_screen("vsdata.vspack not found.\n\nPut it in the same folder as default.xex\n(game:\\vsdata.vspack), then relaunch.");
#endif

    size_t size;
    void *data = vs_plat_read_file(pack_path, &size);
    if (!data) {
        fprintf(stderr, "cannot read %s\n", pack_path);
#if defined(VS_XENON) || defined(VS_XDK)
        return fatal_screen("vsdata.vspack not found or unreadable.\n\nPut it in the same folder as default.xex\n(game:\\vsdata.vspack), then relaunch.");
#endif
        return 1;
    }
    vs_pack pack;
    int rc = vs_pack_init(&pack, data, size);
    if (rc != 0) {
        fprintf(stderr, "%s is not a valid pack (error %d)\n", pack_path, rc);
#if defined(VS_XENON) || defined(VS_XDK)
        char msg[160];
        snprintf(msg, sizeof msg, "vsdata.vspack is damaged or from another\nversion (error %d). Rebuild it with vspatch\nand copy it again.", rc);
        return fatal_screen(msg);
#endif
        return 1;
    }
    vs_plat_log("pack: %u entries, manifest %llu", pack.count, (unsigned long long)pack.manifest);

    /* static: vs_game is ~600 KB, far beyond a console thread's stack */
    static vs_game game;
    memset(&game, 0, sizeof game);
    game.auto_pick = (auto_pick || headless) && !menu;   /* --menu: stay on the title */
    game.menu_stage = stage;
    game.debug_hitboxes = hitboxes;
    game.unlock_all = unlock_all;
    game.show_gameover = show_gameover;
    game.show_chest = show_chest;
    /* The save lives next to the pack (game:\\vsdemake.sav on the console).
     * Headless runs only save with an explicit --save, so tests stay repeatable. */
    if (save_arg) snprintf(game.save_path, sizeof game.save_path, "%s", save_arg);
    else if (!headless) {
        const char *slash = NULL;
        for (const char *c = pack_path; *c; c++) if (*c == '/' || *c == '\\' || *c == ':') slash = c;
        int dir_len = slash ? (int)(slash - pack_path + 1) : 0;
        snprintf(game.save_path, sizeof game.save_path, "%.*svsdemake.sav", dir_len, pack_path);
    }
    if (character > 0) game.selected_character = character;
    /* Sounds: loaded unless the run is headless (then only for --audio-dump). */
    int audio_live = 0;
    FILE *audio_dump = NULL;
    int sounds = 0;
    if (!mute && (!headless || audio_dump_path)) sounds = vs_audio_init(&pack);
    if (!mute && !headless) audio_live = vs_plat_audio_open(VS_AUDIO_RATE) == 0;
    if (mute) snprintf(game.audio_status, sizeof game.audio_status, "Sound: muted");
    else if (headless && audio_dump_path) snprintf(game.audio_status, sizeof game.audio_status, "Sound: %d sounds, recording", sounds);
    else if (!headless) {
        if (sounds == 0) snprintf(game.audio_status, sizeof game.audio_status, "Sound: NO SOUNDS IN THE PACK (rebuild vsdata.vspack)");
        else if (!audio_live) snprintf(game.audio_status, sizeof game.audio_status, "Sound: %d sounds, NO AUDIO OUTPUT (device failed)", sounds);
        else snprintf(game.audio_status, sizeof game.audio_status, "Sound: %d sounds, output on", sounds);
    }
    if (game.audio_status[0]) vs_plat_log("%s", game.audio_status);
    if (audio_dump_path) {
        audio_dump = fopen(audio_dump_path, "wb");
        if (audio_dump) { static const uint8_t blank[44] = {0}; fwrite(blank, 1, 44, audio_dump); }   /* WAV header written at the end */
    }
    if (vs_game_init(&game, &pack, LOGICAL_W, LOGICAL_H, seed) != 0) {
        fprintf(stderr, "game init failed\n");
#if defined(VS_XENON) || defined(VS_XDK)
        return fatal_screen("Game init failed: the pack's table layout does\nnot match this build. Rebuild vsdata.vspack\nwith the patcher from the same commit.");
#endif
        return 1;
    }

    /* dev aid: --give NAME[:LEVEL] adds weapons/passives at the start of the run */
    if (give_count && game.screen == VS_SCREEN_GAME) {
        vs_enum we;
        if (vs_pack_enum(&pack, "weapons", &we) == 0) {
            for (int i = 0; i < give_count; i++) {
                char name[48]; int level = 1;
                snprintf(name, sizeof name, "%s", give[i]);
                char *colon = strchr(name, ':');
                if (colon) { *colon = 0; level = atoi(colon + 1); }
                int type = vs_enum_find_value(&we, name);
                if (type <= 0) { fprintf(stderr, "--give: unknown item %s\n", name); continue; }
                int slot = vs_weapon_find(&game, type);
                if (slot < 0) slot = vs_weapon_add(&game, type, 0);
                while (slot >= 0 && game.weapons[slot].level < level && vs_weapon_level_up(&game, slot)) {}
            }
        }
    }
    /* dev aid: --map-item NAME puts a map bonus item 60 px below the player */
    if (map_item && game.screen == VS_SCREEN_GAME) {
        vs_enum we;
        int type = vs_pack_enum(&pack, "weapons", &we) == 0 ? vs_enum_find_value(&we, map_item) : -1;
        vs_pickup *p = type > 0 ? vs_pickup_make(&game, game.player.x, game.player.y + 60, VS_PICKUPS_WEAPON) : NULL;
        if (p) p->weapon_type = type; else fprintf(stderr, "--map-item: unknown item %s\n", map_item);
    }
    /* dev aid: --drop NAME puts a pickup (e.g. NFT, ROAST, VACUUM) 40 px below the player */
    if (drop && game.screen == VS_SCREEN_GAME) {
        vs_enum pe;
        int type = vs_pack_enum(&pack, "pickups", &pe) == 0 ? vs_enum_find_value(&pe, drop) : -1;
        if (type <= 0 || !vs_pickup_make(&game, game.player.x, game.player.y + 40, type)) fprintf(stderr, "--drop: unknown pickup %s\n", drop);
    }
    /* dev aid: --level N starts the run at player level N */
    if (start_level > 1 && game.screen == VS_SCREEN_GAME) { game.player.level = start_level; vs_levelup_calculate_xp_factor(&game); game.player.xp = game.levelup.previous_xp_factor; }
    /* dev aid: --show-levelup opens a level-up and leaves it on screen */
    if (show_levelup && game.screen == VS_SCREEN_GAME) {
        game.auto_pick = 0;
        vs_levelup_open(&game);
        for (int i = 0; i < game.levelup.option_count && !game.levelup.items_mode; i++) vs_plat_log("offered: %s", vs_weapon_name(&game, game.levelup.options[i]));
    }
    /* dev aid: --chest [LEVEL] opens one treasure chest now and lists what the player owns */
    if (chest && game.screen == VS_SCREEN_GAME) {
        vs_treasure_open(&game, chest, -1);
        vs_enum we;
        if (vs_pack_enum(&pack, "weapons", &we) == 0)
            for (int i = 0; i < VS_MAX_WEAPONS; i++)
                if (game.weapons[i].active) vs_plat_log("owned: %s lv %d", vs_enum_value(&we, (uint32_t)game.weapons[i].type), game.weapons[i].level);
    }
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
            vs_audio_advance(tick_ms);
            if (audio_dump) {   /* exactly one tick of sound: 44100 / 60 = 735 frames */
                static int16_t tick_buf[735 * 2];
                vs_audio_mix(tick_buf, 735);
                for (int k = 0; k < 735 * 2; k++) { uint8_t le[2] = { (uint8_t)(tick_buf[k] & 0xff), (uint8_t)((uint16_t)tick_buf[k] >> 8) }; fwrite(le, 1, 2, audio_dump); }
            }
        }
        /* keep ~70 ms queued at the audio device, mixed in 1024-frame chunks */
        while (audio_live && vs_plat_audio_queued() < 3072) {
            static int16_t chunk[1024 * 2];
            vs_audio_mix(chunk, 1024);
            vs_plat_audio_push(chunk, 1024);
        }
        int recording = record_dir && rendered >= record_from;
        if (!no_render || recording || (frames >= 0 && rendered + 1 >= frames)) vs_game_render(&game);
        /* dev aid: --record DIR writes frames (from --record-from on) as DIR/frame_NNNNN.bmp */
        if (recording) {
            char path[512];
            snprintf(path, sizeof path, "%s/frame_%05d.bmp", record_dir, rendered);
            vs_plat_screenshot(path);
        }
        rendered++;
        if (frames >= 0 && rendered >= frames) break;
        if (!headless && steps == 0) vs_plat_sleep_ms(1);
    }
    if (screenshot) {
        if (vs_plat_screenshot(screenshot) == 0) vs_plat_log("wrote %s", screenshot);
        else vs_plat_log("screenshot failed");
    }
    vs_plat_log("ticks %u, %02d:%02d, player at (%.1f, %.1f) hp %.0f lv %d kills %u gold %.0f, state hash %08x%s", game.tick, game.survived_seconds / 60, game.survived_seconds % 60, game.player.x, game.player.y, game.player.hp, game.player.level, game.kills, game.coins, vs_game_state_hash(&game), game.game_over ? " GAME OVER" : "");
    if (audio_dump) {   /* little-endian PCM WAV header */
        long bytes = ftell(audio_dump) - 44;
        uint8_t h[44];
        const uint32_t rate = VS_AUDIO_RATE;
        uint32_t vals[] = { (uint32_t)(bytes + 36), 16, 0, rate, rate * 4, 0, (uint32_t)bytes };
        memcpy(h, "RIFF", 4); memcpy(h + 8, "WAVEfmt ", 8); memcpy(h + 36, "data", 4);
        h[4] = (uint8_t)vals[0]; h[5] = (uint8_t)(vals[0] >> 8); h[6] = (uint8_t)(vals[0] >> 16); h[7] = (uint8_t)(vals[0] >> 24);
        h[16] = 16; h[17] = h[18] = h[19] = 0;           /* fmt chunk size */
        h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0;       /* PCM, stereo */
        for (int k = 0; k < 4; k++) { h[24 + k] = (uint8_t)(rate >> (8 * k)); h[28 + k] = (uint8_t)((rate * 4) >> (8 * k)); h[40 + k] = (uint8_t)((uint32_t)bytes >> (8 * k)); }
        h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;      /* block align, bits */
        fseek(audio_dump, 0, SEEK_SET); fwrite(h, 1, 44, audio_dump); fclose(audio_dump);
        vs_plat_log("wrote %s (%.1f s of sound)", audio_dump_path, bytes / (4.0 * rate));
    }
    if (audio_live) vs_plat_audio_close();
    vs_audio_shutdown();
    vs_game_shutdown(&game);
    vs_plat_free(data);
    vs_plat_shutdown();
    return 0;
}

#ifdef VS_XDK
/* XDK entry point: no command line */
int main(void) { return vs_main(0, NULL); }
#else
int main(int argc, char **argv) { return vs_main(argc, argv); }
#endif
