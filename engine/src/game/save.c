/* Save file, achievements and unlocks (notes/behaviour/saves.md).
 *
 * The save is a small text file of "key=value" lines using the pack's enum
 * names, so it survives pack rebuilds and can be read by hand, ending with a
 * CRC32 line. Writes go to <path>.tmp, the old file becomes <path>.bak, then
 * the new file takes its place. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/vs_compat.h"
#include "core/vs_enums.h"
#include "game/game.h"

#define SAVE_MAGIC "vsdemake-save 1"

static uint32_t crc32_of(const char *s, size_t n) {
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint8_t)s[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* ---- queries used by the menus and the level-up store ---- */

int vs_weapon_is_unlocked(const vs_game *g, int type) {
    if (g->unlock_all) return 1;
    if (type >= 0 && type < VS_WEAPONS_COUNT && g->save.weapon_unlocked[type]) return 1;
    vs_weapons_rec r;
    return vs_weapon_record(g, type, 0, &r) == 0 && r.isUnlocked;
}

int vs_character_selectable(const vs_game *g, int c) {
    int gi = vs_table_find_group_enum(&g->characters_table, c);
    if (gi < 0) return 0;
    vs_group grp; vs_table_group(&g->characters_table, (uint32_t)gi, &grp);
    vs_characters_rec r; vs_table_record(&g->characters_table, grp.first, &r);
    if (!VS_HAS(&r, VS_CHARACTERS_F_CHARNAME) || !r.charName || !*vs_table_str(&g->characters_table, r.charName)) return 0;
    if (!VS_HAS(&r, VS_CHARACTERS_F_STARTINGWEAPON) || !vs_weapon_implemented(r.startingWeapon)) return 0;
    if (g->unlock_all) return !(VS_HAS(&r, VS_CHARACTERS_F_PRICE) && r.price >= 1000000);  /* debug entries */
    int hidden = VS_HAS(&r, VS_CHARACTERS_F_HIDDEN) && r.hidden;
    if (c >= 0 && c < VS_CHARACTERS_COUNT && g->save.char_unlocked[c]) hidden = 0;
    /* No shop yet: a visible character is playable without buying it. */
    return !hidden;
}

const char *vs_stage_name(const vs_game *g, int s) {
    int gi = vs_table_find_group_enum(&g->stages_table, s);
    if (gi < 0) return "?";
    vs_group grp; vs_table_group(&g->stages_table, (uint32_t)gi, &grp);
    vs_stages_rec r; vs_table_record(&g->stages_table, grp.first, &r);
    return VS_HAS(&r, VS_STAGES_F_STAGENAME) ? vs_table_str(&g->stages_table, r.stageName) : "?";
}

int vs_stage_selectable(const vs_game *g, int s) {
    if (s != VS_STAGES_FOREST && s != VS_STAGES_LIBRARY) return 0;   /* the playable ones */
    if (s == VS_STAGES_FOREST || g->unlock_all) return 1;
    return g->save.stage_unlocked[s];
}

/* ---- run counters ---- */

void vs_save_count_kill(vs_game *g, int enemy) { if (enemy >= 0 && enemy < VS_ENEMIES_COUNT) g->run_kills[enemy]++; }
void vs_save_count_pickup(vs_game *g, int pickup) { if (pickup >= 0 && pickup < VS_PICKUPS_COUNT) { g->run_pickups[pickup]++; g->save.item_seen[pickup] = 1; } }
void vs_save_count_destroyed(vs_game *g, int d) { if (d >= 0 && d < VS_DESTRUCTIBLES_COUNT) g->run_destroyed[d]++; }
void vs_save_count_weapon(vs_game *g, int type) { if (type >= 0 && type < VS_WEAPONS_COUNT) g->save.weapon_seen[type] = 1; }

/* ---- achievements ---- */

