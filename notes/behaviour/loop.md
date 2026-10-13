# Main loop

Line references are into `work/main.deob.js` (build 0.2.10). The original
runs on Phaser 3.55 with Arcade physics; the engine reproduces the *order* and
*rates* below, not the framework.

## Time bases

Three different clocks drive the game. Keep them distinct in the engine.

| Clock | What uses it |
| --- | --- |
| **Frame** (one `update` call per rendered frame, target 60 Hz) | input, movement, enemy steering, culling round-robin, the every-4th-frame tasks |
| **Physics step** (Arcade, velocity in px/s integrated per frame delta) | positions, overlaps (player–enemy damage, bullet–enemy, magnet–pickup) |
| **Timer events** (millisecond delays on the scene clock) | 1 s ticker → survived seconds; weapon firing intervals; spawn frequency; destructible frequency; stage event delays/durations; blink/invulnerability durations; regen tick (1000 ms) |

For the demake: fixed 60 Hz step, `delta = 1000/60` ms, and a timer list
keyed in ms. Phaser timers fire when `elapsed >= delay` on the frame that
crosses it, so with a fixed step every timer fires on a frame boundary.

## Render size (line 11446, 11509)

Game scenes run at **456 px high** (landscape), width = 456 × window aspect
(≈ 811 px at 16:9), then scaled to the window (Phaser FIT). Portrait builds
use 342 wide. Menus use a separate 1366-wide resolution. `PixelScale = 1`,
so one art pixel is one logical pixel; "W, H" in these notes are this
logical size.

## Per-frame order (scene `update`, line 6956)

1. `maxSpeed = PlayerPxSpeed × player.moveSpeed` (PlayerPxSpeed is the stage-
   modified constant, base 82.5 px/s).
2. **Input → player velocity**:
   - Touch joystick (if a pointer is down): velocity from joystick angle,
     magnitude `maxSpeed` when force > 16 px else 0.
   - Otherwise keyboard: arrows or WASD give a (-1/0/1, -1/0/1) vector; if
     non-zero it is normalised and scaled by `maxSpeed` (diagonals are not
     faster). Line 6981.
   - Otherwise gamepad: d-pad like keyboard; if d-pad idle, left stick axes
     with a 0.3 dead zone, each axis `× maxSpeed` *without* normalising
     (diagonal stick input can exceed `maxSpeed`). Button 9 pauses. Line 6986.
   - No input: velocity 0 and `lastFacedDirection` keeps the last non-zero
     velocity (used by directional weapons).
3. **Enemy culling, 10 checks per frame** (line 6964): a round-robin index
   walks the enemy group; for each checked enemy that is cullable or
   teleport-on-cull, if `|dx| > 0.7·W` or `|dy| > 0.8·H` from the player
   (W,H = render size), it is despawned (or teleported for bosses, see
   `spawning.md`). Lines 24–49.
4. **Bullet culling, all bullets every frame**: rectangle 1.2·W × 1.3·H
   centred on the player; outside → despawn. Line 6967.
5. **Core.Update(delta)** (line 6680):
   - `tick++`; every 4th frame (`updateFreq = 4`, line 6357) the "slow" block
     runs: level-up check (`xp >= XpRequired` → `player.LevelUp()`, open the
     level-up screen, recalc loot weights, recompute XP factor, refresh UI),
     day/night hue, then `Update` on every pickup and every explosion.
   - every frame, in this order: every bullet, every weapon (fires via its own
     timer; `Update` only accumulates lifetime), every hidden weapon, the
     player, the magnet (snaps to player, y − 12), the background, every enemy.
6. **Stage.Update**: only the tiling-tileset follower (library stage).

Depth sorting: enemies and pickups set `depth = y − player.y` (pickups +9),
player depth 0, so draw order is by y relative to the player.

## Timer-driven tasks

- **Ticker** (line 6385, 1000 ms loop): `SurvivedSeconds++`; when divisible by
  60 → `Stage.CheckMinute()` (`spawning.md`).
- **Regen** (line 364): 1000 ms loop, `player.Regenerate()`.
- **Spawn timer**: loop with the stage's current `frequency` ms, restarted on
  every minute change (`spawning.md`).
- **Destructible timer**: loop `destructibleFreq` ms.
- **Weapon firing timers**: one per weapon, loop `PInterval` ms (or
  `duration + PInterval` when `intervalDependsOnDuration`), restarted when the
  weapon levels up or cooldown stat changes (`weapons.md`).
- Blink/invulnerability, self-destruct countdowns, event durations: one-shot.

## Level-up pause

When the level-up screen opens, the main scene's input is blocked
(`blockInput`) and the UI scene runs on top; the main scene is *paused by the
scene manager* (timers and physics stop). Music fades to 0.2 and back to 0.3
on exit. Lines 6486–6497.

## Game over

Player death → 1250 ms later the game-over scene (`player.md`).
