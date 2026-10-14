#include <math.h>
#include <string.h>

#include "core/vs_enums.h"
#include "game/game.h"
#include "core/vs_audio.h"

/* notes/behaviour/levelup.md */

#define MAX_TRUE_WEAPONS 6
#define MAX_POWERUP_WEAPONS 6
#define LEVELUP_OPTIONS 3
#define CHANCE_FOR_EXISTING 0.3f

void vs_levelup_calculate_xp_factor(vs_game *g) {
    vs_levelup *lu = &g->levelup;
    int L = g->player.level;
    float f = 5 + 1.5f * (float)(L / 20); if (f > 8) f = 8;
    float fp = 5 + 1.5f * (float)((L - 1) / 20); if (fp > 8) fp = 8;
    lu->current_xp_factor = f * L * L;
    lu->previous_xp_factor = fp * (L - 1) * (L - 1);
}

static int owned_count(const vs_game *g, int power_up) {
    int n = 0;
    for (int i = 0; i < VS_MAX_WEAPONS; i++) if (g->weapons[i].active && g->weapons[i].is_power_up == power_up) n++;
    return n;
}

void vs_levelup_calculate_weights(vs_game *g) {
    vs_levelup *lu = &g->levelup;
    lu->acc = 0; lu->weighted_count = 0;
    for (uint32_t gi = 0; gi < g->weapons_table.group_count; gi++) {
        vs_group grp;
        vs_table_group(&g->weapons_table, gi, &grp);
        int type = grp.enum_index;
        if (lu->excluded[type]) continue;
        int special = 0;
        for (int i = 0; i < lu->special_count; i++) if (lu->special[i] == type) special = 1;
        if (special) continue;
        vs_weapons_rec r;
        vs_table_record(&g->weapons_table, grp.first, &r);
        int owned = vs_weapon_find(g, type) >= 0;
        if (r.isPowerUp) { if (!owned && owned_count(g, 1) >= MAX_POWERUP_WEAPONS) continue; }
        else { if (!owned && owned_count(g, 0) >= MAX_TRUE_WEAPONS) continue; }
        lu->acc += (float)r.rarity;
        if (r.rarity > 0 && lu->weighted_count < 64) { lu->weighted_type[lu->weighted_count] = type; lu->weighted_w[lu->weighted_count] = lu->acc; lu->weighted_count++; }
    }
}

void vs_levelup_init(vs_game *g) {
    vs_levelup *lu = &g->levelup;
    memset(lu, 0, sizeof *lu);
    vs_levelup_calculate_xp_factor(g);
    for (uint32_t gi = 0; gi < g->weapons_table.group_count; gi++) {
        vs_group grp;
        vs_table_group(&g->weapons_table, gi, &grp);
        vs_weapons_rec r;
        vs_table_record(&g->weapons_table, grp.first, &r);
        int type = grp.enum_index;
        /* Unlocked in the data, by an achievement (save), or by the
         * --unlock-all option (the original's debug switch). Weapons without
         * behaviour in the demake are never offered. */
        int unlocked = vs_weapon_is_unlocked(g, type);
        if (r.isEvolution) lu->excluded[type] = 1;
        else if (!r.isPowerUp && !vs_weapon_implemented(type)) lu->excluded[type] = 1;
        else if (unlocked) { if (r.rarity == 0 || r.isSpecialOnly) lu->excluded[type] = 1; }
        else lu->excluded[type] = 1;
        for (uint32_t k = 0; k < grp.count && lu->store_count < 512; k++) lu->store[lu->store_count++] = type;
    }
    vs_levelup_calculate_weights(g);
}

void vs_levelup_remove_from_store(vs_game *g, int type) {
    vs_levelup *lu = &g->levelup;
    for (int i = 0; i < lu->store_count; i++) {
        if (lu->store[i] != type) continue;
        memmove(&lu->store[i], &lu->store[i + 1], (size_t)(lu->store_count - i - 1) * sizeof lu->store[0]);
        lu->store_count--;
        break;
    }
    int left = 0;
    for (int i = 0; i < lu->store_count; i++) if (lu->store[i] == type) left = 1;
    if (!left) lu->excluded[type] = 1;
    vs_levelup_calculate_weights(g);
}

void vs_levelup_add_late_weapon(vs_game *g, int type) {
    vs_levelup *lu = &g->levelup;
    if (vs_weapon_find(g, type) >= 0) return;
    if (lu->special_count < 16) lu->special[lu->special_count++] = type;
    lu->excluded[type] = 0;
    vs_levelup_calculate_weights(g);
}

static int in_store(const vs_levelup *lu, int type) {
    for (int i = 0; i < lu->store_count; i++) if (lu->store[i] == type) return 1;
    return 0;
}

