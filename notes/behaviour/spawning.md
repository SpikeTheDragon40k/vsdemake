# Spawning

Lines refer to `work/main.deob.js`. Stage class 5040–5289.

## Stage defaults (constructor, line 5043)

| Field | Default | Overridden by |
| --- | --- | --- |
| frequency | 1000 ms | minute rows |
| minimum | 0 | minute rows |
| maximum | **300** | (never in data) |
| minimumMultiplier | 1 | (hyper `EnemyMinimumMul` — see below) |
| startingSpawns | 0 | row 0 / hyper |
| destructibleFreq | 10000 | row 0 |
| destructibleChance / Max | 5 / 50 | row 0 |
| maxDestructibles | 10 | row 0 |
| maxTreasureLuck | 20 | |
| pause | 0 | rows (delays timer start) |
| compressTime, randomMinutes | false | rows |

Note: `minimumMultiplier` is a stage field but the `hyper.EnemyMinimumMul`
value is only used when the hyper flag… it is **not** read in `Init`; only
`hyper.StartingSpawns`, speeds, gold, health and `LuckBonus` are. Treat
`EnemyMinimumMul` as unused in 0.2.10 unless found elsewhere.

## Init (line 5045)

1. `minute = 0`, take row 0. BGM: stream-safe option → `BGM`, else
   `legacyBGM` if present else `BGM`.
2. `hasTileset = !!row.tileset`; `spawnType = row.spawnType`.
3. `UpdateAllData(row)` (below) — this starts the spawn timers and plays
   minute-0 events.
4. Spawn rectangles, relative to the player (W,H = render size):
   `rectOuter = (−W/2 − 150, −H/2 − 150, W + 200, H + 200)`,
   `rectInner = (−W/2 − 75,  −H/2 − 75,  W + 100, H + 100)`.
   These are asymmetric as written (right/bottom margins are 50 and 25 px,
   left/top 150 and 75). Reproduce literally.
5. Global constants from the stage: normal → `X = baseX × mods.X` for
   PlayerPxSpeed, EnemySpeed, ProjectileSpeed, GoldMultiplier,
   EnemyHealthMultiplier(`mods.EnemyHealth`). Hyper → same with `hyper.*`,
   plus `player.luck += hyper.LuckBonus` (if set) and
   `startingSpawns = hyper.StartingSpawns`.
6. Tileset stages: read object layers `Destructibles` and `Spawners` into
   location lists; `minTreasureY`/`maxTreasureY` = min/max spawner y.
7. `startingSpawns` times: one spawn round (`SpawnInRandomLocation` on
   tileset stages, `SpawnEnemiesHorizontal` if `spawnType == HORIZONTAL`,
   else `SpawnEnemiesInOuterRect`).

## Minute change (`CheckMinute`, line 5072, from the 1 s ticker every 60 s)

```
m = floor(SurvivedSeconds / 60)
if m > minute and a row with row.minute == m exists:
    minute = m; UpdateAllData(row)
hasAttachedTreasure = false
SpawnBoss()
```

So `SpawnBoss` runs every minute even when no row changed (all shipped
stages have a row per minute, 0..30).

`UpdateAllData(row)` (line 5076): `events = []`, `bosses = []`,
`treasure = null`; (randomMinutes/compressTime variants exist for the
"SINKING"/test stage, ignore for now); then **copy each field of the row onto
the stage** — fields absent from the row keep their previous value (so
`minimum`, `frequency`, `enemies` persist until a later row sets them, while
events/bosses/treasure do not). Then:

- `updateEnemyPools`: disable every regular and boss pool; enable (or create)
  a pool per type in `enemies` and in `bosses`.
- `updateTimers`: if `pause` → start timers after `pause` ms, else now.
  `startTimers`: replace the spawn timer with a loop of `frequency` ms whose
  callback is one spawn round (same three-way choice as Init step 7);
  replace the destructible timer with a loop of `destructibleFreq` ms.
- `playEvents`: trigger each event in `events` (`stage-events.md`).

## Spawn rounds

`permanent = Enemies.length − eventSpawned` (enemies spawned by stage
events don't count toward caps).

- **`SpawnEnemiesInOuterRect`** (line 5221): if `permanent >= maximum`
  return. For each *enabled* regular pool: a random point outside
  `rectInner` but inside `rectOuter`, offset by the player position; spawn
  one. If anything spawned → `SwarmCheck`.
- **`SwarmCheck`** (line 5269): `for (i = 0; permanent < minimum ×
  minimumMultiplier && i < maximum; i++) SpawnEnemiesInOuterRect()`. Each
  iteration adds one enemy per enabled pool, so the population is topped up
  to `minimum` immediately, in a ring just outside the screen.
- **`SpawnInRandomLocation`** (tileset stages, line 5235): same loop
  condition; per enabled pool: `x = player.x + (random < 0.5 ? rectOuter.x :
  rectOuter.x + rectOuter.width)` (left or right edge), `y` = a random
  spawner location's y (absolute map coordinate).
- **`SpawnEnemiesHorizontal`** (line 5252): like the above with
  `y = player.y`.
- **`SpawnEnemiesOutOfSight`** (line 5208, used by some callers): per pool,
  random angle, distance `0.9 × (W + rand(0..256))` horizontally and
  `0.9 × (H + rand)` vertically from the player, then `SwarmCheck`.
- `SpawnAt(x, y, type, owner)` (line 5279): cap check, then spawn from the
  matching pool; used by weapons that summon enemies.

## Bosses (`SpawnBoss`, line 5178)

For each enabled boss pool: random angle `a`, `d = rand(0..256)`,
position `player + 0.9 × (cos a × (W + d), sin a × (H + d))`; spawn; mark
`isTeleportOnCull`. If the stage has a `treasure` for this minute and none
has been attached yet this minute, roll `SetTreasureLevelFromChance`; if
> 0 attach the treasure object to the boss (dropped on death).

`SetTreasureLevelFromChance(t)` (line 5189):

```
luck = player.luck (1 if no player)
b = saved BeginnersLuck counter
if b < 6:
    level = {0:1, 1:1, 2:2, 3:1, 4:1, 5:3}[b]; b += 1 (saved)
    if level > 0: t.level = level; return level
r1, r2, r3 = three rolls in [0, 100)
if r1 <= t.chances[0] × luck: t.level = 3; return 3
if r2 <= t.chances[1] × luck: t.level = 2; return 2
if r3 <= t.chances[2] × luck: t.level = 1; return 1
return 0
```

So the first six boss treasures of a save are guaranteed (levels 1,1,2,1,1,3).

## Destructibles

Every `destructibleFreq` ms (line 5152): roll `100 × random() <=
min(destructibleChance × luck, destructibleChanceMax)`. Tileset stages pick
a random `Destructibles` location that is currently off-screen (relative to
the tiling map's current tile); other stages use
`GetPositionOutOfSight(spread)` with `spread = 45°` if the player is moving
else random direction (line 6644). Position (line 6715): with spread > 0 the
angle is the player's heading ± spread/2, else uniform; with margin 0 the
point is `player + 0.9 × (cos·W, sin·H)`; with a margin `m` it is
`player + (0.5·cos·W + sign(cos)·m, 0.5·sin·H + sign(sin)·m)`. After
spawning, while count > `maxDestructibles` the one farthest from the player
is despawned.

## Culling

See `loop.md`: 10 round-robin checks per frame against 0.7·W / 0.8·H
distance; non-cullable event enemies are skipped; bosses teleport.
