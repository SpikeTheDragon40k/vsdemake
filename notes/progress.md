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
  stage-events, pickups, levelup, weapons (base + Whip + Magic Wand only;
  the other implemented weapons follow the original code directly and have
  no note yet), treasure, stage-map, physics.
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
- Rosary, Orologion, Vacuum, Clover, Floor Chicken (incl. its 300 ms
  invulnerability) and Little Heart pickups work. Not implemented: Revival,
  Nduja Fritta, weapon pickups, and the map's `Weapon`/`Item` script
  objects (bonus passives placed around the Forest/Library maps).
- Enemy explosions (SelfDestruct `MakeExplosion`) not implemented.
- The level-up/chest/menu UI uses our placeholder font; the original's
  nine-slice panels are not drawn.
- Xbox 360: two backends, both **never compiled or run on hardware**.
  The plan is the XDK one (`platform/xdk`, `engine/xdk/*.sln`, real
  tint/alpha shaders, Aurora/XeXMenu-launchable `default.xex`). Verified
  here: the core compiles as strict C++03 and links against `extern "C"`
  platform definitions exactly as the XDK project will; the console error
  screen renders. The libxenon one (`platform/xenon`) is the fallback for
  people without the XDK.
- A real window was never opened here (no display); headless path only.
- Balance check: scripted strafing dies in 30–70 s; a human kiting should
  do far better, but compare against the real game early.

### Next

1. Open the PC build on a machine with a display and play Mad Forest; fix
   whatever feels off against the original.
2. Xbox: first XEX build and boot following `docs/FIRST_XEX_TEST.md`;
   fix what it reports.
3. Remaining weapons, explosions, audio, save file.

## 2026-10-13 (later): documentation audit

Checked every guide against the code and the original:

- Fixed two defects that would have broken the first XEX: the platform API
  had C++ linkage in the C++-compiled core but `extern "C"` definitions in
  the XDK backend (unresolved externals; reproduced with g++ and fixed in
  `vs_platform.h`), and the 604 KB `vs_game` lived on `main`'s stack
  (now static).
- Added an on-screen error for console boot failures (pack missing,
  damaged, layout mismatch).
- Floor Chicken now grants 300 ms invulnerability as in the original.
- PC: Backspace = B; CTest marks the pack test *skipped* without a pack.
- Doc fixes: README (XDK target, key bindings, ctest), Xbox guide (pack
  lookup, mounts, FTP credentials, Aurora steps, removed a wrong artwork
  claim, new build-error rows), pack format (header sizes, PlayerWall flag,
  file names), depot report (atlas format, 31 OGG / 3 BGM), pickups and
  spawning notes (all "verify" items resolved).
- New: `docs/FIRST_XEX_TEST.md`.

## 2026-10-14: build VM

- Answered "can the XEX be built in a container": no Linux image is
  possible (XDK is Windows-only, NDA); the user will use a VirtualBox
  Windows VM. Added `engine/xdk/build-xex.bat` (unattended msbuild with log
  and first-error summary) and a VirtualBox section to the Xbox guide.
- Still waiting for the first real XDK build/boot report.
