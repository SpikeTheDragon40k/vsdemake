# Enemies

Lines refer to `work/main.deob.js`. Enemy class 4446–4586, pool 4588–4616.

## Data → instance (`Init`, line 4459; `OnRecycle`, line 4505)

From the enemy's table row (`enemies.json`, element 0):

- `speed`: if `maxSpeed` is set, uniform random in `[speed, maxSpeed]` per
  instance (chosen once at pool creation of that instance, **not** per
  spawn); else `speed`.
- `maxHp` (data), `power`, `knockback`, `maxKnockback`, `deathKB`, `xp`,
  `moreX`, `moreY`, `alpha` (default 1), `scale` (default 1), `res_Freeze`,
  `res_Debuffs`, `res_Rosary` (default 0), `colliderOverride`,
  `idleFrameCount`, `passThroughWalls`.
- `skills` (enum ids): 0 HPxLevel, 1 FixedDirection, 2 SelfDestruct,
  3 Medusa. Medusa starts with `medusaElapsed = 2 × random()`.
- Sprite: one of `frameNames` picked at random. Death animation: frames
  `<prefix>0..end` at 24 fps, once. Idle animation (if `idleFrameCount`):
  `<prefix>i01..iNN` at 8 fps, looping, random start frame in the first half.

On each spawn (`OnRecycle`):

- `target = player`.
- `maxHp = dataMaxHp × EnemyHealthMultiplier`; with HPxLevel:
  `maxHp = dataMaxHp × player.level × EnemyHealthMultiplier`. `hp = maxHp`.
- FixedDirection / Medusa: direction reset to (0,0).
- `damageKB = 1`, alive, visible, body enabled, not time-stopped,
  `speed = defaultSpeed`.
- Collider: circle radius `r = 0.4 × spriteWidth`, offset `(r/2, r/2)`.
  With `colliderOverride`: `r = spriteWidth × override.radius`, offset
  `(r/2, r/2 + override.offsetY)`.
- Origin (0.5, 1), scale `PixelScale × scale`.
- Enemies without `passThroughWalls` join the physics group that collides
  with other enemies and with tile collision layers; the rest overlap freely.
- If the game is currently time-stopped the new enemy starts stopped.

## Per-frame update (line 4565)

```
if dead: return
depth = y - player.y
if timeStopped: return
if not FixedDirection, or FixedDirection and direction == (0,0):
    direction = normalize(target.pos - pos)
if Medusa:
    medusaElapsed += 0.05          # per frame
    direction.y = cos(medusaElapsed)   # x keeps chasing, y oscillates
if SelfDestruct and not triggered and distSq(player) < 40000 (200 px):
    triggered; show pulsing red ring (200 ms yoyo, speeds up 10 % per repeat,
    alert sound); after 2000 ms -> OnSelfDestruct
k = receivingDamage ? -knockback × damageKB : 1
flipX = direction.x > 0
velocity = EnemySpeed × speed × direction × k      # px/s
angle = shared wobble (one of 10 tweens, -5°..5° over 500 ms, yoyo)
```

`EnemySpeed` is the stage-modified constant (base 0.77 × 0.3 = 0.231), so
a bat with `speed 140` moves at 32.3 px/s. Knockback is simply reversed
velocity scaled by `knockback × damageKB` for the duration of the hit blink.

