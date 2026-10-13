# Physics (our choices where the original relies on Phaser Arcade)

The original uses Phaser 3.55 Arcade physics. Its exact separation
algorithm is framework behaviour, not game code; the engine reproduces the
observable effects listed here. Written as decisions so they aren't
relitigated.

- **Bodies are circles.** Player r = 9 (offset 6,10 in the sprite);
  enemies r = 0.4 × sprite width (or the override); pickups r = 10 (gems),
  r = 16 (chests); magnet r = 30 × bonuses; bullets per weapon.
- **Velocities are px/s, integrated per 60 Hz tick** (`x += vx × dt`).
  Arcade integrates with the frame delta; at a locked 60 Hz the two match.
- **Enemy–enemy collider** (enemies without `passThroughWalls`): when two
  circles overlap, each is moved half the overlap along the centre line. No
  velocity response (enemies set their velocity every frame anyway).
- **Player–enemy collider**: the player is immovable, so an overlapping enemy
  is moved fully out of the player's circle; the contact calls
  `player.GetDamaged(enemy.power)` (live, non-time-stopped enemies only).
  Several enemies per tick resolve in array order.
- **Overlaps** (bullet–enemy, player–pickup, magnet–pickup) trigger callbacks
  without separation.
- **Tile collision** (Walls/PlayerWall/Obstacle, tiles with `collides`): axis
  separation against solid tiles, player and non-pass-through enemies; bullets
  with `hitsWalls` call `OnHasHitWall`. (Implemented with the tilemap.)
- **Order per tick**: input → player move → culling (10 enemies) → bullets →
  weapons → player → enemies move → separation (player, enemy–enemy) →
  overlaps → timers. Separation after movement means the enemies' drawn
  positions never overlap the player, as in the original.
- **Random**: one xoshiro128** stream seeded per run, consumed in the same
  order as `Math.random()` calls in the original where the notes list them.
  Replay = seed + per-tick input.
