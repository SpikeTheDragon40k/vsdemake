# Stage events

Lines refer to `work/main.deob.js`. Event manager class 4642–4908.

Each minute row may carry `events: [{eventType, delay, repeat, chance,
duration, moreX, moreY}]`. They are triggered when the row becomes current
(`spawning.md`, `UpdateAllData`).

## Trigger (line 4647)

- With `delay`: a timer fires after `delay` ms, repeating `repeat` more times
  (Phaser semantics: `repeat: 2` fires 3 times in total). Without `delay`:
  immediately.
- On fire (line 4658): skipped if the event is not ERASE_ENEMIES and
  `Enemies.length > 600` (`dontSpawnIfAbove`).
- If `chance` is set: `luck = player.luck`; skip when
  `100 × random() > chance × (1 / luck)`. As written, higher luck makes the
  event *less* likely. Reproduce literally.

All spawn events: pick (or create) the pool for the enemy type, **disable it
for regular spawns**, spawn N enemies that are `isCullable = false`,
`eventSpawned += N`. After `duration` ms: for each, `eventSpawned -= 1` and,
if still alive, make it cullable and `Disappear` (death animation, no
drops).

Ring placement used by most events: start angle `a = 2π × random()`, each
enemy at `player + 0.72 × (W cos a, H sin a)` (0.8 × 0.9 of the render
size), then `a += step`.

| Event | Enemy type | Count | Default duration | Angle step | Placement |
| --- | --- | --- | --- | --- | --- |
| JELLY_WALL | JELLYFISH | 100 | 60000 | π/50 | ring (full circle) |
| FLOWER_WALL | FLOWER | 100 | 60000 | π/50 | ring (full circle) |
| BAT_SWARM | BATSWARM | 50 | 10000 | π/500 | ring arc of ~18° (a clump) |
| GHOST_SWARM | GHOSTSWARM | 20 | 10000 | π/500 | ring arc of ~7° |
| SKULL_SWARM | SKULL2_SWARM | `moreX` (default 1) | 15000 | π/50 | ring |
| SHADE_BOMB_RED | SHADERED | `moreX` (default 1) | 60000 | — | each at its own random angle on the ring |
| MEDUSA_SWARM | `moreY` enemy type (default MEDUSA1) | `moreX` (default 1) | 15000 | — | column: `x = player.x + (left or right edge of rectOuter)`, `y = player.y + 8 × (i − N/2)` |
| MEDUSA_WALL | as above | `moreX` | 15000 | — | tileset stages only (else behaves as MEDUSA_SWARM): same x; `y = spawnerLocations[i mod (n−1)].y` |
| ERASE_ENEMIES | — | — | — | — | see below |

`SHADE_BOMB_RED` passes the colour `'red'`; a blue variant exists in code
(SHADEBLUE) but no event type selects it.

In the LIBRARY rows some MEDUSA events name `MEDUSA1_FAST`/`MEDUSA2_FAST`,
which don't exist in the enemy enum; the value is `undefined` at runtime, so
the default `MEDUSA1` type is used. The extractor emits `null` for those;
treat `null` as "default".

## ERASE_ENEMIES (line 4889)

Full-screen white flash (if flashing VFX enabled), then every enemy without
`res_Rosary` → `Disappear` (no drops). Background tint fades from white to
dark red (0x880000) in 20 steps of 100 ms and day/night hue cycling stops.
