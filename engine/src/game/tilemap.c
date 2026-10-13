#include <math.h>
#include <string.h>

#include "game/game.h"

/* notes/behaviour/stage-map.md: the Tiled map is instantiated 4 times on a
 * 2x2 grid that follows the player; tiles with `collides` block the player,
 * enemies and wall-hitting bullets. */

#define COPY 2048.0f

int vs_tilemap_init(vs_game *g, const char *map_key) {
    vs_tilemap *tm = &g->tilemap;
    memset(tm, 0, sizeof *tm);
    int mi = vs_pack_find(g->pack, "MAP", map_key);
    if (mi < 0 || vs_pack_map(g->pack, mi, &tm->map) != 0) return -1;
    tm->tex = -1;
    const char *tex_name = vs_pack_entry_name(g->pack, tm->map.tex_entry);
    for (int i = 0; i < g->texture_count; i++) if (g->texture_names[i] && strcmp(g->texture_names[i], tex_name) == 0) tm->tex = i;
    if (tm->tex < 0) {
        vs_tex_info ti;
        if (vs_pack_tex(g->pack, tm->map.tex_entry, &ti) != 0 || g->texture_count >= VS_MAX_TEXTURES) return -1;
        vs_texture *t = vs_plat_texture_create(ti.w, ti.h, ti.pixels);
        if (!t) return -1;
        g->texture_names[g->texture_count] = tex_name;
        g->textures[g->texture_count] = t;
        tm->tex = g->texture_count++;
    }
    for (int i = 0; i < 4; i++) { tm->origin_x[i] = COPY * i; tm->origin_y[i] = 0; }
    tm->active = 1;
    tm->cur = 0;
    return 0;
}

/* Keep the copy containing the player as `cur` and place the other three
 * left/right and above/below it (tilingTileset.update, line 5025). */
void vs_tilemap_update(vs_game *g) {
    vs_tilemap *tm = &g->tilemap;
    if (!tm->active) return;
    float px = g->player.x, py = g->player.y;
    for (int i = 0; i < 4; i++) {
        if (px >= tm->origin_x[i] && px < tm->origin_x[i] + COPY && py >= tm->origin_y[i] && py < tm->origin_y[i] + COPY) { tm->cur = i; break; }
    }
    float cx = tm->origin_x[tm->cur], cy = tm->origin_y[tm->cur];
    int left = px < cx + 0.5f * COPY, up = py < cy + 0.5f * COPY;
    int k = 0;
    for (int i = 0; i < 4; i++) {
        if (i == tm->cur) continue;
        if (k == 0) { tm->origin_x[i] = left ? cx - COPY : cx + COPY; tm->origin_y[i] = cy; }
        else if (k == 1) { tm->origin_x[i] = cx; tm->origin_y[i] = up ? cy - COPY : cy + COPY; }
        else { tm->origin_x[i] = left ? cx - COPY : cx + COPY; tm->origin_y[i] = up ? cy - COPY : cy + COPY; }
        k++;
    }
}

static void tile_src(const vs_map *m, uint32_t gid, int *sx, int *sy) {
    uint32_t id = (gid & 0x1fffffffu) - m->first_gid;
    *sx = (int)(m->margin + (id % m->columns) * (m->tile_w + m->spacing));
    *sy = (int)(m->margin + (id / m->columns) * (m->tile_h + m->spacing));
}