static int ach_record(const vs_game *g, int a, vs_achievements_rec *r) {
    if (!g->achievements_table.record_count) return -1;
    int gi = vs_table_find_group_enum(&g->achievements_table, a);
    if (gi < 0) return -1;
    vs_group grp; vs_table_group(&g->achievements_table, (uint32_t)gi, &grp);
    vs_table_record(&g->achievements_table, grp.first, r);
    return 0;
}

static void apply_unlocks(vs_game *g, const vs_achievements_rec *r) {
    if (VS_HAS(r, VS_ACHIEVEMENTS_F_WEAPONTOUNLOCK) && r->weaponToUnlock > 0 && r->weaponToUnlock < VS_WEAPONS_COUNT) g->save.weapon_unlocked[r->weaponToUnlock] = 1;
    if (VS_HAS(r, VS_ACHIEVEMENTS_F_CHARACTERTOUNLOCK) && r->characterToUnlock > 0 && r->characterToUnlock < VS_CHARACTERS_COUNT) g->save.char_unlocked[r->characterToUnlock] = 1;
    if (VS_HAS(r, VS_ACHIEVEMENTS_F_STAGETOUNLOCK) && r->stageToUnlock >= 0 && r->stageToUnlock < VS_STAGES_COUNT) g->save.stage_unlocked[r->stageToUnlock] = 1;
    if (VS_HAS(r, VS_ACHIEVEMENTS_F_HYPERTOUNLOCK) && r->hyperToUnlock >= 0 && r->hyperToUnlock < VS_STAGES_COUNT) g->save.hyper_unlocked[r->hyperToUnlock] = 1;
}

static void unlock_achievement(vs_game *g, int a) {
    if (a < 0 || a >= VS_ACHIEVEMENTS_COUNT || g->save.achieved[a]) return;
    g->save.achieved[a] = 1;
    vs_achievements_rec r;
    if (ach_record(g, a, &r) == 0) {
        apply_unlocks(g, &r);
        if (VS_HAS(&r, VS_ACHIEVEMENTS_F_GOLDPRIZE) && r.goldPrize > 0) { g->save.coins += r.goldPrize; g->save.lifetime_coins += r.goldPrize; }
    }
    if (g->new_achievement_count < VS_ACHIEVEMENTS_COUNT) g->new_achievements[g->new_achievement_count++] = a;
}

/* Re-applies the unlocks of achieved achievements without a gold prize
 * (the original's RestoreAchievementUnlocks), so unlocks survive a damaged
 * list. */
static void restore_achievement_unlocks(vs_game *g) {
    for (int a = 0; a < VS_ACHIEVEMENTS_COUNT; a++) {
        if (!g->save.achieved[a]) continue;
        vs_achievements_rec r;
        if (ach_record(g, a, &r) == 0 && !(VS_HAS(&r, VS_ACHIEVEMENTS_F_GOLDPRIZE) && r.goldPrize > 0)) apply_unlocks(g, &r);
    }
}

static int owned_level(const vs_game *g, int type) {
    int s = vs_weapon_find(g, type);
    return s >= 0 ? g->weapons[s].level : 0;
}

