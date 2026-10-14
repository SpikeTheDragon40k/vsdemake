#include "vs_pack.h"

#include <string.h>

#define HEAD 32
#define DIR 48

float vs_bef32(const uint8_t *p) {
    uint32_t u = vs_be32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

uint32_t vs_fnv1a(const char *s) {
    uint32_t h = 0x811c9dc5u;
    for (; *s; s++) {
        h ^= (uint8_t)*s;
        h *= 0x01000193u;
    }
    return h;
}

int vs_pack_init(vs_pack *p, const void *data, size_t size) {
    const uint8_t *d = (const uint8_t *)data;
    if (size < HEAD || memcmp(d, "VSPK", 4) != 0) return -1;
    if (vs_be32(d + 4) != 1) return -2;
    p->data = d;
    p->size = size;
    p->manifest = vs_be64(d + 8);
    p->count = vs_be32(d + 16);
    uint32_t dir_off = vs_be32(d + 20);
    if (vs_be32(d + 24) != size) return -3;
    if ((size_t)dir_off + (size_t)p->count * DIR > size) return -4;
    p->dir = d + dir_off;
    for (uint32_t i = 0; i < p->count; i++) {
        const uint8_t *e = p->dir + i * DIR;
        uint64_t off = vs_be32(e + 40), sz = vs_be32(e + 44);
        if (off + sz > size) return -5;
    }
    return 0;
}

const char *vs_pack_entry_name(const vs_pack *p, int idx) {
    return (const char *)(p->dir + (uint32_t)idx * DIR + 8);
}

int vs_pack_find(const vs_pack *p, const char *kind, const char *name) {
    for (uint32_t i = 0; i < p->count; i++) {
        const uint8_t *e = p->dir + i * DIR;
        if (strncmp((const char *)e, kind, 4) == 0 && strncmp((const char *)e + 8, name, 32) == 0) return (int)i;
    }
    return -1;
}

const uint8_t *vs_pack_blob(const vs_pack *p, int idx, uint32_t *size) {
    if (idx < 0 || (uint32_t)idx >= p->count) return NULL;
    const uint8_t *e = p->dir + (uint32_t)idx * DIR;
    if (size) *size = vs_be32(e + 44);
    return p->data + vs_be32(e + 40);
}

/* TEX */
int vs_pack_tex(const vs_pack *p, int idx, vs_tex_info *out) {
    uint32_t sz;
    const uint8_t *b = vs_pack_blob(p, idx, &sz);
    if (!b || sz < 24) return -1;
    out->w = vs_be32(b);
    out->h = vs_be32(b + 4);
    out->format = vs_be32(b + 8);
    out->used_w = vs_be32(b + 12);
    out->used_h = vs_be32(b + 16);
    out->pixels = b + 24;
    if ((uint64_t)out->w * out->h * 4 + 24 != sz) return -2;
    return 0;
}

/* ATL */
#define FRAME 28
int vs_pack_atlas(const vs_pack *p, int idx, vs_atlas *out) {
    uint32_t sz;
    const uint8_t *b = vs_pack_blob(p, idx, &sz);
    if (!b || sz < 16) return -1;
    out->tex_entry = (int)vs_be32(b);
    out->count = vs_be32(b + 4);
    out->frames = b + 16;
    out->strings = (const char *)b + vs_be32(b + 8);
    return 0;
}

void vs_atlas_get(const vs_atlas *a, uint32_t i, vs_frame *out) {
    const uint8_t *f = a->frames + i * FRAME;
    out->hash = vs_be32(f);
    out->name_off = vs_be32(f + 4);
    out->x = vs_be16(f + 8);
    out->y = vs_be16(f + 10);
    out->w = vs_be16(f + 12);
    out->h = vs_be16(f + 14);
    out->off_x = vs_bei16(f + 16);
    out->off_y = vs_bei16(f + 18);
    out->src_w = vs_be16(f + 20);
    out->src_h = vs_be16(f + 22);
    out->rotated = vs_be16(f + 24);
    /* Packs built before the patcher fix kept the stale offsets of untrimmed
     * frames. A frame that does not fit inside its source size is one of
     * those: treat it as untrimmed, as Phaser does. */
    if (out->off_x < 0 || out->off_y < 0 || out->off_x + out->w > out->src_w || out->off_y + out->h > out->src_h) {
        out->off_x = out->off_y = 0;
        out->src_w = out->w;
        out->src_h = out->h;
    }
}

int vs_atlas_find(const vs_atlas *a, const char *name, vs_frame *out) {
    uint32_t h = vs_fnv1a(name);
    uint32_t lo = 0, hi = a->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t mh = vs_be32(a->frames + mid * FRAME);
        if (mh < h) lo = mid + 1;
        else if (mh > h) hi = mid;
        else {
            vs_atlas_get(a, mid, out);
            return strcmp(a->strings + out->name_off, name) == 0 ? 0 : -1;
        }
    }
    return -1;
}

