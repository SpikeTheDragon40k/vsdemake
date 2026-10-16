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
- Holy Wand (HOLY_MISSILE) uses a subclass of this bullet with no overrides
  (line 3137) and a plain weapon class: only its table entry differs. Other
  evolutions: `evolutions.md`.

## Other weapons

To be documented one by one in Phase 5. Factory mapping (line 5916) for
reference: AXE (default class, `PArea × 1.3`), SCYTHE, HOLY_MISSILE,
DIAMOND (random target from the enemy group), HOLYWATER (12 angle/radius
slots), FIREBALL (random target), KNIFE/THOUSAND, HOLYBOOK, VESPERS, CROSS,
LIGHTNING, GARLIC, LANCET, LAUREL, THORNS, BONE, HELLFIRE, SILF/2/3,
PENTAGRAM, HEAVENSWORD, VAMPIRICA, NDUJA, VORTEX. Passives share one class
(`player.md`).

## Lightning Ring (LIGHTNING; bullet 3869–3893, trail class 3838–3864)

- Each bullet picks a random living enemy whose body overlaps the screen
  rectangle (renderer size, centred on the player); none → the bullet is
  dropped.
- Starts at the target's x, at player y − 0.6 × screen height, and moves
  straight down to the target's y in 70 ms (linear tween). The body
  (circle radius 16 × scale, scale = 0.5 × PArea) is disabled while falling.
- On arrival: hit list cleared, body enabled, `Lightning` sound
  (volume 0.6, rate 2, detune −100 × index, 200 ms / 4). A circle of radius
  8, colour `[0x00ffff, 0x0000ff, 0x0088ff, 0xffffff][index % 4]`, alpha
  0.15, additive blend, drawn at the target, grows from scale 0 to PArea in
  60 ms (only with the flashing-VFX option, default on). Then the trail and
  circle are hidden and the bullet despawns.
- The bullet sprite (HitRed2) is never visible. What is seen is the trail:
  a Phaser Rope of `Lightning3.png` (480 × 17, horizontal) with 16 points
  following the bullet, every point starting at the start position. So the
  image is laid along the path from the start point to the bullet, 17 px
  thick, on top of everything (depth MAX_SAFE_INTEGER − 1000).
- The demake draws the trail as one 17 px strip rotated 90°, stretched from
  the start point to the bullet (the path is a vertical line). The additive
  blend is drawn as plain alpha.

## Peachone, Ebony Wings, Vandalier (SILF, SILF2, SILF3; weapons 5671, 5705, 5814; bullets 3899, 3961, 4234)

Weapon (one bird each; Vandalier is one bird with two zones):

- Zone geometry from the renderer height (456 landscape, 342 portrait):
  half of it is split into a zone radius of a quarter (57) and a distance
  from the player of the rest (171).
- Every update: `totalTime` and `angleTime` grow by the frame time. Zone
  angle = 0.001 × angleTime × PSpeed radians (PSpeed = player speed ×
  weapon speed). Peachone's damage zone is at that angle around the player,
  Ebony Wings' at minus that angle, Vandalier has both. The drawn target
  circle uses angleTime − rayDuration (500 ms; Vandalier 400), so it shows
  where bolts fired now will land. 1 px outline, Peachone 0x8888ff, Ebony
  0xff88ff, Vandalier white; alpha 0.5 while firing, 0.1 while resting.
- Duty cycle: when totalTime ≥ PDuration it becomes −PDuration × player
  cooldown (Vandalier: × interval / 1000 too) and firing is blocked; when it
  is back above 0 firing is allowed.
- Fire (only when not blocked): repeatInterval = interval / PAmount (the
  volley is spread over the interval); 12 random points in the damage zone
  (`Circle.Random`, uniform over the disc), 24 for Vandalier (12 per zone).
- The bird: target point = player ± 24 px on x (Peachone behind the
  player's facing, Ebony in front; Vandalier like Peachone). If the squared
  distance to it is over 5000 it flies to (target x, player y − 24) at
  PlayerPxSpeed / 1.178571 (Vandalier: PlayerPxSpeed) × player moveSpeed,
  flipped when moving left; otherwise it drifts at 35 px/s in its last
  direction. Animation `ProjectileBird1–2` (Peachone), `4–5` (Ebony),
  `7–8` (Vandalier) at 6 fps, on top.

Bullet:

- Takes the weapon's next point (`enemyIndex` modulo the list, counting
  up across volleys); none → despawns. Sound MagicMissile, volume 0.15,
  detune −100 × index, 200 ms / 12.
- Starts at the bird. Curve: a Phaser Spline through start, middle and
  target; the middle is the start turned about the target by ±(10° + r ×
  20°), sign alternating with the point counter. Vandalier's middle is
  (player x + 50 × (r − 0.5), player y − 150), so its bolts arc from above.
  Moves along the curve by arc length over rayDuration.