static void check_achievements(vs_game *g) {
    vs_save *sv = &g->save;
    uint64_t total_kills = 0, destroyed = 0;
    for (int i = 0; i < VS_ENEMIES_COUNT; i++) total_kills += sv->kills[i];
    for (int i = 0; i < VS_DESTRUCTIBLES_COUNT; i++) destroyed += sv->destroyed[i];
    int forest = g->selected_stage == VS_STAGES_FOREST, library = g->selected_stage == VS_STAGES_LIBRARY;
    if (total_kills >= 5000) unlock_achievement(g, VS_ACHIEVEMENTS_DEFEAT5000ENEMIES);
    if (sv->kills[VS_ENEMIES_SKELETON] >= 3000) unlock_achievement(g, VS_ACHIEVEMENTS_DEFEAT3000SKELETONS);
    if (sv->kills[VS_ENEMIES_BOSS_XLFLOWER] >= 1 && forest) unlock_achievement(g, VS_ACHIEVEMENTS_DEFEATBOSSMADFOREST);
    if (sv->kills[VS_ENEMIES_BOSS_COUNT1] >= 1 && library) unlock_achievement(g, VS_ACHIEVEMENTS_DEFEATBOSSLIBRARY);
    if (destroyed >= 20) unlock_achievement(g, VS_ACHIEVEMENTS_DESTROY20CANDLES);
    if (sv->pickups[VS_PICKUPS_ROAST] >= 5) unlock_achievement(g, VS_ACHIEVEMENTS_EAT5POTROAST);
    if (sv->pickups[VS_PICKUPS_CLOVER] >= 1) unlock_achievement(g, VS_ACHIEVEMENTS_FINDACLOVER);
    if (sv->pickups[VS_PICKUPS_ROSARY] >= 1) unlock_achievement(g, VS_ACHIEVEMENTS_FINDAROSARY);
    if (sv->pickups[VS_PICKUPS_VACUUM] >= 1) unlock_achievement(g, VS_ACHIEVEMENTS_FINDAVACUUM);
    if (sv->pickups[VS_PICKUPS_OROLOGION] >= 1) unlock_achievement(g, VS_ACHIEVEMENTS_FINDANOROLOGION);
    if (sv->weapon_seen[VS_WEAPONS_GREED]) unlock_achievement(g, VS_ACHIEVEMENTS_FINDASTONEMASK);
    /* secret: chance = clover pickups / 65535 */
    if (vs_rng_f32(&g->rng) < sv->pickups[VS_PICKUPS_CLOVER] / 65535.0f) sv->char_unlocked[VS_CHARACTERS_EXDASH] = 1;
    if (sv->kills[VS_ENEMIES_BOSS_XLDEATH] >= 1) sv->char_unlocked[VS_CHARACTERS_NOSTRO] = 1;
    if (owned_level(g, VS_WEAPONS_FIREBALL) >= 4 || owned_level(g, VS_WEAPONS_HELLFIRE)) unlock_achievement(g, VS_ACHIEVEMENTS_GETFIREWANDTOLEVEL4);
    if (owned_level(g, VS_WEAPONS_HOLYBOOK) >= 4 || owned_level(g, VS_WEAPONS_VESPERS)) unlock_achievement(g, VS_ACHIEVEMENTS_GETHOLYBOOKTOLEVEL4);
    if (owned_level(g, VS_WEAPONS_HOLYWATER) >= 4) unlock_achievement(g, VS_ACHIEVEMENTS_GETHOLYWATERTOLEVEL4);
    if (owned_level(g, VS_WEAPONS_MAGIC_MISSILE) >= 7 || owned_level(g, VS_WEAPONS_HOLY_MISSILE)) unlock_achievement(g, VS_ACHIEVEMENTS_GETMAGICMISSILETOLEVEL7);
    if (owned_level(g, VS_WEAPONS_LIGHTNING) >= 4) unlock_achievement(g, VS_ACHIEVEMENTS_GETLIGHTNINGRINGTOLEVEL4);
    if (owned_level(g, VS_WEAPONS_SILF) >= 7 || owned_level(g, VS_WEAPONS_SILF3)) unlock_achievement(g, VS_ACHIEVEMENTS_GETPEACHONETOLEVEL7);
    if (owned_level(g, VS_WEAPONS_DIAMOND) >= 7) unlock_achievement(g, VS_ACHIEVEMENTS_GETRUNETRACERTOLEVEL7);
    if (owned_level(g, VS_WEAPONS_GARLIC) >= 7 || owned_level(g, VS_WEAPONS_VORTEX)) unlock_achievement(g, VS_ACHIEVEMENTS_GETGARLICTOLEVEL7);
    int owned = 0;
    for (int i = 0; i < VS_MAX_WEAPONS; i++) owned += g->weapons[i].active;
    if (owned >= 6) unlock_achievement(g, VS_ACHIEVEMENTS_HAVE6DIFFERENTWEAPONS);
    if (sv->lifetime_heal >= 1000) unlock_achievement(g, VS_ACHIEVEMENTS_RECOVER1000HP);
    if (g->coins >= 5000) unlock_achievement(g, VS_ACHIEVEMENTS_EARN5000COINSINASINGLERUN);
    if (g->player.level >= 20 && forest) unlock_achievement(g, VS_ACHIEVEMENTS_REACHLV20INMADFOREST);
    if (g->player.level >= 10) unlock_achievement(g, VS_ACHIEVEMENTS_REACHLV10);
    if (g->player.level >= 5) unlock_achievement(g, VS_ACHIEVEMENTS_REACHLV5);
    if (g->survived_seconds >= 60) unlock_achievement(g, VS_ACHIEVEMENTS_SURVIVE1MINUTE);
    if (g->survived_seconds >= 300 && g->selected_character == VS_CHARACTERS_PASQUALINA) unlock_achievement(g, VS_ACHIEVEMENTS_SURVIVE5MINUTESWITHDIAMOND);
    if (g->survived_seconds >= 300 && g->selected_character == VS_CHARACTERS_GENNARO) unlock_achievement(g, VS_ACHIEVEMENTS_SURVIVE5MINUTESWITHKNIFE);
    if (g->survived_seconds >= 600) unlock_achievement(g, VS_ACHIEVEMENTS_SURVIVE10MINUTES);
    if (g->survived_seconds >= 1200) unlock_achievement(g, VS_ACHIEVEMENTS_SURVIVE20MINUTES);
    static const int evo[][2] = {
        { VS_WEAPONS_VAMPIRICA, VS_ACHIEVEMENTS_EVOLVEWHIP }, { VS_WEAPONS_HOLY_MISSILE, VS_ACHIEVEMENTS_EVOLVEMAGICWAND },
        { VS_WEAPONS_THOUSAND, VS_ACHIEVEMENTS_EVOLVEKNIFE }, { VS_WEAPONS_SCYTHE, VS_ACHIEVEMENTS_EVOLVEAXE },
        { VS_WEAPONS_HEAVENSWORD, VS_ACHIEVEMENTS_EVOLVECROSS }, { VS_WEAPONS_VESPERS, VS_ACHIEVEMENTS_EVOLVEKINGBIBLE },
        { VS_WEAPONS_HELLFIRE, VS_ACHIEVEMENTS_EVOLVEFIREWAND }, { VS_WEAPONS_VORTEX, VS_ACHIEVEMENTS_EVOLVEGARLIC },
        { VS_WEAPONS_SILF3, VS_ACHIEVEMENTS_UNITEBIRDS },
    };
    for (size_t i = 0; i < sizeof evo / sizeof evo[0]; i++) if (owned_level(g, evo[i][0])) unlock_achievement(g, evo[i][1]);
    int hypers = 0;
    for (int i = 0; i < VS_STAGES_COUNT; i++) hypers += sv->hyper_unlocked[i];
    if (hypers >= 2) unlock_achievement(g, VS_ACHIEVEMENTS_UNLOCK2HYPERS);
}

