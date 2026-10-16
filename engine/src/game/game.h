/* Game state and simulation. Fixed 60 Hz step; rendering reads state only.
 * All pools are fixed-size arrays: no allocation after init. */
#ifndef VS_GAME_H
#define VS_GAME_H

#include <stdint.h>

#include "core/vs_enums.h"
#include "core/vs_pack.h"
#include "core/vs_platform.h"
#include "core/vs_rng.h"
#include "core/vs_tables.h"

#define VS_TICK_MS (1000.0f / 60.0f)

#define VS_MAX_ENEMIES 1024
#define VS_MAX_BULLETS 512
#define VS_MAX_PICKUPS 1024
#define VS_MAX_WEAPONS 16
#define VS_MAX_TIMERS 128
#define VS_MAX_STAGE_ROWS 32
#define VS_MAX_POOLS 32
#define VS_MAX_TEXTURES 32
#define VS_MAX_SPRITES 16384
#define VS_MAX_GEMS 400
#define VS_MAX_FX 1200
#define VS_BULLET_HITS 64

/* ---- timers (ms, Phaser-like: elapsed accumulates, loops keep the remainder) */
typedef struct {
    int active;
    float elapsed, delay;
    int repeat;      /* remaining repeats; -1 = loop forever */
    int kind;        /* VS_TIMER_* */
    int32_t a, b;    /* payload */
} vs_timer;
enum { VS_TIMER_NONE = 0, VS_TIMER_TICKER, VS_TIMER_SPAWN, VS_TIMER_DESTRUCTIBLE, VS_TIMER_REGEN, VS_TIMER_WEAPON_FIRE, VS_TIMER_WEAPON_REPEAT, VS_TIMER_EVENT, VS_TIMER_EVENT_END, VS_TIMER_TIMESTOP, VS_TIMER_PLAYER_BLINK, VS_TIMER_SELFDESTRUCT };

/* ---- player */
typedef struct {
    float x, y, vx, vy;
    float last_dir_x, last_dir_y;   /* lastFacedDirection */
    int facing_left;
    float anim_t;
    int walking_frames;
    char sprite_prefix[40];
    int character_enum;

    /* stats (notes/behaviour/player.md) */
    float hp, maxHp, power, area, speed, cooldown, amount, moveSpeed, growth, duration, luck, greed, armor, regen;
    int shields;
    float shieldInvulTime;
    int level;
    float xp;
    int startingWeapon;
    float magnet_bonus;     /* from the character row, applied to the magnet */

    float invul_ms;         /* IsInvul while > 0 */
    int receiving_damage;   /* blink window */
    int is_dead;
    float body_r, body_ox, body_oy;   /* circle collider relative to (x, y) */
} vs_player;

/* ---- enemies */
typedef struct {
    int active, is_dead, dying;
    uint32_t uid;
    int type;               /* enemy enum */
    float x, y, vx, vy;
    float dir_x, dir_y;     /* _currentDirection */
    float hp, maxHp, data_maxHp, power, knockback, maxKnockback, deathKB, damageKB, xp, speed, default_speed;
    float scale, alpha;
    float res_Freeze, res_Rosary, res_Debuffs;
    int hp_x_level, fixed_direction, medusa, self_destruct, pass_through_walls;
    float medusa_elapsed;
    int sd_triggered;
    float more_x, more_y;
    int cullable, teleport_on_cull, time_stopped, receiving_damage;
    float freeze_ms;        /* Clock Lancet freeze left (time_stopped while > 0) */
    float blink_ms;         /* > 0 while blinking */
    int treasure_row;       /* stage row index whose treasure is attached, or -1 */
    int from_event;         /* counted in eventSpawned */
    int32_t event_token;
    /* sprite */
    char frame_prefix[32];  /* e.g. "Bat1_" */
    int frame_end;          /* death frames 0..end */
    int idle_frames;        /* idle frames 1..n */
    float anim_t;
    int anim_frame_start;
    float body_r, body_ox, body_oy; /* collider relative to (x, y) (origin 0.5, 1) */
    float src_w, src_h;
    int wobble;             /* which of the 10 shared wobble phases */
    uint32_t hit_tint;      /* current tint while blinking */
} vs_enemy;