static int weighted_pick(vs_game *g, int weapons_only) {
    vs_levelup *lu = &g->levelup;
    if (weapons_only) {
        int types[64]; float ws[64]; int n = 0; float acc = 0;
        for (uint32_t gi = 0; gi < g->weapons_table.group_count; gi++) {
            vs_group grp; vs_table_group(&g->weapons_table, gi, &grp);
            if (lu->excluded[grp.enum_index]) continue;
            vs_weapons_rec r; vs_table_record(&g->weapons_table, grp.first, &r);
            if (r.isPowerUp) continue;
            acc += (float)r.rarity;
            if (r.rarity > 0 && n < 64) { types[n] = grp.enum_index; ws[n] = acc; n++; }
        }
        float r = vs_rng_f32(&g->rng) * acc;
        for (int i = 0; i < n; i++) if (ws[i] >= r) return types[i];
        return -1;
    }
    float r = vs_rng_f32(&g->rng) * lu->acc;
    for (int i = 0; i < lu->weighted_count; i++) if (lu->weighted_w[i] >= r) return lu->weighted_type[i];
    return -1;
}

static float chance_for_existing(const vs_game *g) {
    return (g->player.level % 2 == 0 ? 2 : 1) * CHANCE_FOR_EXISTING + 1 - 1 / g->player.luck;
}

static int random_existing_weapon(vs_game *g) {
    vs_levelup *lu = &g->levelup;
    if (vs_rng_f32(&g->rng) > chance_for_existing(g)) return -1;
    int owned[VS_MAX_WEAPONS]; int n = 0;
    for (int i = 0; i < VS_MAX_WEAPONS; i++) if (g->weapons[i].active) owned[n++] = g->weapons[i].type;
    while (n > 0) {
        int pick = vs_rng_f32(&g->rng) > 0.5f ? 0 : (int)vs_rng_below(&g->rng, (uint32_t)n);
        int type = owned[pick];
        int slot = vs_weapon_find(g, type);
        int lv = slot >= 0 ? g->weapons[slot].level : 0;
        int drop = 1;
        if (2.5f * (lv + 1) > g->player.level) drop = 1;
        else if (in_store(lu, type)) return type;
        if (drop) { memmove(&owned[pick], &owned[pick + 1], (size_t)(n - pick - 1) * sizeof owned[0]); n--; }
    }
    return -1;
}

static int option_count(vs_game *g) {
    return vs_rng_f32(&g->rng) > 1 / g->player.luck ? LEVELUP_OPTIONS + 1 : LEVELUP_OPTIONS;
}

static void pick_options(vs_game *g) {
    vs_levelup *lu = &g->levelup;
    lu->option_count = 0;
    int e = random_existing_weapon(g);
    if (e >= 0) lu->options[lu->option_count++] = e;
    e = random_existing_weapon(g);
    if (e >= 0 && !(lu->option_count && lu->options[0] == e)) lu->options[lu->option_count++] = e;
    int n = option_count(g), tries = 0;
    while (lu->option_count < n && tries < 1000) {
        tries++;
        int pick = g->player.level <= 3 ? weighted_pick(g, 1) : weighted_pick(g, 0);
        if (pick < 0) continue;
        int dup = 0;
        for (int i = 0; i < lu->option_count; i++) if (lu->options[i] == pick) dup = 1;
        if (!dup) lu->options[lu->option_count++] = pick;
    }
    /* the list is reversed and popped by the UI: first pushed is shown first */
}

void vs_levelup_open(vs_game *g) {
    vs_levelup *lu = &g->levelup;
    lu->pending = 1;
    lu->selected = 0;
    vs_sfx(g, VS_SND_LEVELUP, 1.5f, 0, 1, 0, 0);
    vs_audio_music_fade(0.2f, 500);
    lu->items_mode = lu->weighted_count == 0;
    if (lu->items_mode) { lu->item_options[0] = VS_PICKUPS_COINBAG2; lu->item_options[1] = VS_PICKUPS_ROAST; lu->option_count = 2; }
    else pick_options(g);
}

void vs_level_weapon_up(vs_game *g, int type, int from_levelup) {
    int slot = vs_weapon_find(g, type);
    if (slot >= 0) {
        if (!vs_weapon_level_up(g, slot)) g->coins += 10;
        if (from_levelup) vs_levelup_remove_from_store(g, type);
    } else {
        vs_weapon_add(g, type, from_levelup);
    }
}

void vs_levelup_choose(vs_game *g, int option) {
    vs_levelup *lu = &g->levelup;
    if (!lu->pending) return;
    if (option < 0 || option >= lu->option_count) option = 0;
    if (lu->items_mode) {
        vs_pickup *p = vs_pickup_make(g, g->player.x, g->player.y, lu->item_options[option]);
        if (p) { /* MakeAndActivatePickup: taken immediately */ p->x = g->player.x + g->player.body_ox; p->y = g->player.y + g->player.body_oy; }
    } else {
        vs_level_weapon_up(g, lu->options[option], 1);
    }
    lu->pending = 0;
    vs_sfx(g, VS_SND_CLICKIN, -1, 0, 1, 0, 0);
    vs_audio_music_fade(0.3f, 500);
}

