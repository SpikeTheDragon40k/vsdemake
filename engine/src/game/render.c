#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/game.h"

/* Depth-sorted sprite queue. Sprites are collected during a frame and drawn
 * back-to-front in vs_render_end (depth = y - player.y in the original). */

typedef struct {
    float depth;
    uint32_t order;
    vs_sprite s;
} queued;

static queued g_queue[VS_MAX_SPRITES];
static int g_count;
static float g_cam_x, g_cam_y;

void vs_render_begin(vs_game *g, float cam_x, float cam_y) {
    (void)g;
    g_count = 0;
    g_cam_x = cam_x;
    g_cam_y = cam_y;
}

int vs_atlas_frame(const vs_atlas *a, const char *fmt, int n, vs_frame *out) {
    char name[64];
    snprintf(name, sizeof name, fmt, n);
    return vs_atlas_find(a, name, out);
}

/* (x, y) is the sprite position in world space; (ox, oy) the origin in
 * 0..1 of the untrimmed source size; the trimmed frame is offset inside it. */
void vs_render_sprite(vs_game *g, int tex, const vs_frame *f, float x, float y, float ox, float oy, float scale,
                      int flip_x, int flip_y, float angle, uint32_t tint, uint8_t alpha, float depth) {
    if (g_count >= VS_MAX_SPRITES || tex < 0) return;
    queued *q = &g_queue[g_count];
    vs_sprite *s = &q->s;
    s->tex = g->textures[tex];
    s->sx = (int16_t)f->x; s->sy = (int16_t)f->y; s->sw = (int16_t)f->w; s->sh = (int16_t)f->h;
    float off_x = flip_x ? (float)(f->src_w - f->off_x - f->w) : (float)f->off_x;
    float off_y = flip_y ? (float)(f->src_h - f->off_y - f->h) : (float)f->off_y;
    s->dx = x - g_cam_x + (off_x - ox * f->src_w) * scale;
    s->dy = y - g_cam_y + (off_y - oy * f->src_h) * scale;
    s->scale_x = s->scale_y = scale;
    s->angle_deg = angle;
    s->flip = (flip_x ? VS_FLIP_X : 0) | (flip_y ? VS_FLIP_Y : 0);
    s->tint = tint;
    s->alpha = alpha;
    q->depth = depth;
    q->order = (uint32_t)g_count;
    g_count++;
}

/* A frame stretched to w × h pixels, centred on (cx, cy), rotated about its
 * centre (offsets of trimmed frames are ignored). */
void vs_render_stretched(vs_game *g, int tex, const vs_frame *f, float cx, float cy, float w, float h,
                         float angle, uint32_t tint, uint8_t alpha, float depth) {
    if (g_count >= VS_MAX_SPRITES || tex < 0 || f->w <= 0 || f->h <= 0) return;
    queued *q = &g_queue[g_count];
    vs_sprite *s = &q->s;
    memset(s, 0, sizeof *s);
    s->tex = g->textures[tex];
    s->sx = (int16_t)f->x; s->sy = (int16_t)f->y; s->sw = (int16_t)f->w; s->sh = (int16_t)f->h;
    s->dx = cx - g_cam_x - 0.5f * w;
    s->dy = cy - g_cam_y - 0.5f * h;
    s->scale_x = w / (float)f->w; s->scale_y = h / (float)f->h;
    s->angle_deg = angle;
    s->tint = tint;
    s->alpha = alpha;
    q->depth = depth;
    q->order = (uint32_t)g_count;
    g_count++;
}

static int cmp(const void *a, const void *b) {
    const queued *x = (const queued *)a, *y = (const queued *)b;
    if (x->depth < y->depth) return -1;
    if (x->depth > y->depth) return 1;
    return x->order < y->order ? -1 : x->order > y->order;
}

void vs_render_end(vs_game *g) {
    (void)g;
    qsort(g_queue, (size_t)g_count, sizeof g_queue[0], cmp);
    for (int i = 0; i < g_count; i++) vs_plat_draw(&g_queue[i].s);
}
