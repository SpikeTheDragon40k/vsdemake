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

- Weapons missing: Peachone, Ebony Wings, Vandalier, Pentagram, Clock
  Lancet, Laurel, Bone, Nduja (evolutions of the implemented weapons are done,
  see the 2026-10-14 entry).
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

## 2026-10-14: first console run

- First XEX built with the real XDK (after fixes: `_XBOX` guard, 8.3 copy
  check, msbuild lookup, output names before `Microsoft.Cpp.props`) and
  booted on the console from the dashboard.
- Reported: sprites far from their hitboxes. Cause, on every platform: the
  patcher used `spriteSourceSize` offsets of untrimmed atlas frames, which
  Phaser ignores (1,048 enemy, 41 character, 134 vfx, 8 angel frames).
  Fixed in the patcher, with a matching guard in the engine for old packs.
  Gameplay unchanged (state hash identical); only drawing moved.
- New dev aids: `--hitboxes` overlay, `--char N` start character.
- Title screen shows "VSDEMAKE" in the original logo's gold/red style,
  built by the patcher from the user's own title.png (`logo.js`; nothing
  derived from the game is committed). New flag `--menu`.

## 2026-10-14: evolutions and passive items

- Five evolutions now have their own behaviour (`notes/behaviour/evolutions.md`):
  Bloody Tear (crits, heal on crit), Death Spiral, Hellfire, Heaven Sword
  (crits, boomerang), Soul Eater (power from healing, heart drops). Holy
  Wand, Thousand Edge and Unholy Vespers were already done.
- All 14 passive items checked against the original's passive class.
- Items locked on a fresh save (most passives, so most evolutions) are now
  offered: the demake has no save yet and behaves like the original's
  unlock-all developer switch. (Superseded by saves below.)
- Unimplemented weapons (Peachone, Ebony Wings, Vandalier, Pentagram, Clock
  Lancet, Laurel, Bone) are no longer offered; Laurel used to be offered and
  fired a plain shot.
- Dev options: `--give NAME[:LEVEL]`, `--chest`.
- Checked: chest after Whip 8 + Hollow Heart gives Bloody Tear; Whip 7 or no
  Hollow Heart gives none; ASan/UBSan runs clean; C++03 check clean.

## 2026-10-14: level-up screen

- Rebuilt like the original's: menu frame, "Level Up!", one panel per
  choice with icon, name, "New!" or "level: N", and the description. For an
  upgrade the description says what that level adds ("Base damage up by 5",
  "[All] Cooldown reduced by 8.0%"), built at pack time from the English
  strings in `lang.json` with the original's rules and key order.
- New pack entry `LNG en`, new computed field `weapons.levelUpText`: packs
  must be rebuilt (the engine refuses the old table layout).
- Still our 5x7 font; the original's own UI font is not used yet.

## 2026-10-14: saves and unlocks

- `vsdemake.sav` (text, CRC, `.bak` fallback) next to the pack, written at
  game over: gold, lifetime totals, per-type kill/pickup/destroy counters,
  achievements, unlocked items/characters/stages/hypers, seen items, last
  choices. Behaviour in `notes/behaviour/saves.md`.
- All 40 achievements checked after each run with the original's
  conditions; unlocks applied; gold prizes added; the game-over screen
  lists the new ones; title/select show gold and the achievement count.
- Fresh-save rules are the default again: level-ups offer only unlocked
  items, Inlaid Library needs "Reach LV 20 in Mad Forest", hidden
  characters appear when unlocked. `--unlock-all` replaces `--locked`.
- No shop: unlocked characters are playable without buying; gold only
  accumulates.
- Checked: unlocks change the level-up pool (fresh vs saved vs
  unlock-all), counters accumulate over runs, damaged saves fall back to the
  backup, ASan runs clean, C++03 check clean. Not checked on the console.
- New dev options: `--save PATH`, `--level N`, `--show-gameover`.
- First console test: no save appeared. Fixes: the save no longer uses
  `rename` (copy to `.bak`, then overwrite in place); the game-over timer is
  reset each run (only the first run after launch used to save); the
  game-over screen now shows the save result and path.
- Second console report: still no save, and no way to tell why. The title
  screen now shows the save path and its state (loaded / new save created /
  restored from backup / SAVE FAILED with the C library's error reason and
  errno). A missing save is created immediately at start-up, so a folder
  that can't be written shows on the title, without playing a run.
- Console confirmed: saving works (title shows the save created/loaded).

## Next: audio

Planned in `notes/audio-plan.md` (facts, steps, session estimate, how to
resume in a fresh session).