void vs_levelup_input(vs_game *g, const vs_pad *pad) {
    vs_levelup *lu = &g->levelup;
    uint32_t pressed = pad->buttons & ~g->prev_buttons;
    if (g->auto_pick) { vs_levelup_choose(g, 0); return; }
    if (pressed & VS_BTN_UP) lu->selected = (lu->selected + lu->option_count - 1) % lu->option_count;
    if (pressed & VS_BTN_DOWN) lu->selected = (lu->selected + 1) % lu->option_count;
    if (pressed & VS_BTN_A) vs_levelup_choose(g, lu->selected);
}

/* ---- treasure pulls (notes/behaviour/treasure.md) ---- */

static int weighted_from(vs_game *g, int *types, float *ws, int n, float acc) {
    if (n == 0 || acc <= 0) return -1;
    float r = vs_rng_f32(&g->rng) * acc;
    for (int i = 0; i < n; i++) if (ws[i] >= r) return types[i];
    return -1;
}

int vs_levelup_pull_remaining_powerup(vs_game *g) {
    vs_levelup *lu = &g->levelup;
    int types[64]; float ws[64]; int n = 0; float acc = 0;
    for (uint32_t gi = 0; gi < g->weapons_table.group_count; gi++) {
        vs_group grp; vs_table_group(&g->weapons_table, gi, &grp);
        if (lu->excluded[grp.enum_index]) continue;
        vs_weapons_rec r; vs_table_record(&g->weapons_table, grp.first, &r);
        if (!r.isPowerUp) continue;
        acc += (float)r.rarity;
        if (r.rarity > 0 && n < 64) { types[n] = grp.enum_index; ws[n] = acc; n++; }
    }
    return weighted_from(g, types, ws, n, acc);
}

int vs_levelup_pull_existing(vs_game *g, int include_passives) {
    vs_levelup *lu = &g->levelup;
    int types[VS_MAX_WEAPONS]; float ws[VS_MAX_WEAPONS]; int n = 0; float acc = 0;
    for (int i = 0; i < VS_MAX_WEAPONS; i++) {
        if (!g->weapons[i].active) continue;
        int type = g->weapons[i].type;
        if (lu->excluded[type]) continue;
        vs_weapons_rec r; int gi = vs_table_find_group_enum(&g->weapons_table, type);
        vs_group grp; vs_table_group(&g->weapons_table, (uint32_t)gi, &grp); vs_table_record(&g->weapons_table, grp.first, &r);
        if (!include_passives && r.isPowerUp) continue;
        acc += (float)r.rarity;
        if (r.rarity > 0) { types[n] = type; ws[n] = acc; n++; }
    }
    return weighted_from(g, types, ws, n, acc);
}

int vs_levelup_pull_evolution(vs_game *g) {
    vs_levelup *lu = &g->levelup;
    int types[16]; float ws[16]; int n = 0; float acc = 0;
    for (int i = 0; i < lu->special_count; i++) {
        int type = lu->special[i];
        int gi = vs_table_find_group_enum(&g->weapons_table, type);
        if (gi < 0) continue;
        vs_group grp; vs_table_group(&g->weapons_table, (uint32_t)gi, &grp);
        vs_weapons_rec r; vs_table_record(&g->weapons_table, grp.first, &r);
        if (!r.isEvolution || r.rarity <= 0) continue;
        int ok = 1, req_level = VS_HAS(&r, VS_WEAPONS_F_REQUIRESLEVEL) ? r.requiresLevel : 0;
        if (VS_HAS(&r, VS_WEAPONS_F_REQUIRES)) {
            const uint8_t *items; uint32_t cnt = vs_table_list(&g->weapons_table, r.requires, &items);
            for (uint32_t k = 0; k < cnt; k++) {
                int slot = vs_weapon_find(g, vs_list_i32(items, k));
                if (slot < 0 || g->weapons[slot].level < req_level) { ok = 0; break; }
            }
        }
        if (!ok) continue;
        acc += (float)r.rarity;
        types[n] = type; ws[n] = acc; n++;
    }
    int pick = weighted_from(g, types, ws, n, acc);
    if (pick >= 0) {
        for (int i = 0; i < lu->special_count; i++) if (lu->special[i] == pick) { memmove(&lu->special[i], &lu->special[i + 1], (size_t)(lu->special_count - i - 1) * sizeof lu->special[0]); lu->special_count--; break; }
    }
    vs_levelup_calculate_weights(g);
    return pick;
}

int vs_levelup_pull_fixed(vs_game *g, int type) {
    vs_levelup *lu = &g->levelup;
    for (int i = 0; i < lu->weighted_count; i++) if (lu->weighted_type[i] == type) return type;
    int gi = vs_table_find_group_enum(&g->weapons_table, type);
    if (gi >= 0) {
        vs_group grp; vs_table_group(&g->weapons_table, (uint32_t)gi, &grp);
        vs_weapons_rec r; vs_table_record(&g->weapons_table, grp.first, &r);
        if (r.isEvolution) {
            for (int i = 0; i < lu->special_count; i++) if (lu->special[i] == type) {
                memmove(&lu->special[i], &lu->special[i + 1], (size_t)(lu->special_count - i - 1) * sizeof lu->special[0]); lu->special_count--;
                return type;
            }
        }
    }
    return -1;
}
