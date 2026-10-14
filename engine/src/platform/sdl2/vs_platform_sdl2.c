/* SDL2 backend: development on PC, headless rendering for tests. */
#include "core/vs_platform.h"

#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

struct vs_texture { SDL_Texture *t; int w, h; };

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Surface *g_surface; /* headless target */
static SDL_GameController *g_pad;
static int g_lw, g_lh;

int vs_plat_init(const vs_plat_config *cfg) {
    g_lw = cfg->logical_w;
    g_lh = cfg->logical_h;
    if (cfg->headless) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    if (cfg->headless) {
        g_surface = SDL_CreateRGBSurfaceWithFormat(0, g_lw, g_lh, 32, SDL_PIXELFORMAT_ARGB8888);
        g_ren = g_surface ? SDL_CreateSoftwareRenderer(g_surface) : NULL;
    } else {
        int scale = cfg->window_scale > 0 ? cfg->window_scale : 1;
        g_win = SDL_CreateWindow(cfg->title ? cfg->title : "vsdemake", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 g_lw * scale, g_lh * scale, SDL_WINDOW_RESIZABLE);
        if (!g_win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return -1; }
        g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, 0);
        if (g_ren) SDL_RenderSetLogicalSize(g_ren, g_lw, g_lh);
    }
    if (!g_ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return -1; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* nearest: pixel art */
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) { g_pad = SDL_GameControllerOpen(i); if (g_pad) break; }
    }
    return 0;
}

void vs_plat_shutdown(void) {
    if (g_pad) SDL_GameControllerClose(g_pad);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_surface) SDL_FreeSurface(g_surface);
    if (g_win) SDL_DestroyWindow(g_win);
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

void vs_plat_draw(const vs_sprite *s) {
    if (!s->tex) return;
    SDL_Rect src = { s->sx, s->sy, s->sw, s->sh };
    SDL_FRect dst = { s->dx, s->dy, s->sw * s->scale_x, s->sh * s->scale_y };
    SDL_SetTextureColorMod(s->tex->t, (s->tint >> 16) & 255, (s->tint >> 8) & 255, s->tint & 255);
    SDL_SetTextureAlphaMod(s->tex->t, s->alpha);
    int flip = (s->flip & VS_FLIP_X ? SDL_FLIP_HORIZONTAL : 0) | (s->flip & VS_FLIP_Y ? SDL_FLIP_VERTICAL : 0);
    SDL_RenderCopyExF(g_ren, s->tex->t, &src, &dst, s->angle_deg, NULL, (SDL_RendererFlip)flip);
}

void vs_plat_draw_tiled(vs_texture *tex, float ox, float oy, uint32_t tint) {
    if (!tex) return;
    SDL_SetTextureColorMod(tex->t, (tint >> 16) & 255, (tint >> 8) & 255, tint & 255);
    SDL_SetTextureAlphaMod(tex->t, 255);
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

void vs_plat_end_frame(void) {
    if (g_win) SDL_RenderPresent(g_ren);
}

int vs_plat_poll(vs_pad *pad) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return 0;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && (e.key.keysym.mod & KMOD_SHIFT)) return 0;
        if (e.type == SDL_CONTROLLERDEVICEADDED && !g_pad) g_pad = SDL_GameControllerOpen(e.cdevice.which);
        if (e.type == SDL_CONTROLLERDEVICEREMOVED && g_pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad)) == e.cdevice.which) {
            SDL_GameControllerClose(g_pad); g_pad = NULL;
        }
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint32_t b = 0;
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) b |= VS_BTN_UP;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) b |= VS_BTN_DOWN;
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) b |= VS_BTN_LEFT;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) b |= VS_BTN_RIGHT;
    if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE]) b |= VS_BTN_A;
    if (k[SDL_SCANCODE_ESCAPE]) b |= VS_BTN_START;
    if (k[SDL_SCANCODE_BACKSPACE]) b |= VS_BTN_B;
    float lx = 0, ly = 0;
    if (g_pad) {
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) b |= VS_BTN_UP;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) b |= VS_BTN_DOWN;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) b |= VS_BTN_LEFT;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= VS_BTN_RIGHT;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_A)) b |= VS_BTN_A;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_B)) b |= VS_BTN_B;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_X)) b |= VS_BTN_X;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_Y)) b |= VS_BTN_Y;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_START)) b |= VS_BTN_START;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_BACK)) b |= VS_BTN_BACK;
        lx = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
        ly = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
    }
    pad->buttons = b;
    pad->lx = lx;
    pad->ly = ly;
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

int vs_plat_screenshot(const char *path) {
    SDL_Surface *s = g_surface;
    int owned = 0;
    if (!s) {
        int w, h;
        SDL_GetRendererOutputSize(g_ren, &w, &h);
        s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!s || SDL_RenderReadPixels(g_ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) != 0) { if (s) SDL_FreeSurface(s); return -1; }
        owned = 1;
    }
    int rc = SDL_SaveBMP(s, path);
    if (owned) SDL_FreeSurface(s);
    return rc;
}

void vs_plat_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ---- audio: SDL queue (no callback thread) ---- */
static SDL_AudioDeviceID g_audio;

int vs_plat_audio_open(int rate) {
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { vs_plat_log("audio: %s", SDL_GetError()); return -1; }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = rate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_audio) { vs_plat_log("audio: %s", SDL_GetError()); return -1; }
    SDL_PauseAudioDevice(g_audio, 0);
    return 0;
}
void vs_plat_audio_close(void) { if (g_audio) SDL_CloseAudioDevice(g_audio); g_audio = 0; }
int vs_plat_audio_queued(void) { return g_audio ? (int)(SDL_GetQueuedAudioSize(g_audio) / 4) : 0; }
void vs_plat_audio_push(const int16_t *frames, int count) { if (g_audio) SDL_QueueAudio(g_audio, frames, (Uint32)count * 4); }
