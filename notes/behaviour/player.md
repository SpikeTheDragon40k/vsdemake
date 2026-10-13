# Player

Lines refer to `work/main.deob.js`. Player class: lines 332–479. Stat
application in Core: 6735 (bought power-ups), 5410–5422 (passive items).

## Stats

| Stat | Construction default | Meaning |
| --- | --- | --- |
| hp / maxHp | 100 / 100 | current / maximum health |
| power | 1 | multiplies weapon power |
| area | 1 | multiplies weapon area |
| speed | 1 | multiplies projectile speed |
| cooldown | 1 | multiplies weapon interval (clamped ≥ 0.1 when used) |
| amount | 0 | added to weapon amount |
| moveSpeed | 1 | multiplies `PlayerPxSpeed` |
| growth | 1 | multiplies XP from gems |
| duration | 1 | multiplies weapon duration |
| luck | 1 | drop chances, level-up option count, treasure quality |
| greed | 1 | gold multiplier (see `pickups.md`) |
| magnet | (on the Magnet object) | pickup radius, base 30 px |
| armor | 0 | flat damage reduction |
| regen | 0 | HP per second |
| shields | 0 | hits absorbed |
| shieldInvulTime | 240 ms | blink after a shield absorbs |
| level | 1 | |

Physics body: circle radius 9, offset (6, 10), origin (0.5, 1), immovable.
Scale `PixelScale` (1). Sprite `characters/<spriteName>`, walking animation
frames 1..walkingFrames at 8 fps when `walkingFrames` is set, otherwise a
±5° wiggle (250 ms yoyo) while moving.

## Initialisation (`MakeLevelOne`, line 399, called from `InitGame`)

1. `level = 0`, `shieldInvulTime = 240`.
2. Copy every field of the character's row 0 onto the player (only fields the
   player already has: the stats above, `startingWeapon`, `level` (→ 1), …).
3. `hp = maxHp`.
4. If the row has `magnet > 0`: `Magnet.radius += Magnet.radius × magnet`,
   `Magnet.percentage += magnet`.
5. Then the stage is created (which may add `hyper.LuckBonus` to `luck`), the
   starting weapon is added, and every bought shop power-up rank is applied
   (below). Line 6399–6405.

## Bought shop power-ups (`ApplyBoughtPowerup`, line 6735)

For each rank entry `r` of each power-up (ranks 0..level−1):
`power, area, speed, cooldown, duration, amount, moveSpeed, growth, luck,
armor, regen, greed += r.<stat> || 0`;
`maxHp += maxHp × (r.maxHp || 0)`; `hp = maxHp`;
`Magnet.radius += Magnet.radius × (r.magnet || 0)`; `Magnet.percentage += r.magnet || 0`.

## Character level-ups (`LevelUp`, line 390)

`level++`; find the character row whose `level` equals the new level; if it
exists, add each of its fields (except `level`) to the player's same-named
field. If that row has a `cooldown` field, all weapon firing timers restart
(`Core.ResetWeaponCooldowns`). Character rows are data (e.g. Antonio gains
power every 10 levels); nothing else is hard-coded per character.

## Passive items (`weapons` table entries with `isPowerUp`, lines 5410–5422)

A passive is a weapon object with no pool. On level-up it first resets its own
bonus fields to 0, applies the table delta for the new level to itself, then
`Apply` adds its fields to the player:
all stats additive; `maxHp += maxHp × item.maxHp` (fractional);
`cooldown > 0` → reset weapon timers (cooldown deltas in the data are
negative, so in practice timers are *not* reset by Empty Tome; keep this
behaviour); `magnet > 0` → `Magnet.radius += radius × magnet`,
`percentage += magnet`.

## Weapon stat composition (lines 5299–5322)

```
PArea     = player.area  × weapon.area
PAmount   = player.amount + weapon.amount
PPower    = player.power × weapon.power
PSpeed    = player.speed × weapon.speed
PInterval = max(0.1, player.cooldown) × weapon.interval      (ms)
PDuration = player.duration × weapon.duration
```

## Movement

`velocity = dir × PlayerPxSpeed × moveSpeed` (see `loop.md` for how `dir`
is built). Sprite flips to face velocity.x sign. When standing still the
wiggle/walk animation pauses and the angle resets to 0. A 4-entry position
history (updated every frame, ring index) draws a translucent trail copy of
the sprite behind the player (alpha 0.65, tint 0xe0e0ff) — cosmetic.

## Taking damage (`GetDamaged`, line 406)

Called from the player–enemy collider every physics step for each
overlapping enemy that is alive and not time-stopped, with `dmg = enemy.power`.

```
if receivingDamage or IsInvul or hp <= 0: return
if shields > 0:
    shields -= 1
    blink(colour 0xffffbb, shieldInvulTime=240 ms, no particles)
    emit Player_LostShield
    return
if armor > 0: dmg = max(1, dmg - armor)
hp -= dmg
if hp <= 0: hp = 0; Die(); Core.GameOver()
else: blink(red, 120 ms, blood particles + hit sound)
```

`blink` sets `receivingDamage = true` until its timer ends, which is the
player's invulnerability window: **120 ms after a normal hit, 240 ms after a
shield hit**. Damage from several enemies in the same step is therefore one
hit per window.

`IsInvul` is a separate timed flag: `invulTime -= delta` every frame; invul
while > 0; tints the sprite 0xffffbb. Set by Revive (2000 ms) and debug.

Quirk (line 4521): any single hit of ≥ 10000 damage dealt *to an enemy* makes
the player take 9 damage (an anti-cheat).

## Healing

`RecoverHp(n)`: `heal = min(maxHp − hp, n)`; `hp += heal`; heals below 9000
are counted in run stats. Regen tick every 1000 ms: if `regen > 0`,
`RecoverHp(regen)`.

## Death and revive

`Die` → `isDead`, body disabled, death animation (scaleX → 2, scaleY → 0 over
1000 ms, red tint, particles 250 ms), game-over scene after 1250 ms.
`Revive`: `isDead = false`, body enabled, full heal, invulnerable 2000 ms.
(The revive decision itself is in the game-over flow, not yet documented.)
