# Pickups

Lines refer to `work/main.deob.js`. Base pickup 539–582, coin 584–601,
gem 602–649, vacuum 650–675, rosary 676–699, magnet 480–493, pools
1358–1401, gem merging in `Core.MakePickup` 6607–6633.

## Common

- Sprite from `items` atlas, frame and `value` from the `pickups` table.
  Body circle radius 10. Pickups are not in the enemy culling list; gems
  are only removed by the gem cap (below).
- `Update` runs every 4th frame (`loop.md`): if `goToPlayer`, move toward
  the player.
- `GoToPlayer` (line 574): `dir = normalize(player − (x, y) + (0, −8))`;
  `velocity = speed × dir × t`; `speed += 1` per update. `t` is tweened from
  its start value to 1 over 500 ms (linear). Vacuum sets `t = −1` first, so
  a vacuumed gem drifts *away* briefly, then accelerates in.
- `Vacuum()` (line 570): if not already homing, restart the tween, `t = −1`,
  `goToPlayer = true`. Called when the magnet circle overlaps the pickup.
- Magnet (line 480): invisible circle radius 30 px (scaled by player scale),
  follows the player at `y − 12`; `percentage` tracks total magnet bonus.
  Radius grows multiplicatively: `radius += radius × bonus` per bonus.
- `GetTaken` (base, line 562): despawn and `table.pickedupAmount += 1`.
  Pickup happens on player–pickup overlap (line 6518).

## Gems (line 602)

- Spawned at an enemy's death position with `value = gems` (see
  `enemies.md`). Frame by value: 1–2 blue, 3–9 green, ≥10 red.
- Throw: random angle `a ∈ (−π, π)`, landing point
  `(x + 100 cos a, y + 100 sin a)`, cubic Bezier with both control points at
  `(x + 100 cos a, y + 200 sin a)`, followed over 500 ms. (Only cosmetic:
  physics position is set from the curve each update.)
- Depth: `y − player.y − H` (always behind enemies).
- Taken: `player.xp += value × player.growth`; gem sound (volume 0.1,
  throttle 1 ms / 1, no pitch change: a 24-step × 60-cent melody table and
  its getter exist at lines 619–649 but are never called); level-up check.

### Gem cap and merging (`MakePickup` for GEM, line 6610)

`MaxGems = 400` (line 6351). When a new gem is requested and the live gem
count exceeds the cap:

```
acc = 0; stoppedOnScreen = false; tries = 0
while count > MaxGems:
    g = farthest gem from the player
    if g is within the 0.6-screen rect: stoppedOnScreen = true; break
    acc += g.value; despawn g
    if ++tries > MaxGems: break
g = farthest remaining gem; g.value += acc   (if any)
if stoppedOnScreen: g.value += newValue; return null   # no new gem
spawn the new gem at the requested position
```

Otherwise the gem is simply spawned. Requests with no position use
`GetPositionOutOfSight(90°)`.

## Coins (line 584)

Taken: `PlayerOptions.AddCoins(value)` (line 2794):
`coins += value × GoldMultiplier × player.greed` (also added to run coins and
lifetime coins, then saved), logged in run pickups, coin sound. Depth `9 + y − player.y`. COINBAG1/2/MAX are subclasses with values
10/25/100 (data).

## Others (all: logged in run pickups, mark item seen for the collection)

| Type | Effect |
| --- | --- |
| ROAST (Floor Chicken) | `RecoverHp(30)` (value from table) and 300 ms invulnerability (line 765) |
| ROSARY | `Core.RosaryDamage()` — on-screen enemies without `res_Rosary` take `max(66, maxHp)` |
| OROLOGION | `Core.TimeStop()` — all enemies stopped (`enemies.md`); cleared after **10000 ms** (`TimeStopConfig`, line 6352); picking up another restarts the timer |
| VACUUM | `Core.TurnOnVacuum()`: every live gem gets `goToPlayer = true`. A gem's `t` tween ran 0 → 1 once at creation, so landed gems home at full speed (no drift-away like the magnet's vacuum) |
| CLOVER | `luck += value` (0.1) (line 751) |
| LITTLEHEART | `RecoverHp(1)` (hard-coded; the table value is 0) (line 1351) |
| TREASURE | opens the treasure screen (`treasure.md`) |
| WEAPON | grants a weapon type set on the pickup |
| NFT (Nduja Fritta) | temporary hidden weapon: flame cones (`weapons.md`) |

All rows above were checked against the subclass `GetTaken` methods.

## Drop odds from candles and braziers (LootManager, line 6056)

Every destroyed light source drops one item drawn by weight from the
pickups table: items with `unlocksAt` ≤ player level, weight `rarity`
(× luck for `isRare` items). In 0.2.10: Coin 50, Floor Chicken 12 (rare),
Coin Bag 10, NFT 1 (rare), Clover 0.5; Orologion 2 from level 4, big coin
bag 1 from level 5, Rosary 1 from level 8, Vacuum 2 from level 12 (all
rare). Measured in the engine over 100 000 draws: level 1 → coin 68 %, bag
14 %, chicken 16 %, NFT 1.3 %, clover 0.7 %; level 12 → coin 63 %, bag 13 %,
chicken 15 %, Orologion 2.5 %, Vacuum 2.6 %, Rosary 1.3 %, big bag 1.3 %.
The table is recalculated on every level-up.

## NFT → Nduja Fritta (pickup 1312, weapon 1136, bullet 1e8f43)

- Taken: the hidden weapon NDUJA is added (not in the weapon list or slots,
  not levelled, not a chest prize) for 10 000 ms × duration; another NFT
  restarts the time. The pickup animates `Nft1–4` at 10 fps.
- Weapon: interval 500, repeatInterval 40, amount 12 (+ player amount),
  power 3, area 2, speed 3, duration 1000, hit VFX FIRE.
- Bullet: 24 px above the player, velocity = facing direction × TrueSpeed +
  100 × (sin index, cos index); a filled red/orange circle (additive) and
  the hit circle grow to 16 × PArea while fading out in 400 ms; flame
  particles; sound Fireloop (1, −100 × index cents, rate 2, 200 ms / 1).
