# Treasure chests

Lines refer to `work/main.deob.js`. Chest pickup 786–860; treasure scene
9385–10250 (prize logic 9536–9596); level roll in `spawning.md`
(`SetTreasureLevelFromChance`, line 5189); pulls in the level-up factory
1483–1534.

## Drop

A minute row's `treasure: {chances:[c3,c2,c1], level, prizeTypes[5],
fixedPrizes[5]?}` is attached to the first boss spawned that minute if the
level roll returns > 0 (`spawning.md`). When the boss dies the chest pickup
is created at its position carrying `level`, `prizeTypes` and
`fixedPrizes` (default `[null×5]`) (line 6599).

Chest pickup: body circle radius 16, offset (−8, −8); `goToPlayer` from
spawn (it homes to the player like a vacuumed gem); on tileset stages its y
is clamped to `[minTreasureY, maxTreasureY]`. An arrow cursor points at it
from the screen edge (0.45 × screen from the player) when off-screen, or
hovers 24 px above it when on-screen.

Taken (line 820): stat counters `STATS_TREASURE_<level>.pickedupAmount++`,
copy level/types/fixed to Core, open the treasure scene (game paused).

## Prizes (`MakePrize`, line 9563)

| Level | Coins | Prize slots |
| --- | --- | --- |
| 1 | `floor(50 + 50·random)` | `prizeTypes[0]` |
| 2 | `floor(100 + 100·random)` | `prizeTypes[0..2]` |
| 3 | `floor(200 + 100·random)` | `prizeTypes[0..4]` |

Each slot is resolved in order by `MakePrizePairFromAvailablePowerUps(type,
fixed)` (line 9536):

| prizeType | Pull | Fallback |
| --- | --- | --- |
| RANDOM | first becomes POWERUP or EXISTING_ANY (50/50) | |
| POWERUP | `PullRemainingPowerUp`: a *passive* not excluded, weighted by rarity | FILLER |
| EXISTING_WEAPON | `PullExistingRemainingWeapon(false)`: an owned, non-excluded, non-passive item, weighted by rarity | FILLER |
| EXISTING_ANY | `PullExistingRemainingWeapon(true)`: any owned non-excluded item, weighted by rarity | FILLER |
| EVOLUTION | `PullEvolution` (below) | EXISTING_ANY, then FILLER |
| FIXED | `PullExisting(fixed)`: the named item if it is in the weighted store; else if it is an evolution currently in `SpecialWeapons`, that evolution | EXISTING_ANY, then FILLER |
| FILLER | Big Coin Bag: +25 coins (table value of COINBAG2) | |

"Excluded" and "weighted store" are the level-up factory's lists
(`levelup.md`). A non-filler prize is appended to the accumulated weapon
prizes and `RemoveFromStore(item)` runs immediately, so the same item can
be pulled again in a later slot only if it still has levels left.

`PullEvolution` (line 1510): among `SpecialWeapons` entries that are
evolutions with rarity > 0 and whose `requires` items are all owned at level
≥ `requiresLevel` (0 if unset), pick one weighted by rarity; remove it from
`SpecialWeapons`; recompute weights. (An evolution enters `SpecialWeapons`
when its base weapon reaches the level whose table entry has
`addEvolvedWeapon`, i.e. max level — `levelup.md`.)

## Awarding (`OnSceneOver`, line 9553)

After the animation: for each accumulated prize `Core.LevelWeaponUp(item,
false)` (adds the item or levels it; the `false` means the store isn't
touched again); item prizes are spawned as pickups at the player (not used
by the table data in 0.2.10); `AddCoins(CoinsAward + 25 × fillers)` (greed
and GoldMultiplier apply, `pickups.md`); player invulnerable 1000 ms;
resume.

## Presentation (for the demake's own UI)

Chest open animation, 2/6/… ribbons, coin particle bursts (level 1: 1 reel;
levels 2–3: one reel per prize), counter ticking up to the coin award. A
"quick skip" appears after 900 ms once the player has opened ≥ 5 level-1
chests, ≥ 1 level-3 chest, or ≥ 50 chests in total (line 9589). Timings:
level 1 reels stop after 6300 ms, prizes shown, OK button 6000 ms later.
