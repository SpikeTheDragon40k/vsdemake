/* Reads a pack and checks structure against the generated headers. Needs a
 * built pack (skips with exit 77 when the file is missing, CTest's SKIP code). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/vs_enums.h"
#include "core/vs_pack.h"
#include "core/vs_tables.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "vsdata.vspack";
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "no pack at %s, skipping\n", path); return 77; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)n);
    CHECK(fread(buf, 1, (size_t)n, f) == (size_t)n);
    fclose(f);

    vs_pack p;
    CHECK(vs_pack_init(&p, buf, (size_t)n) == 0);
    CHECK(p.manifest == 8292959317163221061ull);

    vs_table t;
    CHECK(vs_pack_table(&p, "weapons", &t) == 0);
    CHECK(vs_table_check_fields(&t, VS_WEAPONS_FIELD_NAMES) == 0);
    CHECK(t.group_count == 41);
    CHECK(sizeof(vs_weapons_rec) >= t.record_size);
    int gi = vs_table_find_group_enum(&t, VS_WEAPONS_WHIP);
    CHECK(gi >= 0);
    vs_group g;
    vs_table_group(&t, (uint32_t)gi, &g);
    CHECK(strcmp(g.key, "WHIP") == 0);
    vs_weapons_rec w;
    vs_table_record(&t, g.first, &w);
    CHECK(VS_HAS(&w, VS_WEAPONS_F_LEVEL) && w.level == 1);
    CHECK(w.bulletType == VS_WEAPONS_WHIP);
    CHECK(VS_HAS(&w, VS_WEAPONS_F_INTERVAL) && w.interval > 0);
    CHECK(strcmp(vs_table_str(&t, w.frameName), "Whip.png") == 0);

    CHECK(vs_pack_table(&p, "characters", &t) == 0);
    CHECK(vs_table_check_fields(&t, VS_CHARACTERS_FIELD_NAMES) == 0);
    CHECK(t.group_count == 40);

    CHECK(vs_pack_table(&p, "stages", &t) == 0);
    CHECK(vs_table_check_fields(&t, VS_STAGES_FIELD_NAMES) == 0);
    gi = vs_table_find_group_enum(&t, VS_STAGES_FOREST);
    CHECK(gi >= 0);
    vs_table_group(&t, (uint32_t)gi, &g);
    CHECK(g.count == 31);
    vs_stages_rec row;
    vs_table_record(&t, g.first + 2, &row);
    CHECK(row.minute == 2);
    const uint8_t *items;
    uint32_t cnt = vs_table_list(&t, row.enemies, &items);
    CHECK(cnt >= 1 && vs_list_i32(items, 0) == VS_ENEMIES_BAT1);
    vs_table ev;
    CHECK(vs_pack_table(&p, "stages.events", &ev) == 0);
    CHECK(vs_table_check_fields(&ev, VS_STAGES_EVENTS_FIELD_NAMES) == 0);
    cnt = vs_table_list(&t, row.events, &items);
    CHECK(cnt == 1);
    vs_stages_events_rec e;
    vs_table_record(&ev, vs_list_u32(items, 0), &e);
    CHECK(e.eventType == VS_EVENTS_BAT_SWARM);

    vs_atlas a;
    CHECK(vs_pack_atlas(&p, vs_pack_find(&p, "ATL", "characters"), &a) == 0);
    vs_frame fr;
    CHECK(vs_atlas_find(&a, "Antonio_01.png", &fr) == 0 && fr.w > 0);
    CHECK(vs_atlas_find(&a, "nope.png", &fr) != 0);
    vs_tex_info ti;
    CHECK(vs_pack_tex(&p, a.tex_entry, &ti) == 0 && ti.w == 128 && ti.h == 1024);

    vs_map m;
    CHECK(vs_pack_map(&p, vs_pack_find(&p, "MAP", "Forest"), &m) == 0);
    CHECK(m.w == 64 && m.h == 64 && m.layer_count == 9 && m.object_group_count == 3);
    vs_map_layer l;
    vs_map_layer_get(&m, 3, &l);
    CHECK(strcmp(l.name, "Walls") == 0 && (l.flags & 1));
    uint32_t oc;
    const uint8_t *cur;
    const char *gname = vs_map_object_group(&m, 0, &oc, &cur);
    CHECK(strcmp(gname, "Scripts") == 0 && oc == 13);
    vs_map_object o;
    vs_map_object_next(&cur, &o);
    CHECK(strcmp(o.name, "PlayerStart") == 0);

    vs_font fnt;
    CHECK(vs_pack_font(&p, vs_pack_find(&p, "FNT", "damNum-export"), &fnt) == 0);
    vs_glyph gl;
    CHECK(vs_font_glyph(&fnt, '0', &gl) == 0 && gl.w > 0);

    vs_enum en;
    CHECK(vs_pack_enum(&p, "weapons", &en) == 0);
    CHECK(en.count == VS_WEAPONS_COUNT);
    CHECK(strcmp(vs_enum_name(&en, VS_WEAPONS_MIGHT), "MIGHT") == 0);
    CHECK(strcmp(vs_enum_value(&en, VS_WEAPONS_MIGHT), "POWER") == 0);

    printf("ok: %u entries\n", p.count);
    free(buf);
    return 0;
}
