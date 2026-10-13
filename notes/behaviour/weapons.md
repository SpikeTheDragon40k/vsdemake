# Weapons

Lines refer to `work/main.deob.js`. Weapon base class 5290–5408, weapon
subclasses 5434–5700, factory 5912–5972; bullet base 3010–3108, bullet pool
4370–4426, bullet subclasses 3109–3300 and onward.

## Weapon object (one per owned item)

Fields come from the weapons table entry 0 at creation (`MakeLevelOne`,
line 5390: every field of the entry that the object already has is copied;
`poolLimit` sets the bullet pool cap; `evolvesFrom` items are removed from
the player). Defaults before the copy (line 5293): interval 1000,
repeatInterval 100, power/area/speed/amount 1, duration 1000, critChance 0,
critMul 2, charges 0, hitBoxDelay 1000, hitVFX DEFAULT, knockback 5,
hitsWalls true, penetrating 1, bounces 0.

Composed stats (`player.md`): `PArea`, `PAmount`, `PPower`, `PSpeed`,
`PInterval`, `PDuration`.

### Firing timer (line 5335)

Looping timer, period `PInterval` ms (or `duration + PInterval` when
`intervalDependsOnDuration`). Restarted on level-up, on
`Core.ResetWeaponCooldowns` and at creation. Note `duration` here is the raw
weapon duration, not `PDuration`.

### `Fire` (line 5365)

```
FireOneBullet(player.x, player.y, index 0, target)
for i in 1 .. PAmount-1:
    if repeatInterval × i > 0: after repeatInterval × i ms → FireOneBullet(..., i, target)
    else: FireOneBullet(..., i, target) now
```

`FireOneBullet` → `pool.SpawnAt(x, y, weapon, i)`; returns nothing if the
pool is at `upperLimit` (default 50, overridden by `poolLimit`). The bullet's
`indexInWeapon = i` drives per-bullet offsets. If a target was set,
`bullet.SetTarget(target)`.

### Hits (line 5343)

Bullet–enemy overlap each physics step: skip dead enemies and enemies this
bullet already hit (`objectsHit` list, reset on recycle); otherwise
`enemy.GetDamaged(PPower, hitVFX, knockback)` and the bullet's
`OnHasHitAnObject(enemy)` runs once per enemy. Destructibles: same, damage
`PPower`. Walls (tileset stages, only if `hitsWalls`): `OnHasHitWall`,
default despawn.

Crits (`critChance`, `critMul`) are not applied in the base path; only
weapons that override damage use them (document per weapon).

## Bullet object (line 3010)

- `TrueSpeed = PSpeed × ProjectileSpeed × _speed` px/s (`ProjectileSpeed`
  base 165, stage-modified; `_speed` is a per-bullet-class factor, default 1).
- `OnRecycle`: alive, `objectsHit = []`, `penetrating = weapon.penetrating`,
  `bounces = weapon.bounces`.
- Aiming helpers: `AimForNearestEnemy` (closest enemy to the *player*, else
  fly in the player's facing direction); `AimForRandomEnemy`;
  `ApplyInitialVelocity(target)`: `v = TrueSpeed × normalize(target.body.pos −
  player.pos)` and rotate the sprite to the velocity;
  `ApplyPlayerFacingVelocity`: `lastFacedDirection` normalised, (1,0) if zero.
- Culled when outside 1.2·W × 1.3·H around the player (`loop.md`).

## Whip (WHIP; weapon 5496, bullet 3139)

The weapon class adds nothing; all behaviour is the bullet.

- Sprite `vfx/slash.png`. On first creation: scale 0 → `PArea` over 100 ms.
- On each spawn (`OnRecycle`, line 3149): alpha 1; if `PArea` changed since
  the last shot, rebuild the scale tween; restart it. Then a fade: alpha 1 → 0
  over 100 ms after a 100 ms delay, and **despawn on completion** (so a whip
  hitbox lives 200 ms).
- Placement: `side = indexInWeapon odd`, mirrored when the player faces left
  (`flipX`): `x = player.x ± 64` (64 px toward the facing side for even
  indices, the opposite side for odd), `y = player.y − 16 − 16 × index`.
  Sprite flipped vertically for odd indices and horizontally when on the left.
- No velocity; stays where spawned (the player can move out of it).
- Hits every enemy overlapping the sprite body once (no `penetrating`
  handling: unlimited pierce). `hitsWalls` false in data, so it ignores walls.
- Sound: whip, detune −100 × index, volume 1.7, rate 2.
- Stats per level from the table (`weapons.json` WHIP): interval 1350,
  repeatInterval 100, power 1, area 1, speed 1, amount 1, poolLimit 10,
  `hitsWalls` false at level 1; later levels add amount/power/area (data).
  The hitbox is the sprite's rectangle (147×22 source pixels × `PArea`),
  centred on the spawn point (origin 0.5, 0.5).

## Magic Wand (MAGIC_MISSILE; weapon 5491, bullet 3109)

The weapon class adds nothing; the base `Fire` runs with no explicit target.

- Sprite `vfx/ProjectileHoly1.png`, circle body radius 8, scale `PArea`.
- On spawn: position jittered by `(random − 0.5) × index × 10` on each axis;
  `AimForNearestEnemy()` (closest enemy to the player; facing direction if
  none). Velocity `TrueSpeed` in that direction.
- `OnHasHitAnObject(enemy)`: if the enemy is alive, `penetrating -= 1`; at
  0 → despawn. So `penetrating` = number of enemies pierced (1 at level 1).
- `OnHasHitWall`: despawn (default).
- Emits a holy particle each frame (cosmetic). Sound: magic, detune
  −100 × index, volume `weapon.volume || 0.15`.
- Holy Wand (HOLY_MISSILE) uses the same bullet class with its own table
  entry and a weapon class to be documented with the evolutions.

## Other weapons

To be documented one by one in Phase 5. Factory mapping (line 5916) for
reference: AXE (default class, `PArea × 1.3`), SCYTHE, HOLY_MISSILE,
DIAMOND (random target from the enemy group), HOLYWATER (12 angle/radius
slots), FIREBALL (random target), KNIFE/THOUSAND, HOLYBOOK, VESPERS, CROSS,
LIGHTNING, GARLIC, LANCET, LAUREL, THORNS, BONE, HELLFIRE, SILF/2/3,
PENTAGRAM, HEAVENSWORD, VAMPIRICA, NDUJA, VORTEX. Passives share one class
(`player.md`).