/* TAB */
int vs_pack_table(const vs_pack *p, const char *name, vs_table *out) {
    int idx = vs_pack_find(p, "TAB", name);
    uint32_t sz;
    const uint8_t *b = vs_pack_blob(p, idx, &sz);
    if (!b || sz < 48) return -1;
    out->field_count = vs_be32(b);
    out->record_size = vs_be32(b + 4);
    out->group_count = vs_be32(b + 8);
    out->record_count = vs_be32(b + 12);
    out->fields = b + vs_be32(b + 16);
    out->groups = b + vs_be32(b + 20);
    out->records = b + vs_be32(b + 24);
    out->pool = b + vs_be32(b + 28);
    out->pool_size = vs_be32(b + 32);
    out->strings = (const char *)b + vs_be32(b + 36);
    out->strings_size = vs_be32(b + 40);
    if (out->record_size != 8 + 4 * out->field_count) return -2;
    return 0;
}

int vs_table_check_fields(const vs_table *t, const char *csv) {
    const char *s = csv;
    for (uint32_t i = 0; i < t->field_count; i++) {
        const char *fname = (const char *)t->fields + i * 36;
        size_t n = strlen(fname);
        if (strncmp(s, fname, n) != 0 || (s[n] != ',' && s[n] != '\0')) return -1;
        s += n;
        if (*s == ',') s++;
    }
    return *s == '\0' ? 0 : -1;
}

void vs_table_group(const vs_table *t, uint32_t i, vs_group *out) {
    const uint8_t *g = t->groups + i * 16;
    out->key = t->strings + vs_be32(g);
    out->enum_index = vs_bei32(g + 4);
    out->first = vs_be32(g + 8);
    out->count = vs_be32(g + 12);
}

int vs_table_find_group(const vs_table *t, const char *key) {
    for (uint32_t i = 0; i < t->group_count; i++) {
        if (strcmp(t->strings + vs_be32(t->groups + i * 16), key) == 0) return (int)i;
    }
    return -1;
}

int vs_table_find_group_enum(const vs_table *t, int32_t enum_index) {
    for (uint32_t i = 0; i < t->group_count; i++) {
        if (vs_bei32(t->groups + i * 16 + 4) == enum_index) return (int)i;
    }
    return -1;
}

void vs_table_record(const vs_table *t, uint32_t i, void *out_struct) {
    const uint8_t *r = t->records + i * t->record_size;
    uint8_t *o = (uint8_t *)out_struct;
    uint64_t present = vs_be64(r);
    memcpy(o, &present, 8);
    for (uint32_t f = 0; f < t->field_count; f++) {
        /* Every field is a 4-byte word; floats and ints share the swap. */
        uint32_t w = vs_be32(r + 8 + 4 * f);
        memcpy(o + 8 + 4 * f, &w, 4);
    }
}

const char *vs_table_str(const vs_table *t, uint32_t off) { return t->strings + off; }

uint32_t vs_table_list(const vs_table *t, uint32_t off, const uint8_t **items) {
    const uint8_t *p = t->pool + off;
    *items = p + 4;
    return vs_be32(p);
}

/* ENM */
int vs_pack_enum(const vs_pack *p, const char *name, vs_enum *out) {
    uint32_t sz;
    const uint8_t *b = vs_pack_blob(p, vs_pack_find(p, "ENM", name), &sz);
    if (!b || sz < 16) return -1;
    out->count = vs_be32(b);
    out->members = b + 16;
    out->strings = (const char *)b + vs_be32(b + 4);
    return 0;
}
const char *vs_enum_name(const vs_enum *e, uint32_t i) { return e->strings + vs_be32(e->members + i * 12); }
const char *vs_enum_value(const vs_enum *e, uint32_t i) { return e->strings + vs_be32(e->members + i * 12 + 4); }
int vs_pack_lang(const vs_pack *p, const char *name, vs_lang *out) {
    uint32_t sz;
    const uint8_t *b = vs_pack_blob(p, vs_pack_find(p, "LNG", name), &sz);
    memset(out, 0, sizeof *out);
    if (!b || sz < 8) return -1;
    out->count = vs_be32(b);
    if (8 + 8ull * out->count > sz) { out->count = 0; return -1; }
    out->pairs = b + 8;
    out->strings = (const char *)b + 8 + 8 * out->count;
    return 0;
}

