# Progress log

One dated entry per session. Newest first.

## 2026-10-13 (cloud session, Fable 5.1)

Setting: the original local checkout (`/home/spike/dev/vsdemake` with
`notes/depot-report.md`, `work/tools/deob.mjs`, `probe.mjs`) was not
available. The user uploaded `main.bundle.js` and `assets/` from the depot;
they live in `vs-1.0/resources/app/.webpack/renderer/` here (read-only,
git-ignored). Recon was redone and written to `notes/depot-report.md`.

### Done

- **Phase 0–1**: repo, `.gitignore`, hash list, AST-driven de-obfuscator,
  static table extraction, JSON Schemas, `vspatch verify/deob/extract`,
  golden tests.
- **Phase 2**: `notes/behaviour/` loop, player, enemies, spawning,
  stage-events, pickups, levelup, weapons (base + Whip + Magic Wand + notes
  for 7 more in code comments), treasure, stage-map, physics.
- **Phase 3–4**: `notes/pack-format.md`; `vspatch build` → 27 MB pack with
  textures, atlases, maps, font, enums and tables; C headers generated from
  `patcher/src/tables.js`; SDL2 engine with pack reader, platform layer,
  headless mode (`--headless --no-render --frames N --input "R240 L240"
  --seed N --stage library --screenshot x.bmp`).
- **Phase 5 (MVP state)**: player stats/damage/regen; enemies (chase,
  knockback, skills, death, culling, teleport-on-cull bosses); stage minute
  rows, spawn shapes, swarm check, bosses with treasure rolls, all 9 stage
  events; weapons Whip, Magic Wand, Knife, Axe, Fire Wand, King Bible, Cross,
  Garlic, Santa Water, Lightning Ring, Runetracer; passives (generic); gems with cap/merge, magnet, XP;
  level-up factory and screen; chest prizes (pulls per treasure.md, no
  animation); destructibles + loot weights; tilemap with 4 scrolling copies
  and tile collision; damage numbers (BMFont digits) and hit flashes; HUD;
  title / character+stage select / game-over screens with the built-in
  5x7 font. Both Mad Forest and Inlaid Library run headless.

### Not done / known gaps

- Weapons missing: Pentagram, Bone, Lancet, Clock Lancet, Song of Mana,
  Laurel/Thorns shield, Nduja and all evolutions' special behaviour (evolution objects
  load from the table but use the base weapon's bullet; Holy Wand/Vespers
  share their base weapon's code which is correct for those two).
- Audio: not in the pack (needs a permissively licensed Vorbis decoder for
  Node) and no mixer in the engine.
- Save data / unlocks / shop / achievements: none (every bought-or-cheap
  character is selectable; Library selectable without unlock).
- Revive, orologion/rosary pickups exist in code; Nduja Fritta and weapon
  pickups do nothing yet.
- Enemy explosions (SelfDestruct `MakeExplosion`) not implemented.
- The level-up/chest/menu UI uses our placeholder font; the original's
  nine-slice panels are not drawn.
- Xbox 360 backend: written but **never compiled or run**
  (`engine/src/platform/xenon`, `engine/xenon/Makefile`,
  `notes/toolchain.md`). It passes a host syntax check against the real
  libxenon headers. Uses the cube example's shaders: brightness-only tint,
  no per-sprite alpha, until a 2D shader is compiled for Xenos.
- A real window was never opened here (no display); headless path only.
- Balance check: scripted strafing dies in 30–70 s; a human kiting should
  do far better, but compare against the real game early.

### Next

1. Open the PC build on a machine with a display and play Mad Forest; fix
   whatever feels off against the original.
2. Xbox: install the free60 toolchain, write the xenon platform file
   (Xenos sprite batch, pad input, USB file I/O), build the ELF, test on
   hardware; then XEX packaging.
3. Remaining weapons, explosions, audio, save file.