void vs_save_run_start(vs_game *g) {
    memset(g->run_kills, 0, sizeof g->run_kills);
    memset(g->run_pickups, 0, sizeof g->run_pickups);
    memset(g->run_destroyed, 0, sizeof g->run_destroyed);
    g->run_heal = 0;
    g->new_achievement_count = 0;
    g->run_recorded = 0;
}

void vs_save_run_end(vs_game *g) {
    if (g->run_recorded) return;
    g->run_recorded = 1;
    vs_save *sv = &g->save;
    for (int i = 0; i < VS_ENEMIES_COUNT; i++) sv->kills[i] += g->run_kills[i];
    for (int i = 0; i < VS_PICKUPS_COUNT; i++) sv->pickups[i] += g->run_pickups[i];
    for (int i = 0; i < VS_DESTRUCTIBLES_COUNT; i++) sv->destroyed[i] += g->run_destroyed[i];
    sv->lifetime_heal += g->run_heal;
    sv->lifetime_survived += g->survived_seconds;
    sv->coins += g->coins;
    sv->lifetime_coins += g->coins;
    sv->selected_character = g->selected_character;
    sv->selected_stage = g->selected_stage;
    check_achievements(g);
    vs_save_write(g);
}

/* ---- file format ---- */

typedef struct { char *buf; size_t len, cap; } sbuf;

