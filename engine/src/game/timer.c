#include "game/game.h"

/* Timers follow the Phaser clock: elapsed accumulates per tick; when it
 * reaches delay the timer fires and, for repeats/loops, keeps the remainder
 * (notes/behaviour/loop.md). Callbacks are dispatched by kind in
 * vs_timers_step so timers stay plain data (replayable, hashable). */

vs_timer *vs_timer_add(vs_game *g, int kind, float delay_ms, int repeat, int32_t a, int32_t b) {
    for (int i = 0; i < VS_MAX_TIMERS; i++) {
        vs_timer *t = &g->timers[i];
        if (t->active) continue;
        t->active = 1;
        t->elapsed = 0;
        t->delay = delay_ms;
        t->repeat = repeat;
        t->kind = kind;
        t->a = a;
        t->b = b;
        return t;
    }
    vs_plat_log("timer pool full (kind %d)", kind);
    return NULL;
}

void vs_timer_cancel(vs_game *g, int kind, int32_t a) {
    for (int i = 0; i < VS_MAX_TIMERS; i++) {
        vs_timer *t = &g->timers[i];
        if (t->active && t->kind == kind && t->a == a) t->active = 0;
    }
}

static void fire(vs_game *g, vs_timer *t);

void vs_timers_step(vs_game *g) {
    for (int i = 0; i < VS_MAX_TIMERS; i++) {
        vs_timer *t = &g->timers[i];
        if (!t->active) continue;
        t->elapsed += VS_TICK_MS;
        if (t->elapsed < t->delay) continue;
        float remainder = t->elapsed - t->delay;
        if (t->repeat == 0) {
            t->active = 0; /* free before firing: the callback may add timers */
            fire(g, t);
        } else {
            if (t->repeat > 0) t->repeat--;
            t->elapsed = remainder;
            fire(g, t);
        }
    }
}

static void fire(vs_game *g, vs_timer *t) {
    switch (t->kind) {
    case VS_TIMER_TICKER:
        g->survived_seconds++;
        if (g->survived_seconds % 60 == 0) vs_stage_check_minute(g);
        break;
    case VS_TIMER_SPAWN:
        vs_stage_spawn_round(g);
        break;
    case VS_TIMER_DESTRUCTIBLE:
        vs_stage_spawn_destructible(g);
        break;
    case VS_TIMER_REGEN:
        if (g->player.regen > 0) vs_player_recover_hp(g, g->player.regen);
        break;
    case VS_TIMER_EVENT:
        vs_stage_event_trigger(g, t->a);
        break;
    case VS_TIMER_EVENT_END:
        vs_stage_event_end(g, t->a);
        break;
    case VS_TIMER_TIMESTOP:
        vs_enemy_time_stop_all(g, 0);
        break;
    case VS_TIMER_PLAYER_BLINK:
        g->player.receiving_damage = 0;
        break;
    case VS_TIMER_WEAPON_FIRE:
        vs_weapon_fire(g, t->a);
        break;
    case VS_TIMER_WEAPON_REPEAT:
        vs_weapon_fire_one(g, t->a, t->b);
        break;
    case VS_TIMER_SELFDESTRUCT: {
        vs_enemy *e = &g->enemies[t->a];
        if (e->active && !e->is_dead && e->self_destruct && e->sd_triggered) {
            /* MakeExplosion(x, y, moreX, moreY) → explosions arrive with weapons */
            vs_enemy_disappear(g, e);
        }
        break;
    }
    default:
        break;
    }
}
