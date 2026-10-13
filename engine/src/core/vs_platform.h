/* Platform layer. Everything the game needs from the machine; implemented
 * once per backend (platform/sdl2, platform/xenon). Keep it small. */
#ifndef VS_PLATFORM_H
#define VS_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

typedef struct vs_texture vs_texture;

typedef struct {
    int logical_w, logical_h; /* game pixels; the backend scales to the display */
    int window_scale;         /* PC only: initial window = logical × scale */
    int headless;             /* PC only: no window, software rendering */
    const char *title;
} vs_plat_config;

int vs_plat_init(const vs_plat_config *cfg);
void vs_plat_shutdown(void);

/* Textures take big-endian ARGB8 pixels straight from the pack. */
vs_texture *vs_plat_texture_create(uint32_t w, uint32_t h, const uint8_t *argb8_be);
void vs_plat_texture_destroy(vs_texture *t);

enum { VS_FLIP_X = 1, VS_FLIP_Y = 2 };
typedef struct {
    vs_texture *tex;
    int16_t sx, sy, sw, sh;  /* source rect in texels */
    float dx, dy;            /* destination top-left in logical pixels */
    float scale_x, scale_y;  /* 1 = unscaled */
    float angle_deg;         /* rotation about the destination centre */
    uint8_t flip;
    uint8_t alpha;           /* 255 = opaque */
    uint32_t tint;           /* 0xRRGGBB multiply, 0xffffff = none */
} vs_sprite;

void vs_plat_begin_frame(uint32_t clear_rgb);
void vs_plat_draw(const vs_sprite *s);
/* Fills the whole logical screen with `tex` repeated, scrolled by (ox, oy). */
void vs_plat_draw_tiled(vs_texture *tex, float ox, float oy, uint32_t tint);
void vs_plat_end_frame(void);

enum {
    VS_BTN_UP = 1u << 0, VS_BTN_DOWN = 1u << 1, VS_BTN_LEFT = 1u << 2, VS_BTN_RIGHT = 1u << 3,
    VS_BTN_A = 1u << 4, VS_BTN_B = 1u << 5, VS_BTN_X = 1u << 6, VS_BTN_Y = 1u << 7,
    VS_BTN_START = 1u << 8, VS_BTN_BACK = 1u << 9,
};
typedef struct {
    float lx, ly;      /* left stick, -1..1, y down */
    uint32_t buttons;  /* VS_BTN_* mask; keyboard maps onto the same pad */
} vs_pad;
/* Pumps events. Returns 0 when the user asked to quit. */
int vs_plat_poll(vs_pad *pad);

uint64_t vs_plat_time_ms(void);
void vs_plat_sleep_ms(uint32_t ms);

/* Reads a whole file; caller frees with vs_plat_free. */
void *vs_plat_read_file(const char *path, size_t *size);
void vs_plat_free(void *p);
/* Dev aid: writes the last presented frame as BMP. -1 if unsupported. */
int vs_plat_screenshot(const char *path);
void vs_plat_log(const char *fmt, ...);

#endif