- Sprite `HitWhite1` (Peachone, Vandalier) / `HitBlack1` (Ebony) at scale
  0.5 while flying. Trail: Rope with 24 points of `Gradient3_4px` (Peachone;
  6px when PArea ≥ 1.5, 8px when ≥ 2; additive, alpha 0.4), `Gradient4_4px`
  (Ebony; 6px only when 1.5 ≤ PArea < 2; alpha 1), `Prism<1 + pool index %
  9>_8px` (Vandalier, alpha 0.4). Trail fades in over 200 ms.
- On arrival: body enabled (circle radius 16 × sprite scale), the sprite
  grows to PArea and fades out in 120 ms (hidden without flashing VFX), the
  trail fades out in 200 ms, then the bullet despawns. Each enemy is hit
  once per bullet. Hit VFX LIGHT / DARK / PRISM, knockback 2.
- Vandalier: `requires` Peachone at level 8; added to the evolution pool
  when Ebony Wings reaches level 8 (`addEvolvedWeapon`), removes both
  (`evolvesFrom`). Exdash starts with Ebony Wings.

Demake: the curve is followed by its parameter instead of arc length (it
only changes where the bolt is drawn in flight), the trail is drawn as 24
strips along the curve, the circle outline as dots, plain alpha instead of
additive blend; the hit circle is centred on the bolt (the original's is
offset by 2.5 px × scale).

## Bone (BONE; default weapon class, bullet 3693)

- Fire: PAmount bullets at once. Each aims at a random enemy
  (`AimForRandomEnemy`: towards it, or the facing direction without
  enemies) at TrueSpeed, spinning (360° per s), scale 0.5 × PArea, circle
  radius 8 × scale; sound Shot 0.45, −100 × index cents, 200 ms / 10.
- Bounces: on the WorldBoxCollider (screen-sized box around the player) and
  on walls (velocity flipped on the axis it hit), each time clearing its hit
  list. Hitting an enemy that survives sends it straight back (velocity
  × −1) and clears the list.
- After PDuration it shrinks to 0 in 500 ms and despawns.

## Clock Lancet (LANCET; weapon 5619, bullet 3764)

- PAmount is always 6; power 0 (no damage), "ignores power, speed, amount,
  area".
- 12 directions (2π × k / 12); `ticks` starts at 8 and steps by one per
  Fire, so the line turns 30° each shot.
- Fire: a streak (`Pierce1–5`, 30 fps, origin (0, 0.5), scale 2 × 1, tint
  0xcceeff, alpha 1 then fading from 200 to 700 ms) at the player along the
  direction, and 6 invisible bullets at the player + 50 × (1 + index) along
  it: circle radius 16 × 2, gone after 20 ms.
- Hit (enemy alive): `Freeze(PDuration, 1)`: ignored if `res_Freeze` > 1
  or the enemy is already stopped; otherwise speed 0, tinted blue, until the
  timer ends (a new freeze replaces it). The Orologion's time stop cancels
  freezes. Hit VFX TIMEFREEZE.

## Laurel (LAUREL; weapon 5562)

- PAmount 1; `maxCharges` 3; `charges` from the table (1, 2 at level 4, 3
  at level 7). Fire (also right after it is obtained): +1 player shield if
  shields < charges and < 3. No bullet.
- A rank with `shieldInvulTime` adds it to the player's (base 240 ms).
- Player damage with a shield up: a shield is spent instead, the player
  blinks pale yellow for shieldInvulTime and is invulnerable meanwhile
  (player GetDamaged, line 409).
- Look: `Shockwave2` at the player (16 px up), scale 1.25, alpha 0.3 ↔ 0.6
  (2 s), turning once in 12 s, tinted by the shields (1 0x8888ff, 2
  0x88ff88, 3 0xffff00), hidden with none.

## Pentagram (PENTAGRAM; weapon 5737, bullet 4045)

- Interval 90 s (−10 s at levels 2, 4 and 6); fires 100 ms after it is
  obtained (MakeLevelOne) and, like every weapon, on each level-up. One
  bullet whatever the amount. `chance` 0.1, +0.15, +0.2, +0.2.
- Fire: a white screen flash (alpha 0 → 0.5 → 0 in 200 ms, flashing VFX
  only); sound PentagramSFX 0.9, rate 2.
- `bad = random() × (1 / luck) > chance` (usually true). 100 ms later every
  enemy in containmentRect_Screen (0.6 × screen around the player) without
  `res_Rosary`: bad → `Disappear()` (dead, death animation, no gem, no kill
  counted); else `GetDamaged(max(66, maxHp))` (a normal kill with drops).
  Bad also despawns the pickups and destructibles in that rectangle.
- Look: a picture 0.8 × the short screen side at the player, over
  everything: a disc (radius 7/16 of that; bad 0x550044 alpha 0.5, good
  0x0000ee alpha 0.35), `outer0`, the outer ring (`outer1`, `outer2` from
  level 3, `outer3` from 6), the inner star (`inner1`, `inner2` from 4,
  `inner3` from 7) and `center`, filled white-yellow; it shatters into
  pieces that fly apart, turn ±90° and fade over 1 s after 150 ms.

Demake: Pentagram's pieces are 4 × 4 tiles per layer instead of triangles,
layers tinted rather than filled.
