#include "core/vs_text.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The UI font: Liberation Mono (Courier metrics, like the browser default
 * the original's menus use) pre-rasterized at 10, 15, 20 and 30 px
 * (vs_font_data.h, SIL OFL 1.1). Layout keeps the old 6 px × scale
 * advance and 7 px × scale cap height, so a scale picks the raster whose
 * advance is closest to 6 × scale and draws it at the remaining factor. */
#include "core/vs_font_data.h"

static vs_texture *g_tex;

int vs_text_init(void) {
    /* padded to a power-of-two height, like the pack's textures */
    uint32_t th = 1;
    while (th < VS_FONT_ATLAS_H) th <<= 1;
    uint8_t *px = (uint8_t *)calloc((size_t)VS_FONT_ATLAS_W * th, 4);
    if (!px) return -1;
    size_t n = (size_t)VS_FONT_ATLAS_W * VS_FONT_ATLAS_H, o = 0;
    for (size_t i = 0; i < sizeof VS_FONT_RLE && o < n; i++) {
        uint8_t b = VS_FONT_RLE[i];
        if (b & 0x80) {
            for (int k = 0; k <= (b & 0x7f) && o < n; k++, o++) memset(px + o * 4, 0, 4);
        } else {
            uint8_t *q = px + o * 4;
            q[0] = (uint8_t)(b * 17); q[1] = q[2] = q[3] = 255;   /* big-endian ARGB: white, alpha */
            o++;
        }
    }
    g_tex = vs_plat_texture_create(VS_FONT_ATLAS_W, th, px);
    free(px);
    return g_tex ? 0 : -1;
}

/* The raster for `scale` and the factor to draw it at. */
static int pick_size(float scale, float *factor) {
    int best = 0; float best_d = 1e9f;
    for (int i = 0; i < VS_FONT_SIZES; i++) {
        float base = VS_FONT_SIZE_INFO[i][1] / 6.0f, r = scale / base, d = r > 1 ? r : 1 / r;
        if (d < best_d) { best_d = d; best = i; }
    }
    *factor = scale / (VS_FONT_SIZE_INFO[best][1] / 6.0f);
    return best;
}

void vs_text_shutdown(void) {
    vs_plat_texture_destroy(g_tex);
    g_tex = NULL;
}

void vs_text_draw(float x, float y, float scale, uint32_t tint, const char *s) {
    if (!g_tex || scale <= 0) return;
    float f;
    const short *info = VS_FONT_SIZE_INFO[pick_size(scale, &f)];
    const int adv = info[1], cell_h = info[2], cap_top = info[4], row_y = info[5], per_row = info[6];
    const float step = (VS_TEXT_GLYPH_W + 1) * scale;   /* = adv × f */
    if (f > 0.99f && f < 1.01f) { x = floorf(x + 0.5f); y = floorf(y + 0.5f); f = 1; }   /* crisp at native size */
    float cx = x;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '\n') { cx = x; y += (VS_TEXT_GLYPH_H + 2) * scale; continue; }
        if (c < VS_FONT_FIRST || c > VS_FONT_LAST) c = '?';
        if (c != ' ') {
            int i = c - VS_FONT_FIRST;
            vs_sprite sp;
            memset(&sp, 0, sizeof sp);
            sp.tex = g_tex;
            sp.sx = (int16_t)((i % per_row) * adv); sp.sy = (int16_t)(row_y + (i / per_row) * cell_h);
            sp.sw = (int16_t)adv; sp.sh = (int16_t)cell_h;
            sp.dx = cx; sp.dy = y - cap_top * f;   /* cap tops line up with y */
            sp.scale_x = sp.scale_y = f; sp.alpha = 255; sp.tint = tint;
            vs_plat_draw(&sp);
        }
        cx += step;
    }
}

float vs_text_width(float scale, const char *s) {
    float w = 0, max = 0;
    for (; *s; s++) {
        if (*s == '\n') { if (w > max) max = w; w = 0; continue; }
        w += (VS_TEXT_GLYPH_W + 1) * scale;
    }
    return w > max ? w : max;
}
