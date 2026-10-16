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

## Presentation (treasure scene, line 9385)

Scene start (`create`): background nine-slice grows from scale (−0.2,
−0.1) turned 180° to full in 150 ms; yellow `Burst1–6` (30 fps, scale 8,
additive) when flashing VFX are on; header `treasure_header`; chest
`TreasureIdle_01–08` (15 fps, played again every 3 s), origin (0.5, 1),
scale 5, at panel bottom − 110; OPEN button (`button_c9`, `treasure_open`)
at panel bottom − 64, growing in after 300 ms; ThingFound sound (0.7). The
prizes are decided now (`MakePrize`); the main scene is paused.

On OPEN (per level, ms after the press):

| | Level 1 | Level 2 | Level 3 |
| --- | --- | --- | --- |
| sound | Treasure1 (1) | Treasure2 (1) | Treasure3 (1.1) |
| chest opens | 0 | 0 | 1600 (fireworks first) |
| coin counter runs | 0–7500 | 0–11000 | 1600–15500 |
| coins emitted until | 6300 | 11000 | 16000 |
| reels start | 0 | reel 2 at 0, 0 at 80, 1 at 160 | 0–2 at 1600, 3–4 at 9500 |
| reels stop, prizes appear | 6300 | 9700 | 13700 |
| ribbons | 2 | 6 | 8 (+ a second set) |
| reel colours | blue | violet, magenta ×2 | red, orange ×2, yellow ×2 |
| background glow (`frame5_c4_ad`) | — | to 0.7 while spinning | flash, then 1 |

- Chest: `TreasureOpen_01–08` plus a front layer `TreasureOpenFront_01–08`
  (30 fps) drawn over the reels and coins.
- Reels: a 32 px strip of item icons every 32 px on a coloured background
  (alpha 0.5), shown at scale 2, conveyor-like along a line from the chest
  to (0, −1536) turned by 0, 20, −20, 40, −40°, 2 s per pass; masked to the
  panel above the chest. Contents: power-ups for POWERUP prizes, owned
  weapons for EXISTING ones, any item for EVOLUTION / FIXED; at least 12,
  padded with money bags. On stop each turns into its empty strip with a
  50 ms white flash, staggered [0], [600, 0, 300] or [600, 300, 300, 0,
  0] ms; the prize icons appear with the same delays.
- Prize icons at chest + (0, −250), (∓115, −325), (∓150, −190):
  `PrizeBG` and the item at scale 2, two `HitStarWhite2` stars that become
  `HitStar2`, pulse 2↔4 and turn.
- Coins: two back emitters (gold, scale 2 → 1) and two front ones
  (silver, 2 → 2.1) at the chest: 225–315°, 450–600 px/s, gravity 500,
  4 s, bouncing on the screen sides; one coin every 2 × [100, 50, 25] ms
  (back) / 2 × [200, 100, 50] ms (front).
- Ribbons: `Ribbon1` ropes (32 points) following a fixed spline (jittered,
  mirrored every other one, 50 % turned ±10°), there and back in 1.5 s
  (Sine.easeInOut), 100 ms apart; fade out as the reels stop.
- Counter: `CoinsAward × GoldMultiplier × greed` (rounded up) shown with two
  decimals, ticking up; at the end it turns yellow, a ring flashes and it
  pulses.
- 6000 ms after the stop: reels fade, the counter moves to the header, the
  button comes back as DONE and the prizes are shown one at a time in a
  panel at the bottom (`CyclePrizes`: icon, name, level, description; 4.5 s
  each, next every 5 s). DONE: everything shrinks away in 150 ms; 500 ms
  later `OnSceneOver` gives the prizes.
- Quick skip (levels 1 and 2, after 900 / 1800 ms, if enough chests were
  opened before): the counter finishes 1.2 / 1.3 s later, coins stop, the
  reels stop at once, the wait before DONE is 3 × shorter, and the chest
  sound restarts at 6.3 / 9.7 s (its ending).
- The music fades to 0 while the scene is open and back to 0.3 after.

Demake: same timeline and layout scaled by 0.6, the panel moved up so the
prize panel fits under it; additive effects drawn with plain alpha; level
3's fireworks and the background "bubbles" are not drawn. Headless runs
skip the screen and give the prizes at once (`--show-chest` plays it).