/* ---- weapons and bullets (notes/behaviour/weapons.md) */
typedef struct {
    int active;
    int type;               /* weapon enum */
    int level;
    int is_power_up;
    /* accumulated stats (the weapon object's fields) */
    float interval, repeatInterval, power, area, speed, amount, duration, critChance, critMul, hitBoxDelay, knockback, chance, shieldInvulTime, volume;
    float penetrating;
    int charges, hitVFX, hitsWalls, intervalDependsOnDuration, poolLimit, bounces;
    /* passive bonuses (reset and reapplied per level) */
    float b_power, b_area, b_speed, b_cooldown, b_amount, b_maxHp, b_moveSpeed, b_growth, b_magnet, b_luck, b_armor, b_greed, b_regen, b_duration;
    float stats_damage, stats_lifetime;
    /* Santa Water: 12 angle slots and shuffled radii */
    float hw_radii[12]; int hw_angle_i, hw_radius_i;
    int target_uid;         /* Fire Wand: random target picked at Fire */
    float heal_cd_ms;       /* Bloody Tear: heal on crit at most once per 1000 ms */
    float recovered_bonus;  /* Soul Eater: min(6, recovered HP / 600), taken at Fire */
    /* Clock Lancet: direction step (of 12) and the last shot's streak */
    int lancet_ticks; float lancet_angle, lancet_x, lancet_y, lancet_ms;
    float flash_ms;         /* Pentagram: time since the screen flash (< 0: none) */
    /* Peachone / Ebony Wings / Vandalier: the bird, the circling zone(s) and
     * the points the next volley aims at (notes/behaviour/weapons.md) */
    float bird_x, bird_y, bird_dx, bird_dy; int bird_flip;
    float zone_total_ms, zone_angle_ms; int zone_block;
    float zone_x[2], zone_y[2];          /* drawn target circles (lag the damage zones) */
    float tgt[24][2]; int tgt_n, tgt_i;
} vs_weapon;

typedef struct {
    int active;
    int weapon;             /* slot in vs_game.weapons */
    int type;               /* weapon enum */
    int index;              /* indexInWeapon */
    float x, y, vx, vy;
    float age_ms;
    float penetrating;
    float scale;
    int flip_x, flip_y;
    float angle;
    uint8_t alpha;
    float body_r;           /* circle if > 0 */
    float body_w, body_h;   /* else rectangle centred on (x, y) */
    uint32_t hit[VS_BULLET_HITS];
    int hit_count;
    float hit_reset_ms;     /* HitboxTimer accumulator */
    int state;              /* per-type phase (e.g. Santa Water flying/broken) */
    float ax, ay, bx, by;   /* per-type: tween start/end, initial velocity */
    float f0, f1;           /* per-type scalars (accel, orbit angle, …) */
    int cullable;
    int visible;
} vs_bullet;

/* ---- treasure and item-found screens (treasure.c, notes/behaviour/treasure.md) */
#define VS_TREASURE_COINS 256
#define VS_TREASURE_RIBBONS 8
#define VS_TREASURE_TRAIL 32
typedef struct { float x, y, vx, vy, age, life, s0, s1, rot; int kind, front; } vs_ui_particle;
typedef struct {
    int active, level, n;           /* n = prize slots / reels (1, 3, 5) */
    int prize[5];                   /* item per slot, -1 = filler (+25 gold) */
    int reel_type[5];               /* prize type of the slot (reel contents) */
    float coins_award;              /* before Greed and the stage's gold multiplier */
    float t_ms;                     /* since the screen appeared */
    float open_ms;                  /* since OPEN was pressed; < 0 before */
    float time_scale;               /* 3 after a quick skip (second wait only) */
    float open_delay;               /* the chest opens this long after OPEN (level 3: fireworks first) */
    float stop_at, ok_at, coins_end;  /* timeline, ms after OPEN */
    float c_t0, c_v0, c_end;        /* coin counter: from c_v0 at c_t0 to the award at c_end */
    float reel_start[5], stop_delay[5];
    int skippable, skipped, done_shown, closing; float close_ms;
    vs_ui_particle coin[VS_TREASURE_COINS];
    float emit_acc[4];
    int ribbon_n; float ribbon_pts[VS_TREASURE_RIBBONS][10][2];
    float ribbon_hist[VS_TREASURE_RIBBONS][VS_TREASURE_TRAIL][2]; int ribbon_hist_n[VS_TREASURE_RIBBONS];
    int reel_icons[5][12]; int reel_icon_n[5];
} vs_treasure_ui;
typedef struct {
    int active, type, sel;          /* sel: 0 take, 1 leave */
    float t_ms;
    vs_ui_particle confetti[128];
} vs_item_found_ui;

