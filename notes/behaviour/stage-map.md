# Stage map and background

Lines refer to `work/main.deob.js`. Tiling tileset 4922–5039, background
manager 6235–6257.

## Background (`BGMan`)

A screen-sized tile sprite of `BGTextureName` (`bg_forest`, `bg_green`;
1024×1024 PNGs) fixed to the camera, drawn behind everything. Each frame
(when scrolling is enabled) the tile offset is `(player.x, player.y) ×
RPixelScale`, so the texture scrolls 1:1 with the world.

Day/night (`dayNight` stages), evaluated every 4th frame: `cycle = 900 s`;
`t = (SurvivedSeconds + offset) mod 900`; even half-cycles go
`t/900` from day to night, odd ones back; tint = lerp(0xffffff → 0x0444ee).
Erase-enemies turns the cycle off and tints dark red (`stage-events.md`).

## Tiled map (stages with `tileset`)

Both shipped maps are 64×64 tiles of 32×32 px (2048×2048 px), orthogonal,
`right-down`, tileset from `<Name>TexturePacked.png` (512×512, 15 columns,
margin 1, spacing 2, 225 tiles, `firstgid 1`). Some tiles have the boolean
property `collides`.

Layers in draw order with the depth the original assigns:

| Layer | Depth | Collision |
| --- | --- | --- |
| Floor | −2000 | |
| FloorOverlay | −1999 | |
| FakeWalls | −1998 | |
| Walls | −1997 | player, enemies, bullets (`hitsWalls`) |
| PlayerWall | −1997 | player, bullets |
| Obstacle | −1996 | player, enemies, bullets |
| Decals | −1995 | |
| Overlay1 | +1 (above actors at the same y) | |
| Shadows | +2000 (above everything) | |

Collision is per tile via the `collides` tile property (tile-rect collision,
Arcade style: bodies are pushed out, no sliding tricks beyond the engine's
separation).

Object groups:
- `Scripts`: `PlayerStart` (spawn point; Forest (1575, 1099)); `Weapon`
  objects with `type` = a passive id (POWER, LUCK, GREED, COOLDOWN) and
  properties `chance`, `xMultiplier`, `yMultiplier`; `Item` objects
  (`type` GEM with a `value` property); `tilep` (a debug object that
  flattens the map, ignore).
- `Destructibles`: point objects, candidate positions for destructibles
  (Library: 29; Forest: none).
- `Spawners`: point objects, candidate enemy spawn rows (Library: 15, all at
  x 64, various y; Forest: none).

### Infinite tiling

The map is instantiated **4 times**; each copy is a 2048 px square placed on
a 2×2 grid around the player (`_SIZE = 2048`). Every frame (`update`, line
5025): find the copy containing the player → `current`; the other three are
placed left/right (by which half of `current` the player is in) and
above/below, so the player always has map copies on all sides. Object
positions in `Spawners`/`Destructibles` are offset by `current`'s origin
when used. Player start = `PlayerStart` of copy 0.

Weapon/Item script objects spawn once at start, at
`(x + 2048 × xMultiplier, y + 2048 × yMultiplier)` if `random() <= chance ×
luck` (lines 4967, 5010). The `xMultiplier` values of ±5..10 place them
several map-copies away, i.e. far from the start.

- The object's `type` is the item's id string: for `Weapon` an entry of
  the weapons enum by value (`POWER` is Might's id), for `Item` a pickup
  (`GEM` makes a gem whose experience is the `value` property). No
  `chance` property → always placed.
- Mad Forest: 9 Might/Luck/Max Health/Recovery items, chances 0.1–0.5;
  Inlaid Library: Greed (always), 4 × Cooldown (0.2–0.5) and a 5-xp gem.
- `Weapon` objects become WEAPON pickups (line 1274): stationary (the
  magnet does not pull them), floating 8 px up and down (1 s, Sine), a
  shadow spot breathing 1 ↔ 0.75, a glow (`round.png`, additive, alpha
  0.5 ↔ 0 every 500 ms) whose tint cycles (r = |sin c|, g = |cos c/2|,
  b = 200 + 55 |cos c|, c += 0.1 per frame), 10 sparkles every 200 ms.
- Taken: the item-found scene (line 11333): the game pauses, music fades
  to 0.2, LevelUp sound (1.25, +1000 cents), yellow burst, header
  `newItem_header`, the item's level-up panel sliding in, buttons
  `newItem_get` / `newItem_discard` (usable after 500 ms), confetti rising
  from the bottom. Get: `LevelWeaponUp(item)`, weights recalculated,
  ClickIn. Discard: ClickOut. Either way the item is gone and the music
  goes back to 0.3.

Enemy and bullet pools collide with the three collision layers of all four
copies; enemies with `passThroughWalls` do not.