static void put(sbuf *b, const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof line) n = (int)sizeof line - 1;
    if (b->len + (size_t)n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 4096;
        while (cap < b->len + (size_t)n + 1) cap *= 2;
        char *nb = (char *)realloc(b->buf, cap);
        if (!nb) return;
        b->buf = nb; b->cap = cap;
    }
    memcpy(b->buf + b->len, line, (size_t)n);
    b->len += (size_t)n;
    b->buf[b->len] = 0;
}

static void put_flags(sbuf *b, const vs_pack *p, const char *enum_name, const char *key, const uint8_t *flags, int count) {
    vs_enum e;
    if (vs_pack_enum(p, enum_name, &e) != 0) return;
    for (int i = 0; i < count && (uint32_t)i < e.count; i++) if (flags[i]) put(b, "%s=%s\n", key, vs_enum_value(&e, (uint32_t)i));
}

static void put_counts(sbuf *b, const vs_pack *p, const char *enum_name, const char *key, const uint32_t *v, int count) {
    vs_enum e;
    if (vs_pack_enum(p, enum_name, &e) != 0) return;
    for (int i = 0; i < count && (uint32_t)i < e.count; i++) if (v[i]) put(b, "%s.%s=%lu\n", key, vs_enum_value(&e, (uint32_t)i), (unsigned long)v[i]);
}

static int g_last_errno;   /* errno of the last failed write, for the on-screen status */

static int write_whole(const char *path, const char *data, size_t n) {
    errno = 0;
    FILE *f = fopen(path, "wb");
    if (!f) { g_last_errno = errno; return -1; }
    size_t w = fwrite(data, 1, n, f);
    int rc = fclose(f);
    if (rc != 0 || w != n) { g_last_errno = errno; return -2; }
    return 0;
}

/* No rename: the console's CRT can't be relied on for it. The current save
 * is first copied to <path>.bak, then <path> is overwritten. If that write
 * is interrupted, loading falls back to the .bak copy. */
static int write_file_safely(const char *path, const char *data, size_t n) {
    char bak[300];
    snprintf(bak, sizeof bak, "%s.bak", path);
    size_t old_n;
    void *old = vs_plat_read_file(path, &old_n);
    if (old) { write_whole(bak, (const char *)old, old_n); vs_plat_free(old); }
    return write_whole(path, data, n);
}