/* ---- pickups (notes/behaviour/pickups.md) */
typedef struct {
    int active;
    int type;               /* pickup enum */
    float x, y, vx, vy;
    float value;
    int go_to_player, stationary;
    float speed;
    float t, t_from, tween_ms;  /* "time" tween: t_from → 1 over 500 ms */
    int treasure_level;     /* chests */
    int treasure_row;
    int weapon_type;        /* WEAPON pickups (map bonus items): the item it gives */
    float anim_ms;          /* WEAPON pickups: float / glow / sparkle clock */
} vs_pickup;

typedef struct { float radius, percentage; } vs_magnet;

#define VS_MAX_DESTRUCTIBLES 16
typedef struct {
    int active, is_dead, type;
    float x, y, hp, maxHp, blink_ms, src_w, src_h;
    char frame_prefix[32];
} vs_destructible;

typedef struct {
    int type[32]; float w[32]; int count; float acc;
} vs_loot;

/* ---- level-up factory (notes/behaviour/levelup.md) */
typedef struct {
    int store[512]; int store_count;        /* WeaponStore multiset */
    uint8_t excluded[64];                   /* ExcludedWeapons by enum */
    int special[16]; int special_count;     /* SpecialWeapons */
    int weighted_type[64]; float weighted_w[64]; int weighted_count; float acc;
    float current_xp_factor, previous_xp_factor;
    int pending;            /* level-up screen open */
    int options[4]; int option_count; int selected;
    int items_mode;         /* store empty: offering items */
    int item_options[2];
} vs_levelup;

/* ---- stage */
typedef struct {
    int type;               /* enemy enum */
    int enabled;
    int is_boss;
} vs_pool;

typedef struct {
    vs_stages_rec rec;
    int row_index;          /* record index in the table */
} vs_stage_row;

typedef struct {
    int stage_enum;
    vs_table table, events_table, treasure_table;
    vs_stage_row rows[VS_MAX_STAGE_ROWS];
    int row_count;
    int minute;
    float frequency; int minimum, maximum, startingSpawns, pause;
    float minimumMultiplier;
    float destructibleFreq, destructibleChance, destructibleChanceMax; int maxDestructibles, destructibleType;
    int enemies[32], enemy_count;
    int bosses[8], boss_count;
    int has_treasure; vs_stages_treasure_rec treasure;
    int treasure_attached;
    int spawn_type; int has_tileset; int day_night;
    vs_pool pools[VS_MAX_POOLS]; int pool_count;
    float rect_outer_x, rect_outer_y, rect_outer_w, rect_outer_h;
    float rect_inner_x, rect_inner_y, rect_inner_w, rect_inner_h;
    int event_spawned;      /* enemies alive from events */
    int spawner_count; float spawner_y[64];
    int destructible_count; float destructible_x[64], destructible_y[64];
    float start_x, start_y;
} vs_stage;

/* ---- effects */
enum { VS_FX_NUMBER = 1, VS_FX_HIT };
typedef struct {
    int active, kind;
    float x, y, y0, rise, age, dur;
    int value;
    uint32_t tint;
    char frame[32];
} vs_fx;

enum { VS_SCREEN_TITLE = 0, VS_SCREEN_SELECT, VS_SCREEN_GAME, VS_SCREEN_GAMEOVER, VS_SCREEN_OPTIONS, VS_SCREEN_POWERUP };