const char *vs_lang_get(const vs_lang *l, const char *key, const char *fallback) {
    uint32_t lo = 0, hi = l ? l->count : 0;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int c = strcmp(l->strings + vs_be32(l->pairs + 8 * mid), key);
        if (c < 0) lo = mid + 1;
        else if (c > 0) hi = mid;
        else return l->strings + vs_be32(l->pairs + 8 * mid + 4);
    }
    return fallback;
}

int vs_enum_find_value(const vs_enum *e, const char *value) {
    for (uint32_t i = 0; i < e->count; i++) if (strcmp(vs_enum_value(e, i), value) == 0) return (int)i;
    return -1;
}

/* MAP */
int vs_pack_map(const vs_pack *p, int idx, vs_map *out) {
    uint32_t sz;
    const uint8_t *b = vs_pack_blob(p, idx, &sz);
    if (!b || sz < 64) return -1;
    out->w = vs_be32(b);
    out->h = vs_be32(b + 4);
    out->tile_w = vs_be32(b + 8);
    out->tile_h = vs_be32(b + 12);
    out->tex_entry = (int)vs_be32(b + 16);
    out->first_gid = vs_be32(b + 20);
    out->columns = vs_be32(b + 24);
    out->margin = vs_be32(b + 28);
    out->spacing = vs_be32(b + 32);
    out->tile_count = vs_be32(b + 36);
    out->layer_count = vs_be32(b + 40);
    out->object_group_count = vs_be32(b + 44);
    out->collides = b + vs_be32(b + 48);
    out->layers = b + vs_be32(b + 52);
    out->object_groups = b + vs_be32(b + 56);
    return 0;
}

void vs_map_layer_get(const vs_map *m, uint32_t i, vs_map_layer *out) {
    const uint8_t *l = m->layers + i * (40 + 4 * m->w * m->h);
    out->name = (const char *)l;
    out->depth = vs_bei32(l + 32);
    out->flags = vs_be32(l + 36);
    out->gids = l + 40;
}

void vs_map_object_next(const uint8_t **cursor, vs_map_object *out) {
    const uint8_t *o = *cursor;
    out->name = (const char *)o;
    out->type = (const char *)o + 32;
    out->x = vs_bef32(o + 64);
    out->y = vs_bef32(o + 68);
    out->w = vs_bef32(o + 72);
    out->h = vs_bef32(o + 76);
    out->prop_count = vs_be32(o + 80);
    out->props = o + 84;
    *cursor = o + 84 + 36 * out->prop_count;
}

const char *vs_map_object_group(const vs_map *m, uint32_t i, uint32_t *count, const uint8_t **first) {
    const uint8_t *g = m->object_groups;
    for (uint32_t k = 0; k < i; k++) {
        uint32_t n = vs_be32(g + 32);
        const uint8_t *c = g + 36;
        vs_map_object tmp;
        for (uint32_t j = 0; j < n; j++) vs_map_object_next(&c, &tmp);
        g = c;
    }
    *count = vs_be32(g + 32);
    *first = g + 36;
    return (const char *)g;
}

float vs_map_object_prop(const vs_map_object *o, const char *name, float dflt) {
    for (uint32_t i = 0; i < o->prop_count; i++) {
        const uint8_t *p = o->props + i * 36;
        if (strncmp((const char *)p, name, 32) == 0) return vs_bef32(p + 32);
    }
    return dflt;
}

/* FNT */
int vs_pack_font(const vs_pack *p, int idx, vs_font *out) {
    uint32_t sz;
    const uint8_t *b = vs_pack_blob(p, idx, &sz);
    if (!b || sz < 16) return -1;
    out->tex_entry = (int)vs_be32(b);
    out->line_height = vs_be32(b + 4);
    out->glyph_count = vs_be32(b + 8);
    out->glyphs = b + 16;
    return 0;
}

int vs_font_glyph(const vs_font *f, uint32_t codepoint, vs_glyph *out) {
    for (uint32_t i = 0; i < f->glyph_count; i++) {
        const uint8_t *g = f->glyphs + i * 20;
        if (vs_be32(g) != codepoint) continue;
        out->codepoint = codepoint;
        out->x = vs_be16(g + 4);
        out->y = vs_be16(g + 6);
        out->w = vs_be16(g + 8);
        out->h = vs_be16(g + 10);
        out->xoff = vs_bei16(g + 12);
        out->yoff = vs_bei16(g + 14);
        out->xadvance = vs_bei16(g + 16);
        return 0;
    }
    return -1;
}
