/* Reader for vsdata.vspack (notes/pack-format.md). The pack is big-endian;
 * accessors decode into native values, which is a no-op on the console. */
#ifndef VS_PACK_H
#define VS_PACK_H

#include <stddef.h>
#include <stdint.h>

#include "core/vs_compat.h"

static inline uint32_t vs_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline uint16_t vs_be16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static inline int32_t vs_bei32(const uint8_t *p) { return (int32_t)vs_be32(p); }
static inline int16_t vs_bei16(const uint8_t *p) { return (int16_t)vs_be16(p); }
static inline uint64_t vs_be64(const uint8_t *p) { return ((uint64_t)vs_be32(p) << 32) | vs_be32(p + 4); }
float vs_bef32(const uint8_t *p);

typedef struct {
    const uint8_t *data;
    size_t size;
    uint32_t count;
    const uint8_t *dir;
    uint64_t manifest;
} vs_pack;

/* Validates the header, the directory and every entry (each offset a reader
 * may follow stays inside its entry). Returns 0 on success; the pack keeps a
 * pointer to `data` (not a copy). -6: a damaged entry, named by vs_pack_error(). */
int vs_pack_init(vs_pack *p, const void *data, size_t size);
const char *vs_pack_error(void);
/* Entry index for (kind, name) or -1. kind is "TEX", "ATL", "MAP", "FNT", "TAB", "ENM". */
int vs_pack_find(const vs_pack *p, const char *kind, const char *name);
const uint8_t *vs_pack_blob(const vs_pack *p, int idx, uint32_t *size);
const char *vs_pack_entry_name(const vs_pack *p, int idx);
/* 1 if entry idx is of `kind` ("SND", ...). */
int vs_pack_entry_is(const vs_pack *p, int idx, const char *kind);

/* TEX */
typedef struct {
    uint32_t w, h, format, used_w, used_h;
    const uint8_t *pixels; /* w*h big-endian ARGB8 words */
} vs_tex_info;
int vs_pack_tex(const vs_pack *p, int idx, vs_tex_info *out);

/* ATL */
typedef struct {
    uint32_t hash, name_off;
    uint16_t x, y, w, h;
    int16_t off_x, off_y;
    uint16_t src_w, src_h;
    uint16_t rotated;
} vs_frame;
typedef struct {
    int tex_entry;
    uint32_t count;
    const uint8_t *frames;
    const char *strings;
} vs_atlas;
int vs_pack_atlas(const vs_pack *p, int idx, vs_atlas *out);
void vs_atlas_get(const vs_atlas *a, uint32_t i, vs_frame *out);
/* Binary search by name; returns 0 if found. */
int vs_atlas_find(const vs_atlas *a, const char *name, vs_frame *out);
uint32_t vs_fnv1a(const char *s);

/* TAB */
typedef struct {
    uint32_t field_count, record_size, group_count, record_count;
    const uint8_t *fields, *groups, *records, *pool;
    const char *strings;
    uint32_t pool_size, strings_size;
} vs_table;
typedef struct {
    const char *key;
    int32_t enum_index;
    uint32_t first, count;
} vs_group;
int vs_pack_table(const vs_pack *p, const char *name, vs_table *out);
/* Compares the pack's field names with the generated header's list (VS_*_FIELD_NAMES). */
int vs_table_check_fields(const vs_table *t, const char *csv_names);
void vs_table_group(const vs_table *t, uint32_t i, vs_group *out);
int vs_table_find_group(const vs_table *t, const char *key);
int vs_table_find_group_enum(const vs_table *t, int32_t enum_index);
/* Decodes record i into a vs_tables.h struct (uint64 present + 4-byte fields). */
void vs_table_record(const vs_table *t, uint32_t i, void *out_struct);
const char *vs_table_str(const vs_table *t, uint32_t off);
/* List field: returns count and sets *items to the first big-endian word. */
uint32_t vs_table_list(const vs_table *t, uint32_t off, const uint8_t **items);
static inline uint32_t vs_list_u32(const uint8_t *items, uint32_t i) { return vs_be32(items + 4 * i); }
static inline int32_t vs_list_i32(const uint8_t *items, uint32_t i) { return vs_bei32(items + 4 * i); }
static inline float vs_list_f32(const uint8_t *items, uint32_t i) { return vs_bef32(items + 4 * i); }

/* ENM */
typedef struct {
    uint32_t count;
    const uint8_t *members;
    const char *strings;
} vs_enum;
int vs_pack_enum(const vs_pack *p, const char *name, vs_enum *out);
const char *vs_enum_name(const vs_enum *e, uint32_t i);
const char *vs_enum_value(const vs_enum *e, uint32_t i);
int vs_enum_find_value(const vs_enum *e, const char *value);

/* LNG: UI strings by key (English) */
typedef struct {
    uint32_t count;
    const uint8_t *pairs;   /* count × (keyOffset, valueOffset), sorted by key */
    const char *strings;
} vs_lang;
int vs_pack_lang(const vs_pack *p, const char *name, vs_lang *out);
/* The text for `key`, or `fallback` when the pack has no such string. */
const char *vs_lang_get(const vs_lang *l, const char *key, const char *fallback);

/* MAP */
typedef struct {
    uint32_t w, h, tile_w, tile_h;
    int tex_entry;
    uint32_t first_gid, columns, margin, spacing, tile_count;
    uint32_t layer_count, object_group_count;
    const uint8_t *collides; /* tile_count bytes */
    const uint8_t *layers, *object_groups;
} vs_map;
typedef struct {
    const char *name;
    int32_t depth;
    uint32_t flags; /* 1 collision, 2 hidden, 4 player-only collision */
    const uint8_t *gids; /* w*h big-endian u32 */
} vs_map_layer;
typedef struct {
    const char *name, *type;
    float x, y, w, h;
    uint32_t prop_count;
    const uint8_t *props; /* each: char[32] name, f32 value */
} vs_map_object;
int vs_pack_map(const vs_pack *p, int idx, vs_map *out);
void vs_map_layer_get(const vs_map *m, uint32_t i, vs_map_layer *out);
/* Iterates object groups: returns the group name and object count, and the first object pointer. */
const char *vs_map_object_group(const vs_map *m, uint32_t i, uint32_t *count, const uint8_t **first);
/* Reads one object at *cursor and advances it. */
void vs_map_object_next(const uint8_t **cursor, vs_map_object *out);
float vs_map_object_prop(const vs_map_object *o, const char *name, float dflt);

/* FNT */
typedef struct {
    int tex_entry;
    uint32_t line_height, glyph_count;
    const uint8_t *glyphs;
} vs_font;
typedef struct {
    uint32_t codepoint;
    uint16_t x, y, w, h;
    int16_t xoff, yoff, xadvance;
} vs_glyph;
int vs_pack_font(const vs_pack *p, int idx, vs_font *out);
int vs_font_glyph(const vs_font *f, uint32_t codepoint, vs_glyph *out);

#endif