/* ---- tilemap (notes/behaviour/stage-map.md) */
typedef struct {
    int active;
    vs_map map;
    int tex;
    int cur;
    float origin_x[4], origin_y[4];
} vs_tilemap;

/* ---- game */
/* Sound effects the game plays (keys in notes/behaviour/audio.md). */
enum {
    VS_SND_HIT, VS_SND_LOSS, VS_SND_GEM, VS_SND_COIN, VS_SND_ROAST, VS_SND_LITTLEHEART, VS_SND_VACUUM,
    VS_SND_ROSARY, VS_SND_OROLOGION, VS_SND_WHIP, VS_SND_MAGICMISSILE, VS_SND_SHOT, VS_SND_HOLYWATER,
    VS_SND_LIGHTNING, VS_SND_LEVELUP, VS_SND_CLICKIN, VS_SND_CLICKOUT, VS_SND_THINGFOUND,
    VS_SND_TREASURE1, VS_SND_TREASURE2, VS_SND_TREASURE3, VS_SND_BGM_INTRO, VS_SND_BGM_GAMEOVER,
    VS_SND_PENTAGRAM,
    VS_SND_COUNT
};

/* Persistent player data (game/save.c, notes/behaviour/saves.md). */
typedef struct {
    double coins, lifetime_coins, lifetime_heal, lifetime_survived;
    float sound_volume, music_volume;   /* options, 0..1 (default 0.5) */
    int flashing_vfx, damage_numbers;   /* options, 0/1 (default 1) */
    int has_options;                    /* loaded file had the option keys (not saved) */
    int selected_character, selected_stage;
    uint8_t achieved[VS_ACHIEVEMENTS_COUNT];
    uint8_t weapon_unlocked[VS_WEAPONS_COUNT], weapon_seen[VS_WEAPONS_COUNT];
    uint8_t char_unlocked[VS_CHARACTERS_COUNT];
    uint8_t char_bought[VS_CHARACTERS_COUNT];          /* bought at the character select (BoughtCharacters) */
    uint32_t powerup_level[VS_WEAPONS_COUNT];           /* PowerUp ranks bought (BoughtPowerups) */
    uint8_t stage_unlocked[VS_STAGES_COUNT], hyper_unlocked[VS_STAGES_COUNT];
    uint8_t item_seen[VS_PICKUPS_COUNT];
    uint32_t kills[VS_ENEMIES_COUNT], pickups[VS_PICKUPS_COUNT], destroyed[VS_DESTRUCTIBLES_COUNT];
} vs_save;