int vs_save_write(vs_game *g) {
    if (!g->save_path[0]) return 0;
    const vs_save *sv = &g->save;
    vs_enum ce, se;
    sbuf b = { NULL, 0, 0 };
    put(&b, "%s\n", SAVE_MAGIC);
    put(&b, "coins=%.0f\nlifetime_coins=%.0f\nlifetime_heal=%.1f\nlifetime_survived=%.0f\n", sv->coins, sv->lifetime_coins, sv->lifetime_heal, sv->lifetime_survived);
    put(&b, "sound_volume=%.2f\nmusic_volume=%.2f\n", sv->sound_volume, sv->music_volume);
    put(&b, "flashing_vfx=%d\ndamage_numbers=%d\n", sv->flashing_vfx, sv->damage_numbers);
    if (vs_pack_enum(g->pack, "characters", &ce) == 0 && (uint32_t)sv->selected_character < ce.count) put(&b, "selected_character=%s\n", vs_enum_value(&ce, (uint32_t)sv->selected_character));
    if (vs_pack_enum(g->pack, "stages", &se) == 0 && (uint32_t)sv->selected_stage < se.count) put(&b, "selected_stage=%s\n", vs_enum_value(&se, (uint32_t)sv->selected_stage));
    put_flags(&b, g->pack, "achievements", "achievement", sv->achieved, VS_ACHIEVEMENTS_COUNT);
    put_flags(&b, g->pack, "weapons", "unlocked_weapon", sv->weapon_unlocked, VS_WEAPONS_COUNT);
    put_flags(&b, g->pack, "characters", "unlocked_character", sv->char_unlocked, VS_CHARACTERS_COUNT);
    put_flags(&b, g->pack, "stages", "unlocked_stage", sv->stage_unlocked, VS_STAGES_COUNT);
    put_flags(&b, g->pack, "stages", "unlocked_hyper", sv->hyper_unlocked, VS_STAGES_COUNT);
    put_flags(&b, g->pack, "weapons", "seen_weapon", sv->weapon_seen, VS_WEAPONS_COUNT);
    put_flags(&b, g->pack, "pickups", "seen_item", sv->item_seen, VS_PICKUPS_COUNT);
    put_counts(&b, g->pack, "enemies", "kills", sv->kills, VS_ENEMIES_COUNT);
    put_counts(&b, g->pack, "pickups", "pickups", sv->pickups, VS_PICKUPS_COUNT);
    put_counts(&b, g->pack, "destructibles", "destroyed", sv->destroyed, VS_DESTRUCTIBLES_COUNT);
    if (!b.buf) return -1;
    put(&b, "crc=%08lx\n", (unsigned long)crc32_of(b.buf, b.len));
    int rc = write_file_safely(g->save_path, b.buf, b.len);
    free(b.buf);
    if (rc == 0) snprintf(g->save_status, sizeof g->save_status, "Progress saved");
    else snprintf(g->save_status, sizeof g->save_status, "SAVE FAILED: %s %s (%s, errno %d)", rc == -1 ? "cannot create" : "cannot write", g->save_path, g_last_errno ? strerror(g_last_errno) : "no reason given", g_last_errno);
    vs_plat_log("save: %s", g->save_status);
    return rc;
}

static int find_value(const vs_pack *p, const char *enum_name, const char *value) {
    vs_enum e;
    if (vs_pack_enum(p, enum_name, &e) != 0) return -1;
    return vs_enum_find_value(&e, value);
}

/* Parses one save text; 0 when the magic and CRC line are valid. */
/* A fresh save: every field that is not 0 by default is set here. */
static void save_defaults(vs_save *sv) {
    memset(sv, 0, sizeof *sv);
    sv->selected_character = VS_CHARACTERS_ANTONIO;
    sv->selected_stage = VS_STAGES_FOREST;
    sv->sound_volume = sv->music_volume = 0.5f;
    sv->flashing_vfx = sv->damage_numbers = 1;
}