`OnSelfDestruct` (line 4513): if alive, hide the ring, `MakeExplosion(x, y,
moreX, moreY)` (explosion radius/damage from the row's moreX/moreY), then
`Disappear` (death animation, no drops).

Alert ring (line 4465, 4571): `sPFX_ring_64`, origin (0.5, 0.75), additive,
red, following the enemy; tween alpha 1 → 0 and scale 1 → 0.9, 200 ms,
yoyo, repeating forever; every repeat (400 ms pass) multiplies its time
scale by 1.1 and plays `Alert` (volume 0.2, 250, 3). It is a tween, so it
keeps going while time is stopped.

## Explosions (`MakeExplosion`, line 6654; class line 6258)

Pool `SpawnAt(x, y, damage = moreX, radius = moreY)` (Shade Red rows:
damage 20 / 50, radius 60). Lifetime 120 ms (a tween of the ground circle,
then `DeSpawn`). Each frame while alive: if it has not hit yet and the
player position is inside the circle (radius fixed at `radius`), the
player `GetDamaged(damage)` once. Sound `Explosion` (volume 0.5, detune
500 × (r − 0.5), 150, 3).

Looks (decoration):
- Smoke emitter: `Smoke1/2`, life 1000, rotate 0–360, angle 0–180 in 16
  steps, speed 100–200, 4 per frame, scale 1 → 0, alpha 0.5 → 0.
- Flash emitter: `HitSmoke1/2`, life 1000, angle 0–360 in 16 steps, speed
  50–80, 4 per frame, scale 1 → 0, additive.
- Both run from spawn to despawn and share a gravity well at (x, y − 20),
  power 1, epsilon 50, gravity 20 (Phaser: factor = power × gravity × dt /
  (max(d², ε²) × d) × 100, added to the velocity × offset).
- Ground: a red circle (alpha 0.4, additive) growing from radius 1 to
  `radius` in 120 ms, only with flashing VFX on.

## Taking damage (`GetDamaged(dmg, hitType, kb=1)`, line 4517)

```
if dmg > 0 and damage numbers enabled: show number
hp -= dmg
if hp <= 0: Die() else damageKB = kb
play hit sound (volume 0.5, random detune ±250, throttled)
if hitType != NONE: spawn hit VFX at (x, y)
OnGetDamaged(hitType)
if dmg >= 10000: player.GetDamaged(9)
```

`OnGetDamaged` (line 4523): if flashing VFX enabled and hitType ≠ NONE, tint
from the `hitvfx` table (`isTintFill` → fill tint, else multiply tint,
`targetTint`); start blink timer: `duration` from the table (default 120 ms,
60 ms if already dead); `receivingDamage = true` until it fires, then tint
restored (blue 0x0000ff if time-stopped, else white).

Bullets skip enemies they have already hit (`weapons.md`), so one bullet
damages each enemy once; the blink does not grant enemies invulnerability.

## Death (`Die`, line 4548)

```
if dead: return
dead = true
velocity = EnemySpeed × speed × direction × (-deathKB)   # death fling
play death animation; cancel blink timer
gems = floor((random() + 0.5) × xp)        # 0.5xp .. 1.5xp, floored
if gems > 0: MakeGem(x, y, gems)
if a treasure is attached: MakeTreasure(x, y, treasure); detach
row.killedAmount += 1; run kills += 1; UI
```

When the death animation completes → `DeSpawn`: body off, invisible,
returned to its pool, removed from the global enemy list and the collision
group; the enemy's owner (a weapon that spawned it, if any) is told.

`Disappear` (used by events and Erase Enemies): `dead = true` and play the
death animation only — no gems, no treasure, no kill count.

## Freeze and time stop

- `Freeze(ms, strength=1)` (line 4483): ignored if `res_Freeze > strength` or
  already time-stopped. Else speed 0, velocity 0, blue tint, idle anim paused;
  resumes after `ms` (speed back to default).
- `TimeStop` / `ResumeFromTimeStop` (Orologion): same effect, cleared
  globally by `Core.ClearTimeStop`.
- Rosary (`Core.RosaryDamage`, line 6539): every enemy without `res_Rosary`
  that is within the 0.6-screen containment rect takes `max(66, maxHp)`
  damage with hitType NONE.

## Culling and bosses

- `isCullable` default true; event-spawned enemies are not cullable for their
  duration (`stage-events.md`).
- Bosses spawned by the minute table have `isTeleportOnCull`: instead of
  despawning when far away they are moved to
  `GetPositionForEnemyOutOfSight(5°, 48 px)` (line 4557, 6706): on tileset
  stages, to the left/right outer-rect edge at the player's y; otherwise
  within ±2.5° of the player's heading, at half the screen size plus 48 px.
