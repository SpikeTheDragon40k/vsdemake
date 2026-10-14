# Saves, achievements and unlocks

Lines refer to `work/main.deob.js`: player options (save) class 2776–3008,
achievement checks 2711–2725, results screen 10271 and 10412, game-over
save 8657.

## What the original saves (`PlayerOptions`)

Key/value storage (Electron), each value JSON. Kept per player:

- Selected character, stage, hyper flag.
- `Coins` (spendable) and `LifetimeCoins`; `AddCoins(v)` adds
  `v × GoldMultiplier × player.greed` to both and to `RunCoins`, and saves
  the coins at once. `AddCoinsFlat(v)` adds `v` without multipliers
  (achievement prizes).
- Lifetime totals: `LifetimeSurvived` (seconds; += survived seconds when the
  results screen opens), `LifetimeHeal` (every recovered HP, only amounts
  below 9000).
- Per-type lifetime counters, written when the run ends (game over):
  enemies `killedAmount`, pickups `pickedupAmount`, destructibles
  `destroyedAmount`.
- Lists: `UnlockedWeapons`, `UnlockedCharacters`, `UnlockedStages`,
  `UnlockedHypers`, `Achievements`, `CollectedWeapons` (items ever
  obtained, "seen"), `CollectedItems`, `BoughtCharacters`,
  `BoughtPowerups` (shop, not in the demake yet).
- Options: sound/music volume, flashing VFX, damage numbers, language…

On load (`apply`): the lists flip the table flags (weapon `isUnlocked`,
character `hidden = false`, stage `unlocked`, hyper `unlocked`, achievement
`achieved`, item `seen`); Mad Forest is always unlocked; then every
achieved achievement **without a gold prize** re-applies its unlock
(`RestoreAchievementUnlocks`).

## Unlocking an achievement (`UnlockAchievement`)

If not achieved yet: mark achieved; unlock its `weaponToUnlock`,
`characterToUnlock`, `stageToUnlock`, `hyperToUnlock` (each also pushed to
the matching list); add `goldPrize` with `AddCoinsFlat`; record it as new
for this run.

## When the checks run

Once per run, on the results screen after game over
(`GetNewAchievementsEarnedOnThisRun`). The screen then shows each new
achievement in a popup (cycling every 3.5 s when there are several) and the
count of new unlocks.

## The checks (lifetime counters include this run)

| Achievement | Condition |
| --- | --- |
| Defeat5000Enemies | sum of all enemies' kill counters ≥ 5000 |
| Defeat3000Skeletons | SKELETON kills ≥ 3000 |
| DefeatBossMadForest | BOSS_XLFLOWER kills ≥ 1 and the selected stage is Mad Forest |
| DefeatBossLibrary | BOSS_COUNT1 kills ≥ 1 and the selected stage is Inlaid Library |
| Destroy20Candles | sum of destroyed counters ≥ 20 |
| Eat5PotRoast | ROAST picked up ≥ 5 |
| FindAClover / FindARosary / FindAVacuum / FindAnOrologion | that pickup picked up ≥ 1 |
| FindAStoneMask | Stone Mask (GREED) ever obtained |
| GetFireWandToLevel4 | Fire Wand level ≥ 4, or Hellfire owned (at run end) |
| GetHolyBookToLevel4 | King Bible ≥ 4, or Unholy Vespers |
| GetHolyWaterToLevel4 | Santa Water ≥ 4 |
| GetMagicMissileToLevel7 | Magic Wand ≥ 7, or Holy Wand |
| GetLightningRingToLevel4 | Lightning Ring ≥ 4 |
| GetPeachoneToLevel7 | Peachone ≥ 7, or Vandalier |
| GetRunetracerToLevel7 | Runetracer ≥ 7 |
| GetGarlicToLevel7 | Garlic ≥ 7, or Soul Eater |
| Have6DifferentWeapons | 6 or more items (weapons and passives) owned at run end |
| Recover1000HP | LifetimeHeal ≥ 1000 |
| Earn5000CoinsInASingleRun | RunCoins ≥ 5000 |
| ReachLV20InMadForest | level ≥ 20 and the stage is Mad Forest |
| ReachLV10 / ReachLV5 | level ≥ 10 / ≥ 5 |
| Survive1Minute / Survive10Minutes / Survive20Minutes | survived ≥ 60 / 600 / 1200 s |
| Survive5MinutesWithDiamond / …WithKnife | ≥ 300 s as Pasqualina / Gennaro |
| EvolveWhip … EvolveGarlic, UniteBirds | that evolution owned at run end |
| Unlock2Hypers | 2 or more hypers unlocked |

Two extra unlocks without achievements: with probability
`(clover pickups) / 65535` the secret character EXDASH becomes visible and
bought; killing BOSS_XLDEATH makes NOSTRO visible.

What each achievement unlocks is data (`achievements` table).

## Selection rules in the original

- Characters: listed unless `hidden`; playable once bought (shop).
- Stages: selectable when `unlocked`.
- Level-up/treasure: items with `isUnlocked` only (`levelup.md`).

## Demake choices (not in the original)

- Saved as a text file (`key=value` lines, enum names, CRC32 line) next to
  the pack: `vsdemake.sav`, previous copy `vsdemake.sav.bak`.
- Saved when a run ends (game over) and after achievements; gold is not
  saved on every pickup.
- No shop yet: a visible (unlocked) character is playable without buying.
  Characters whose starting weapon the demake lacks are not listed.
- Hypers and Green Acres unlock and are saved but cannot be played yet.