typedef struct {
    const vs_pack *pack;
    vs_rng rng;
    uint32_t tick;
    int update_tick;        /* Core.updateTick (mod 4) */
    int survived_seconds;
    int logical_w, logical_h;

    float base_player_px_speed, base_enemy_speed, base_projectile_speed;
    float player_px_speed, enemy_speed, projectile_speed, gold_multiplier, enemy_health_multiplier;

    vs_player player;
    vs_magnet magnet;
    vs_enemy enemies[VS_MAX_ENEMIES];
    int enemy_count;
    uint32_t next_uid;
    int cull_index;
    vs_weapon weapons[VS_MAX_WEAPONS];
    vs_bullet bullets[VS_MAX_BULLETS];
    vs_pickup pickups[VS_MAX_PICKUPS];
    vs_destructible destructibles[VS_MAX_DESTRUCTIBLES];
    vs_loot loot;
    vs_levelup levelup;
    vs_stage stage;
    vs_tilemap tilemap;
    vs_fx fx[VS_MAX_FX];
    vs_timer timers[VS_MAX_TIMERS];
    int time_stopped;
    int game_over;
    float gameover_ms;
    uint32_t kills;
    float coins;
    int chests_opened;
    int auto_pick;          /* headless: take the first level-up option */
    int debug_hitboxes;     /* dev aid: outline colliders */
    int unlock_all;         /* dev/PC option: every item, character and stage available */
    int show_gameover;      /* dev aid: headless runs still switch to the game-over screen */
    int show_chest;         /* dev aid: headless runs still play the treasure / item-found screens */
    vs_save save;
    char save_path[260];    /* empty: no saving (headless tests) */
    char save_status[600];  /* result of the last save, shown on the game-over screen */
    char save_boot_status[600]; /* save found/created/failed at start-up, shown on the title */
    char audio_status[160];     /* sounds loaded / output state, shown on the title */
    int audio_swap;             /* diagnostics: byte order swapped at the output */
    vs_table achievements_table, stages_table;
    uint32_t run_kills[VS_ENEMIES_COUNT], run_pickups[VS_PICKUPS_COUNT], run_destroyed[VS_DESTRUCTIBLES_COUNT];
    float run_heal;
    int run_recorded, new_achievement_count, new_achievements[VS_ACHIEVEMENTS_COUNT];
    float recovered_hp;     /* HP actually recovered (Soul Eater's onHPRecoveryCallback) */
    int screen;             /* VS_SCREEN_* */
    int menu_sel, menu_stage, menu_char_list[16], menu_char_count;
    int title_sel;          /* title menu: 0 start, 1 power up, 2 options */
    int shop_sel;           /* power-up screen: panel index, -1 = refund button */
    float shop_msg_ms; char shop_msg[64];   /* short notice ("not enough gold") */
    /* pause / options screen (options.c) */
    int paused;             /* run paused, options screen shown over it */
    int opt_sel, opt_btn;   /* selected row; on the button row, which button */
    float opt_hold_ms;      /* left/right held, for auto-repeat */
    int opt_dirty;          /* options changed since the screen opened */
    int quit_run;           /* the run ended from the pause screen (results, no game over) */
    int selected_character, selected_stage;
    uint64_t seed;
    uint32_t prev_buttons;

    vs_table enemies_table, characters_table, hitvfx_table, weapons_table, pickups_table, destructibles_table;
    vs_table powerups_table;

    /* render resources */
    vs_texture *textures[VS_MAX_TEXTURES];
    const char *texture_names[VS_MAX_TEXTURES];
    int texture_count;
    int tex_characters, tex_enemies, tex_bg, tex_items, tex_vfx;
    vs_atlas atl_characters, atl_enemies, atl_items, atl_vfx, atl_ui;
    int tex_ui, tex_damage, tex_title, title_w, title_h;
    vs_lang lang;           /* English UI strings (empty for packs without LNG) */
    int snd[VS_SND_COUNT];  /* sound ids (vs_audio_find), -1 when missing */
    vs_rng audio_rng;       /* random pitch for sounds; separate so audio never changes gameplay */
    vs_rng ui_rng;          /* decoration only (ribbons, coins, confetti) */
    vs_treasure_ui treasure;
    vs_item_found_ui item_found;
    vs_font damage_font;
} vs_game;

int vs_game_init(vs_game *g, const vs_pack *pack, int logical_w, int logical_h, uint64_t seed);
void vs_game_update(vs_game *g, const vs_pad *pad);
void vs_game_render(vs_game *g);
void vs_game_shutdown(vs_game *g);
uint32_t vs_game_state_hash(const vs_game *g);

/* timers */
vs_timer *vs_timer_add(vs_game *g, int kind, float delay_ms, int repeat, int32_t a, int32_t b);
void vs_timer_cancel(vs_game *g, int kind, int32_t a);
void vs_timers_step(vs_game *g);

/* player */
int vs_player_init(vs_game *g, int character_enum);
void vs_player_update(vs_game *g, const vs_pad *pad);
int vs_player_level_up(vs_game *g);   /* returns 1 when the row had a cooldown field */
void vs_player_get_damaged(vs_game *g, float dmg);
void vs_player_recover_hp(vs_game *g, float n);
int vs_player_is_invul(const vs_game *g);

/* enemies */
vs_enemy *vs_enemy_spawn(vs_game *g, int type, float x, float y, int from_event);
void vs_enemy_get_damaged(vs_game *g, vs_enemy *e, float dmg, int hit_type, float kb);
void vs_enemy_die(vs_game *g, vs_enemy *e);
void vs_enemy_disappear(vs_game *g, vs_enemy *e);
void vs_enemies_update(vs_game *g);
void vs_enemies_cull_step(vs_game *g);
int vs_enemies_permanent(const vs_game *g);
void vs_enemy_time_stop_all(vs_game *g, int stopped);
void vs_enemy_freeze(vs_game *g, vs_enemy *e, float ms, float power);
vs_enemy *vs_enemy_closest_to_player(vs_game *g);

