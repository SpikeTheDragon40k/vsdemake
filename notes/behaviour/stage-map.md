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

Enemy and bullet pools collide with the three collision layers of all four
copies; enemies with `passThroughWalls` do not.
