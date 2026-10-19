/* PC test backend: the game is not playable on PC (README, "Why Xbox 360
 * only"). This backend only lets the engine run headless for development:
 * SDL's software renderer draws into an off-screen surface for screenshots
 * and frame recordings; there is no window, no keyboard or gamepad (runs
 * are driven by --input scripts) and no sound output (--audio-dump writes
 * the mix to a file). */
#include "core/vs_platform.h"

#include <SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

struct vs_texture { SDL_Texture *t; int w, h; };

static SDL_Renderer *g_ren;
static SDL_Surface *g_surface;
static int g_lw, g_lh;

int vs_plat_init(const vs_plat_config *cfg) {
    g_lw = cfg->logical_w;
    g_lh = cfg->logical_h;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    g_surface = SDL_CreateRGBSurfaceWithFormat(0, g_lw, g_lh, 32, SDL_PIXELFORMAT_ARGB8888);
    g_ren = g_surface ? SDL_CreateSoftwareRenderer(g_surface) : NULL;
    if (!g_ren) { fprintf(stderr, "SDL_CreateSoftwareRenderer: %s\n", SDL_GetError()); return -1; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* nearest: pixel art */
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    return 0;
}

void vs_plat_shutdown(void) {
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_surface) SDL_FreeSurface(g_surface);
    SDL_Quit();
}

vs_texture *vs_plat_texture_create(uint32_t w, uint32_t h, const uint8_t *argb8_be) {
    SDL_Texture *t = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, (int)w, (int)h);
    if (!t) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return NULL; }
    /* The pack stores big-endian ARGB words; ARGB8888 is a native-endian packed
     * format, so assemble each word from its bytes. */
    uint32_t *px = (uint32_t *)malloc((size_t)w * h * 4);
    if (!px) { SDL_DestroyTexture(t); return NULL; }
    for (size_t i = 0; i < (size_t)w * h; i++) {
        const uint8_t *p = argb8_be + i * 4;
        px[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    }
    SDL_UpdateTexture(t, NULL, px, (int)w * 4);
    free(px);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    vs_texture *out = (vs_texture *)malloc(sizeof *out);
    out->t = t; out->w = (int)w; out->h = (int)h;
    return out;
}

void vs_plat_texture_destroy(vs_texture *t) {
    if (!t) return;
    SDL_DestroyTexture(t->t);
    free(t);
}

void vs_plat_begin_frame(uint32_t rgb) {
    SDL_SetRenderDrawColor(g_ren, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
    SDL_RenderClear(g_ren);
}

/* NaN or huge coordinates (from damaged data) crash SDL's software renderer. */
static int sane(float v) { return v > -1e6f && v < 1e6f; }

void vs_plat_draw(const vs_sprite *s) {
    if (!s->tex || !sane(s->dx) || !sane(s->dy) || !sane(s->scale_x) || !sane(s->scale_y) || !sane(s->angle_deg)) return;
    SDL_Rect src = { s->sx, s->sy, s->sw, s->sh };
    SDL_FRect dst = { s->dx, s->dy, s->sw * s->scale_x, s->sh * s->scale_y };
    SDL_SetTextureColorMod(s->tex->t, (s->tint >> 16) & 255, (s->tint >> 8) & 255, s->tint & 255);
    SDL_SetTextureAlphaMod(s->tex->t, s->alpha);
    SDL_SetTextureBlendMode(s->tex->t, s->blend == VS_BLEND_ADD ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    int flip = (s->flip & VS_FLIP_X ? SDL_FLIP_HORIZONTAL : 0) | (s->flip & VS_FLIP_Y ? SDL_FLIP_VERTICAL : 0);
    SDL_RenderCopyExF(g_ren, s->tex->t, &src, &dst, s->angle_deg, NULL, (SDL_RendererFlip)flip);
}

void vs_plat_draw_tiled(vs_texture *tex, float ox, float oy, uint32_t tint) {
    if (!tex) return;
    SDL_SetTextureColorMod(tex->t, (tint >> 16) & 255, (tint >> 8) & 255, tint & 255);
    SDL_SetTextureAlphaMod(tex->t, 255);
    SDL_SetTextureBlendMode(tex->t, SDL_BLENDMODE_BLEND);
    /* Phaser tilePosition scrolls the texture under a fixed quad: a tile
     * offset of +o shows the texture shifted by -o. */
    float sx = -(float)fmod(ox, (double)tex->w), sy = -(float)fmod(oy, (double)tex->h);
    if (sx > 0) sx -= tex->w;
    if (sy > 0) sy -= tex->h;
    for (float y = sy; y < g_lh; y += tex->h) {
        for (float x = sx; x < g_lw; x += tex->w) {
            SDL_FRect dst = { x, y, (float)tex->w, (float)tex->h };
            SDL_RenderCopyF(g_ren, tex->t, NULL, &dst);
        }
    }
}

void vs_plat_end_frame(void) {}

/* No input devices: the pad stays empty (main.c applies --input scripts). */
int vs_plat_poll(vs_pad *pad) {
    SDL_PumpEvents();
    pad->buttons = 0; pad->lx = 0; pad->ly = 0;
    return 1;
}

uint64_t vs_plat_time_ms(void) { return SDL_GetTicks64(); }
void vs_plat_sleep_ms(uint32_t ms) { SDL_Delay(ms); }

void *vs_plat_read_file(const char *path, size_t *size) {
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");
    if (!rw) return NULL;
    Sint64 n = SDL_RWsize(rw);
    if (n < 0) { SDL_RWclose(rw); return NULL; }
    void *buf = malloc((size_t)n);
    if (buf && SDL_RWread(rw, buf, 1, (size_t)n) != (size_t)n) { free(buf); buf = NULL; }
    SDL_RWclose(rw);
    if (buf && size) *size = (size_t)n;
    return buf;
}
void vs_plat_free(void *p) { free(p); }

int vs_plat_screenshot(const char *path) { return SDL_SaveBMP(g_surface, path); }

void vs_plat_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ---- audio: no output device (sound is checked with --audio-dump) ---- */
int vs_plat_audio_open(int rate) { (void)rate; return -1; }
void vs_plat_audio_close(void) {}
int vs_plat_audio_queued(void) { return 0; }
void vs_plat_audio_push(const int16_t *frames, int count) { (void)frames; (void)count; }
uint64_t vs_plat_audio_played(void) { return 0; }
int vs_plat_audio_toggle_swap(void) { return 0; }