/* stage */
int vs_stage_init(vs_game *g, int stage_enum);
void vs_stage_check_minute(vs_game *g);
void vs_stage_spawn_round(vs_game *g);
void vs_stage_spawn_boss(vs_game *g);
int vs_stage_event_trigger(vs_game *g, int event_record);
void vs_stage_event_end(vs_game *g, int32_t token);

/* weapons */
int vs_weapon_add(vs_game *g, int type, int from_levelup);
int vs_weapon_find(const vs_game *g, int type);
/* 1 if the demake has this weapon's own behaviour (others fall back to a plain shot) */
int vs_weapon_implemented(int type);
/* Peachone / Ebony Wings / Vandalier helpers (weapon.c), used for drawing */
float vs_bird_ray_ms(int type);
float vs_bird_zone_radius(void);
void vs_bird_curve(const vs_bullet *b, float t, float *x, float *y);
/* sound (game.c): the original's PlaySound with this game's sound ids */
void vs_sfx(vs_game *g, int snd, float volume, float detune, float rate, float delay_ms, int max_instances);
float vs_sfx_random(vs_game *g);
void vs_music_for_stage(vs_game *g);
/* saves and unlocks (save.c) */
int vs_save_load(vs_game *g, const char *path);
int vs_save_write(vs_game *g);
void vs_save_run_start(vs_game *g);
void vs_save_run_end(vs_game *g);
void vs_save_count_kill(vs_game *g, int enemy);
void vs_save_count_pickup(vs_game *g, int pickup);
void vs_save_count_destroyed(vs_game *g, int destructible);
void vs_save_count_weapon(vs_game *g, int type);
int vs_save_achievement_count(const vs_game *g);
void vs_save_achievement_text(const vs_game *g, int a, char *desc, size_t desc_n, char *unlock, size_t unlock_n);
int vs_weapon_is_unlocked(const vs_game *g, int type);
int vs_character_selectable(const vs_game *g, int c);
int vs_stage_selectable(const vs_game *g, int s);
const char *vs_stage_name(const vs_game *g, int s);
/* Table entry `level` (0 = level 1) of a weapon/passive; -1 if none. */
int vs_weapon_record(const vs_game *g, int type, int level, vs_weapons_rec *out);
int vs_weapon_level_up(vs_game *g, int slot);        /* returns 0 when maxed */
void vs_weapon_fire(vs_game *g, int slot);
void vs_weapon_fire_one(vs_game *g, int slot, int index);
void vs_weapons_reset_timers(vs_game *g);
void vs_weapons_update(vs_game *g);
void vs_bullets_update(vs_game *g);
void vs_bullets_cull(vs_game *g);
float vs_weapon_p_power(const vs_game *g, const vs_weapon *w);
float vs_weapon_p_area(const vs_game *g, const vs_weapon *w);
float vs_weapon_p_interval(const vs_game *g, const vs_weapon *w);
int vs_weapon_p_amount(const vs_game *g, const vs_weapon *w);
void vs_weapon_remove(vs_game *g, int type);

/* pickups */
void vs_pickup_make_gem(vs_game *g, float x, float y, float value);
vs_pickup *vs_pickup_make(vs_game *g, float x, float y, int type);
void vs_pickups_update(vs_game *g);      /* slow block (every 4th frame) */
void vs_pickups_physics(vs_game *g);     /* every frame: move, overlaps */
void vs_pickups_vacuum_all(vs_game *g);
void vs_loot_recalculate(vs_game *g);
int vs_loot_random_item(vs_game *g);
void vs_destructible_make(vs_game *g, int type, float x, float y, int has_pos);
void vs_destructible_damage(vs_game *g, vs_destructible *d, float dmg);
void vs_destructibles_update(vs_game *g);
void vs_stage_spawn_destructible(vs_game *g);
void vs_stage_position_out_of_sight(vs_game *g, float spread_deg, float margin, float *x, float *y);

