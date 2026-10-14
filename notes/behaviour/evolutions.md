# Evolutions and passive items

Lines refer to `work/main.deob.js`. Weapon factory 4394–4424, bullet
factory 5913–5975, passive class 5410, base weapon `onBulletOverlapsEnemy`
5343, base bullet 3010–3080.

## How an evolution is obtained (already in `levelup.md`, `treasure.md`)

1. A weapon whose last level entry has `addEvolvedWeapon` reaches that
   level: the evolution joins `SpecialWeapons`.
2. A treasure chest slot of type EVOLUTION pulls a special evolution whose
   `requires` items are all owned (level ≥ `requiresLevel`, default 0).
3. Adding the evolution removes the `evolvesFrom` weapons.

Pairs in 0.2.10 (data): Whip + Hollow Heart → Bloody Tear; Magic Wand +
Empty Tome → Holy Wand; Knife + Bracer → Thousand Edge; Axe + Candelabrador
→ Death Spiral; Cross + Clover → Heaven Sword; King Bible + Spellbinder →
Unholy Vespers; Fire Wand + Spinach → Hellfire; Garlic + Pummarola → Soul
Eater; Peachone + Ebony Wings (Peachone at level 8) → Vandalier.

## Locked items

Most passives (all but Spinach and Armor) and several weapons have
`isUnlocked` false in the data; the original unlocks them through
achievements and its save file. The level-up store excludes locked items
(line 1458). The original also has a developer switch that marks every item
unlocked (line 7296, off in the release). The demake follows the save
(`saves.md`); `--unlock-all` (PC) acts like that switch. Weapons the demake
does not implement are never offered.

## Passive items (class at line 5410)

On level 1 and on every level-up: reset the bonus fields, take the level
entry's values, then add them to the player: power, area, speed, cooldown,
amount, moveSpeed, growth, luck, armor, duration, greed, regen as plain sums;
`maxHp += maxHp × bonus`; a positive cooldown bonus resets all weapon
timers; magnet: `radius += radius × bonus`, `percentage += bonus`.

## Bullet behaviour of each evolution

Speeds are `PSpeed × ProjectileSpeed × _speed` with `_speed` = 1 unless
noted. "Body" radii scale with the sprite scale (`PArea`).

### Bloody Tear (VAMPIRICA), bullet line 4205, weapon 5793

- Placement, flip and hitbox exactly as the Whip.
- Scale 0 → PArea over 100 ms; alpha 1 → 0 over **200 ms** after a 100 ms
  delay, then despawn (the Whip fades in 100 ms).
- Drawn tinted 0x110011 with two ghosts: tint 0xff0000 at (+4, +2) and
  tint 0x660066 (additive) at (−4, −2).
- Weapon: on every overlap frame, `crit = random < critChance × luck ?
  critMul : 1`. If crit and the heal is ready: recover 8 HP, show a recovery
  number, heal ready again after 1000 ms. Then, if the bullet has not hit
  this enemy yet, damage `PPower × crit`.

### Death Spiral (SCYTHE), bullet line 3787, weapon 5656

- Weapon `PArea = 1.2 × player.area × area`.
- Body circle 16; scale PArea; velocity angle
  `360° / PAmount × index − 90°`; spins 360° per 500 ms.
- Straight line, no gravity, no lifetime: culled off-screen. Penetrating
  1000 (data); `hitsWalls` false.

### Hellfire (HELLFIRE), bullet line 3732, weapon 5546

- Weapon picks a random enemy as target on Fire (as the Fire Wand).
- Body circle 16; scale 0 → PArea over 200 ms; no y offset.
- Velocity angle = angle to target + offset[index mod 9], offsets
  (0, 10, −10, 20, −20, 30, −30, 40, −40)°; sprite angle follows the
  velocity.
- Does not despawn on hits (the base handler does nothing): passes through.

### Heaven Sword (HEAVENSWORD), bullet line 4119, weapon 5779

- Start at the player plus `(random − 0.5) × index × 20 × PArea` on each
  axis; aim at the nearest enemy (sprite angle = velocity angle).
- Body circle 12; scale PArea; not cullable at first.
- Velocity multiplier `accel`: 2 → 0 linearly over 500 ms; then it turns
  back: hit list cleared, spins 360° per 1000 ms, `accel` 0 → −2 over
  500 ms and stays −2; becomes cullable 500 ms after turning.
- Weapon: crits like Bloody Tear (no heal): `PPower × crit` on first hit.

### Soul Eater (VORTEX), bullet line 4341, weapon 5853

- Bullet as Garlic: follows the player, body radius 16 × 2 × PArea,
  expires after `PInterval`, adds knockback/freeze-resistance changes on
  hit; `PAmount` is always 1.
- From the moment the weapon exists, every HP actually recovered is summed.
  At each Fire, `bonus = min(6, recovered / 600)`; `PPower = player.power ×
  power + bonus`.
- When its hit kills an enemy: `r = random`; if `PPower / 7 × 0.15 > r`, a
  Little Heart spawns at the enemy offset by `10 × (r − 0.5)` (x +, y −),
  already homing at full speed.
- Drawn as a black circle of diameter `2 × PArea × 36` at alpha 0.6, and a
  second black circle whose alpha pulses 0.1 ↔ 0.6 (1000 ms each way, 100 ms
  pause) and whose scale shrinks to 0 every 6000 ms.

## Not implemented yet

Vandalier and its parts (Peachone, Ebony Wings), Pentagram, Clock Lancet,
Laurel, Bone. These are never offered in level-ups or chests.