static int parse(vs_game *g, char *text, size_t n) {
    vs_save *sv = &g->save;
    char *crc_line = NULL;
    for (char *q = text + n; q > text; q--) {
        if (q - text >= 4 && strncmp(q - 4 + 1, "crc=", 4) == 0 && (q - 4 == text || q[-4] == '\n')) { crc_line = q - 3; break; }
    }
    if (!crc_line || strncmp(text, SAVE_MAGIC "\n", strlen(SAVE_MAGIC) + 1) != 0) return -1;
    unsigned long want = strtoul(crc_line + 4, NULL, 16);
    if (crc32_of(text, (size_t)(crc_line - text)) != (uint32_t)want) return -1;
    *crc_line = 0;
    save_defaults(sv);
    for (char *line = strtok(text + strlen(SAVE_MAGIC) + 1, "\n"); line; line = strtok(NULL, "\n")) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        int i;
        if (!strcmp(k, "coins")) sv->coins = atof(v);
        else if (!strcmp(k, "lifetime_coins")) sv->lifetime_coins = atof(v);
        else if (!strcmp(k, "lifetime_heal")) sv->lifetime_heal = atof(v);
        else if (!strcmp(k, "lifetime_survived")) sv->lifetime_survived = atof(v);
        else if (!strcmp(k, "sound_volume")) sv->sound_volume = (float)atof(v);
        else if (!strcmp(k, "music_volume")) sv->music_volume = (float)atof(v);
        else if (!strcmp(k, "flashing_vfx")) { sv->flashing_vfx = atoi(v) != 0; sv->has_options = 1; }
        else if (!strcmp(k, "damage_numbers")) sv->damage_numbers = atoi(v) != 0;
        else if (!strcmp(k, "selected_character")) { if ((i = find_value(g->pack, "characters", v)) >= 0) sv->selected_character = i; }
        else if (!strcmp(k, "selected_stage")) { if ((i = find_value(g->pack, "stages", v)) >= 0) sv->selected_stage = i; }
        else if (!strcmp(k, "achievement")) { if ((i = find_value(g->pack, "achievements", v)) >= 0 && i < VS_ACHIEVEMENTS_COUNT) sv->achieved[i] = 1; }
        else if (!strcmp(k, "unlocked_weapon")) { if ((i = find_value(g->pack, "weapons", v)) >= 0 && i < VS_WEAPONS_COUNT) sv->weapon_unlocked[i] = 1; }
        else if (!strcmp(k, "unlocked_character")) { if ((i = find_value(g->pack, "characters", v)) >= 0 && i < VS_CHARACTERS_COUNT) sv->char_unlocked[i] = 1; }
        else if (!strcmp(k, "unlocked_stage")) { if ((i = find_value(g->pack, "stages", v)) >= 0 && i < VS_STAGES_COUNT) sv->stage_unlocked[i] = 1; }
        else if (!strcmp(k, "unlocked_hyper")) { if ((i = find_value(g->pack, "stages", v)) >= 0 && i < VS_STAGES_COUNT) sv->hyper_unlocked[i] = 1; }
        else if (!strcmp(k, "seen_weapon")) { if ((i = find_value(g->pack, "weapons", v)) >= 0 && i < VS_WEAPONS_COUNT) sv->weapon_seen[i] = 1; }
        else if (!strcmp(k, "seen_item")) { if ((i = find_value(g->pack, "pickups", v)) >= 0 && i < VS_PICKUPS_COUNT) sv->item_seen[i] = 1; }
        else if (!strncmp(k, "kills.", 6)) { if ((i = find_value(g->pack, "enemies", k + 6)) >= 0 && i < VS_ENEMIES_COUNT) sv->kills[i] = (uint32_t)strtoul(v, NULL, 10); }
        else if (!strncmp(k, "pickups.", 8)) { if ((i = find_value(g->pack, "pickups", k + 8)) >= 0 && i < VS_PICKUPS_COUNT) sv->pickups[i] = (uint32_t)strtoul(v, NULL, 10); }
        else if (!strncmp(k, "destroyed.", 10)) { if ((i = find_value(g->pack, "destructibles", k + 10)) >= 0 && i < VS_DESTRUCTIBLES_COUNT) sv->destroyed[i] = (uint32_t)strtoul(v, NULL, 10); }
    }
    return 0;
}

static int load_one(vs_game *g, const char *path) {
    size_t n;
    char *data = (char *)vs_plat_read_file(path, &n);
    if (!data) return -1;
    char *text = (char *)malloc(n + 1);
    int rc = -1;
    if (text) { memcpy(text, data, n); text[n] = 0; rc = parse(g, text, n); free(text); }
    vs_plat_free(data);
    return rc;
}

