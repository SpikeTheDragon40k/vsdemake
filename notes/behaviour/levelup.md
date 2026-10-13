# Level-up

Lines refer to `work/main.deob.js`. Level-up factory 1423–1615; level-up
scene 7483–7614; weapon add/level in Core 6573–6662.

## XP curve (line 1428)

```
f(L) = min(5 + 1.5 × floor(L / 20), 8)
XpRequired(L)      = f(L)     × L × L            # cumulative XP to reach level L+1
PreviousXpRequired = f(L - 1) × (L-1) × (L-1)
```

`xp` is cumulative and never reset. Level up when `xp >= XpRequired`
(checked every 4th frame, `loop.md`), one level per check; the level-up
screen is shown and the game paused; then `CalculateXPfactor` recomputes
both values for the new level. The XP bar shows
`(xp − Previous) / (XpRequired − Previous)`.

With `L` the current level: L=1 → 5 XP, L=2 → 20, L=10 → 500, L=20 → 2600
(f = 6.5), L=40 → 12800 (f = 8).

## Store setup (`Init`, line 1454)

- `WeaponStore`: for every entry of the weapons table (weapons *and*
  passives), push the key once per level entry (an item with 8 level entries
  appears 8 times). This is a multiset of remaining level-ups.
- `ExcludedWeapons`: evolutions; locked items (`isUnlocked` false); items
  with `rarity 0` or `isSpecialOnly`.
- `SpecialWeapons`: evolutions that became available (see below).
- `calculateWeights()`.

`RemoveFromStore(type)` (line 1579): remove one occurrence of `type` from
`WeaponStore`; if none remain → add to `ExcludedWeapons`; recalc weights.
Called when a weapon is added or levelled through the level-up flow.

## Weights (`calculateWeights`, line 1535)

Walk the weapons table in order; skip excluded and special; skip a passive
the player doesn't own when they already own `maxPowerUpWeapons = 6`
passives; skip a weapon the player doesn't own when they already own
`maxTrueWeapons = 6` weapons. For the rest, `acc += rarity`; items with
`rarity > 0` are stored with their cumulative weight. A pick is
`r = random() × acc` → first entry with `weight >= r`.

Two variants with the same cumulative scheme:
`GetRandomWeightedWeapon` (weapons only, not excluded, no slot check),
`GetRandomWeightedWeaponOrPowerup` (the store above).

## Number of options (line 1442)

`options = 3`, plus 1 when `random() > 1 / luck` (so luck 1 → always 3;
luck 1.3 → 23 % chance of 4). Max 4 panels.

## Choosing the options (`GetLevelUpPowerups`, line 1564)

```
list = []
e = GetRandomExistingWeapon(); if e: push
e = GetRandomExistingWeapon(); if e and not in list: push
n = options count
tries = 0
while list.length < n and tries < 1000:
    tries++
    pick = player.level <= 3 ? GetRandomWeightedWeapon() : GetRandomWeightedWeaponOrPowerup()
    if pick defined and not in list: push
return list reversed
```

(The UI pops from the end, so the first pushed item is shown first.)
If the store is empty (`HasPowerupsInStore` false) the screen offers items
instead: Big Coin Bag (25 gold) and Floor Chicken (line 1575).

`GetRandomExistingWeapon` (line 1583): with probability
`ChanceForExistingPowerUp` (below), consider the owned item types; loop:
pick the *first* owned item with probability 0.5, else a uniformly random
owned one; let `lv` be its level; if `2.5 × (lv + 1) > player.level` it is
too high-level for now → drop it from consideration; else if it is still in
`WeaponStore` return it; else drop it. Return null when nothing qualifies.

`ChanceForExistingPowerUp` (line 1446):
`(level even ? 2 : 1) × 0.3 + 1 − 1/luck`. At luck 1: 0.3 on odd levels,
0.6 on even levels.

## Applying a choice (`Core.LevelWeaponUp`, line 6573)

If the player owns the item: `weapon.LevelUp()`; if that returns false (no
next level) the player gets 10 coins instead. Then `RemoveFromStore`.
If not owned: `AddWeapon` — level 1 object, push to `Weapons`,
`RemoveFromStore`, UI icon. Either way the item is marked "seen" in the
collection save.

`weapon.LevelUp()` (line 5383): take table entry `[level]` (0-based, so the
first level-up applies entry 1); add each field to the weapon; restart the
firing timer; `level++`; fire immediately (non-passives); if the entry has
`addEvolvedWeapon`, `AddLateWeapon(evolution)`: the evolution joins
`SpecialWeapons`, is removed from `ExcludedWeapons`, weights recomputed. It
is offered only through treasure chests (`treasure.md`, `PullEvolution`,
line 1510: candidates are special evolutions whose `requires` items are all
owned at level ≥ `requiresLevel`; weighted by rarity).

## Debug/edge

- A character's `showcase` list pre-levels those weapons at start (debug).
- `maxTrueWeapons`/`maxPowerUpWeapons` are 6/6; the player's starting weapon
  counts as one.