void vs_tilemap_render(vs_game *g, float cam_x, float cam_y) {
    vs_tilemap *tm = &g->tilemap;
    if (!tm->active) return;
    const vs_map *m = &tm->map;
    float tw = (float)m->tile_w, th = (float)m->tile_h;
    for (int c = 0; c < 4; c++) {
        float ox = tm->origin_x[c], oy = tm->origin_y[c];
        int x0 = (int)floorf((cam_x - ox) / tw), y0 = (int)floorf((cam_y - oy) / th);
        int x1 = (int)ceilf((cam_x + g->logical_w - ox) / tw), y1 = (int)ceilf((cam_y + g->logical_h - oy) / th);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > (int)m->w) x1 = (int)m->w;
        if (y1 > (int)m->h) y1 = (int)m->h;
        if (x0 >= x1 || y0 >= y1) continue;
        for (uint32_t li = 0; li < m->layer_count; li++) {
            vs_map_layer l;
            vs_map_layer_get(m, li, &l);
            if (l.flags & 2) continue;
            for (int ty = y0; ty < y1; ty++) {
                for (int tx = x0; tx < x1; tx++) {
                    uint32_t gid = vs_be32(l.gids + 4 * (ty * (int)m->w + tx));
                    if (!gid) continue;
                    int sx, sy;
                    tile_src(m, gid, &sx, &sy);
                    vs_frame f = {0};
                    f.x = (uint16_t)sx; f.y = (uint16_t)sy; f.w = (uint16_t)tw; f.h = (uint16_t)th; f.src_w = (uint16_t)tw; f.src_h = (uint16_t)th;
                    vs_render_sprite(g, tm->tex, &f, ox + tx * tw, oy + ty * th, 0, 0, 1, (gid & 0x80000000u) != 0, (gid & 0x40000000u) != 0, 0, 0xffffff, 255, (float)l.depth);
                }
            }
        }
    }
}

/* Is the tile at world (x, y) solid for `mask` (1 Walls/Obstacle for everyone, 2 PlayerWall)? */
int vs_tilemap_solid_at(const vs_game *g, float x, float y, int mask) {
    const vs_tilemap *tm = &g->tilemap;
    if (!tm->active) return 0;
    const vs_map *m = &tm->map;
    for (int c = 0; c < 4; c++) {
        float lx = x - tm->origin_x[c], ly = y - tm->origin_y[c];
        if (lx < 0 || ly < 0 || lx >= COPY || ly >= COPY) continue;
        int tx = (int)(lx / m->tile_w), ty = (int)(ly / m->tile_h);
        if (tx < 0 || ty < 0 || tx >= (int)m->w || ty >= (int)m->h) return 0;
        for (uint32_t li = 0; li < m->layer_count; li++) {
            vs_map_layer l;
            vs_map_layer_get(m, li, &l);
            if (!(l.flags & 1)) continue;
            if ((l.flags & 4) && !(mask & 2)) continue;   /* PlayerWall: player only */
            uint32_t gid = vs_be32(l.gids + 4 * (ty * (int)m->w + tx));
            if (!gid) continue;
            uint32_t id = (gid & 0x1fffffffu) - m->first_gid;
            if (id < m->tile_count && m->collides[id]) return 1;
        }
        return 0;
    }
    return 0;
}

/* Arcade-style AABB vs tile separation for a circle body (AABB of the circle):
 * X axis first, then Y, against every solid tile the box touches. */
void vs_tilemap_collide(const vs_game *g, float *x, float *y, float r, int mask) {
    const vs_tilemap *tm = &g->tilemap;
    if (!tm->active) return;
    float tw = (float)tm->map.tile_w, th = (float)tm->map.tile_h;
    for (int axis = 0; axis < 2; axis++) {
        float bx0 = *x - r, by0 = *y - r, bx1 = *x + r, by1 = *y + r;
        int tx0 = (int)floorf(bx0 / tw), ty0 = (int)floorf(by0 / th), tx1 = (int)floorf((bx1 - 0.001f) / tw), ty1 = (int)floorf((by1 - 0.001f) / th);
        for (int ty = ty0; ty <= ty1; ty++) {
            for (int tx = tx0; tx <= tx1; tx++) {
                float cx = (tx + 0.5f) * tw, cy = (ty + 0.5f) * th;
                if (!vs_tilemap_solid_at(g, cx, cy, mask)) continue;
                float tl = tx * tw, tt = ty * th, tr = tl + tw, tb = tt + th;
                float ox = (bx1 < tr ? bx1 : tr) - (bx0 > tl ? bx0 : tl);
                float oy = (by1 < tb ? by1 : tb) - (by0 > tt ? by0 : tt);
                if (ox <= 0 || oy <= 0) continue;
                if (axis == 0 && ox <= oy) { *x += (*x < cx) ? -ox : ox; bx0 = *x - r; bx1 = *x + r; }
                else if (axis == 1 && oy < ox) { *y += (*y < cy) ? -oy : oy; by0 = *y - r; by1 = *y + r; }
            }
        }
    }
}