/* level-up */
void vs_levelup_init(vs_game *g);
void vs_levelup_calculate_xp_factor(vs_game *g);
void vs_levelup_remove_from_store(vs_game *g, int type);
void vs_levelup_calculate_weights(vs_game *g);
void vs_levelup_add_late_weapon(vs_game *g, int type);
void vs_levelup_open(vs_game *g);
void vs_levelup_choose(vs_game *g, int option);
void vs_levelup_input(vs_game *g, const vs_pad *pad);
void vs_level_weapon_up(vs_game *g, int type, int from_levelup);
/* treasure.c */
void vs_treasure_ui_start(vs_game *g, int level, int n, const int *prizes, float coins_award);
void vs_treasure_award(vs_game *g, int n, const int *prizes, float coins_award);
void vs_treasure_ui_update(vs_game *g, const vs_pad *pad);
void vs_treasure_ui_render(vs_game *g);
void vs_item_found_start(vs_game *g, int type);
void vs_item_found_update(vs_game *g, const vs_pad *pad);
void vs_item_found_render(vs_game *g);
void vs_ui_weapon_panel(vs_game *g, int type, float px, float py, float pw, float ph);
int vs_levelup_pull_remaining_powerup(vs_game *g);
int vs_levelup_pull_existing(vs_game *g, int include_passives);
int vs_levelup_pull_evolution(vs_game *g);
int vs_levelup_pull_fixed(vs_game *g, int type);
void vs_treasure_open(vs_game *g, int level, int treasure_row);

/* effects */
void vs_fx_damage_number(vs_game *g, float x, float y, float dmg);
void vs_fx_hit(vs_game *g, float x, float y, int hit_type);
void vs_fx_update(vs_game *g);
void vs_fx_render(vs_game *g, float cam_x, float cam_y);
/* run start/restart */
int vs_game_start_run(vs_game *g, int character_enum, int stage_enum);

/* tilemap */
int vs_tilemap_init(vs_game *g, const char *map_key);
void vs_tilemap_update(vs_game *g);
void vs_tilemap_render(vs_game *g, float cam_x, float cam_y);
int vs_tilemap_solid_at(const vs_game *g, float x, float y, int mask);
void vs_tilemap_collide(const vs_game *g, float *x, float *y, float r, int mask);

/* render */
void vs_render_begin(vs_game *g, float cam_x, float cam_y);
void vs_render_sprite(vs_game *g, int tex, const vs_frame *f, float x, float y, float ox, float oy, float scale, int flip_x, int flip_y, float angle, uint32_t tint, uint8_t alpha, float depth);
void vs_render_stretched(vs_game *g, int tex, const vs_frame *f, float cx, float cy, float w, float h,
                         float angle, uint32_t tint, uint8_t alpha, float depth);
void vs_render_end(vs_game *g);

/* shop.c: PowerUp selection and character purchase */
void vs_shop_open(vs_game *g);
void vs_shop_input(vs_game *g, const vs_pad *pad);
void vs_shop_render(vs_game *g);
void vs_powerups_apply(vs_game *g);
float vs_powerup_markup(const vs_game *g);
int vs_char_bought(const vs_game *g, int c);
double vs_char_price(const vs_game *g, int c);
int vs_char_buy(vs_game *g, int c);

/* options.c: pause screen (in a run) and options screen (from the title) */
void vs_options_open(vs_game *g);
void vs_options_input(vs_game *g, const vs_pad *pad);
void vs_options_render(vs_game *g);
/* game.c UI helpers shared with options.c */
void vs_ui_nine_slice(vs_game *g, const char *frame, float x, float y, float w, float h, int cw, int ch, uint32_t tint);
void vs_ui_nine_slice_alpha(vs_game *g, const char *frame, float x, float y, float w, float h, int cw, int ch, uint32_t tint, uint8_t alpha);
void vs_ui_rect(vs_game *g, float x, float y, float w, float h, uint32_t tint, uint8_t alpha);
const char *vs_weapon_name(const vs_game *g, int type);

#endif