/* Loads <path>, else <path>.bak; a missing or damaged save starts fresh. */
int vs_save_load(vs_game *g, const char *path) {
    save_defaults(&g->save);
    snprintf(g->save_path, sizeof g->save_path, "%s", path ? path : "");
    if (!path || !*path) return -1;
    char bak[300];
    snprintf(bak, sizeof bak, "%s.bak", path);
    int rc = load_one(g, path), from_backup = 0;
    if (rc != 0) { rc = load_one(g, bak); from_backup = rc == 0; }
    if (rc != 0) save_defaults(&g->save);
    /* Builds before the options screen wrote new saves with both volumes at
     * 0 by mistake; those saves (no option keys yet) get the defaults. */
    if (!g->save.has_options && g->save.sound_volume <= 0 && g->save.music_volume <= 0) g->save.sound_volume = g->save.music_volume = 0.5f;
    g->save.stage_unlocked[VS_STAGES_FOREST] = 1;
    restore_achievement_unlocks(g);
    /* A new save is written right away, so a folder that can't be written is
     * reported on the title screen instead of after a whole run. */
    if (rc == 0) snprintf(g->save_boot_status, sizeof g->save_boot_status, "Save: %s (%s)", path, from_backup ? "restored from backup" : "loaded");
    else if (vs_save_write(g) == 0) snprintf(g->save_boot_status, sizeof g->save_boot_status, "Save: %s (new save created)", path);
    else snprintf(g->save_boot_status, sizeof g->save_boot_status, "%s", g->save_status);
    g->save_status[0] = 0;
    vs_plat_log("%s", g->save_boot_status);
    return rc;
}

int vs_save_achievement_count(const vs_game *g) {
    int n = 0;
    for (int i = 0; i < VS_ACHIEVEMENTS_COUNT; i++) n += g->save.achieved[i];
    return n;
}

/* Description of an achievement and what it unlocked, for the results screen. */
void vs_save_achievement_text(const vs_game *g, int a, char *desc, size_t desc_n, char *unlock, size_t unlock_n) {
    vs_achievements_rec r;
    desc[0] = unlock[0] = 0;
    if (ach_record(g, a, &r) != 0) return;
    if (VS_HAS(&r, VS_ACHIEVEMENTS_F_DESCRIPTION)) snprintf(desc, desc_n, "%s", vs_table_str(&g->achievements_table, r.description));
    if (VS_HAS(&r, VS_ACHIEVEMENTS_F_WEAPONTOUNLOCK) && r.weaponToUnlock > 0) snprintf(unlock, unlock_n, "%s", vs_weapon_name(g, r.weaponToUnlock));
    else if (VS_HAS(&r, VS_ACHIEVEMENTS_F_CHARACTERTOUNLOCK) && r.characterToUnlock > 0) {
        int gi = vs_table_find_group_enum(&g->characters_table, r.characterToUnlock);
        if (gi >= 0) {
            vs_group grp; vs_table_group(&g->characters_table, (uint32_t)gi, &grp);
            vs_characters_rec c; vs_table_record(&g->characters_table, grp.first, &c);
            if (VS_HAS(&c, VS_CHARACTERS_F_CHARNAME)) snprintf(unlock, unlock_n, "%s", vs_table_str(&g->characters_table, c.charName));
        }
    } else if (VS_HAS(&r, VS_ACHIEVEMENTS_F_STAGETOUNLOCK) && r.stageToUnlock > 0) {
        snprintf(unlock, unlock_n, "%s", vs_stage_name(g, r.stageToUnlock));
    } else if (VS_HAS(&r, VS_ACHIEVEMENTS_F_HYPERTOUNLOCK)) snprintf(unlock, unlock_n, "Hyper mode");
    else if (VS_HAS(&r, VS_ACHIEVEMENTS_F_GOLDPRIZE) && r.goldPrize > 0) snprintf(unlock, unlock_n, "%d gold", r.goldPrize);
}
