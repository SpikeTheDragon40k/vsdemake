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

## 2026-10-14: audio

- Behaviour: `notes/behaviour/audio.md` (every sound event with volume,
  pitch, speed and repeat limits; music per stage and screen).
- Patcher: the 30 OGG files the game loads are packed unchanged as `SND`
  entries named by sound key (pack: 32.1 MB, 88 entries).
- Engine: `core/vs_audio.c` mixer (32 voices, volume, pitch/speed with
  linear resampling, repeat limits, music streaming with loop and fades),
  stb_vorbis decoder vendored; short sounds decoded at start-up, the stage
  tracks while playing. Push model, no threads: SDL2 queue on PC, XAudio2
  source voice on the XDK build. Sound and music volumes in the save.
- Hooked: weapons, enemy and player hits, pickups, chests, level-up (with
  music fades), menus, stage music, game-over jingle, start-up jingle.
- Checks: `--audio-dump` WAV of headless runs (no clipping, music loops
  without a gap, Library track stereo), gameplay hash identical with and
  without sound, ASan clean, C++03 clean. Not yet heard on the console.
- Not done: volume options screen; explosions/alert/Pentagram/Nduja sounds
  (those features don't exist yet); treasure opening animation (the chest
  sound plays at once).

### Console silence (same day)

- Symptom on the Xbox: sounds decoded, XAudio2 consuming buffers, but mix
  level 0. Ruled out byte order: the decoder and mixer built for big-endian
  PowerPC (cross gcc + qemu-ppc) give the same output as on x86.
- Cause: `vs_save_load` cleared the save a second time when no save existed,
  setting both volumes to 0, and the new save was written that way, so every
  sound was refused ("volume 0"). PC tests ran without a save file. Fixed;
  saves holding 0/0 (no options screen exists yet) get the defaults.
- Title diagnostics: Y = LevelUp sound, X = plain 440 Hz tone, B = swap the
  byte order of the XAudio2 output; a line with volumes, voices, play
  requests (and why the last one was refused) and the LevelUp sound's data.

## 2026-10-14: Lightning Ring drawing

- Reported from the video: the bolt was drawn across the whole screen and
  not where it hit. The 480 × 17 bolt image was scaled uniformly by
  path length / 17, so it became ~16 times too wide, and the invisible
  bullet sprite (HitRed2) was drawn too. Now drawn as the original's trail:
  a 17 px strip from the start point to the strike, plus the small
  coloured impact circle; the bullet itself stays invisible. Behaviour in
  `notes/behaviour/weapons.md`. New `vs_render_stretched` (separate width
  and height). Gameplay unchanged (rendering only).

## 2026-10-16: pause and options

- Start pauses a run: the original's pause screen (`notes/behaviour/options.md`)
  with Sounds/Music sliders, Flashing VFX and Damage Numbers switches,
  Quit (results screen; the run is recorded and achievements checked) and
  Resume. Title menu: START / OPTIONS (same screen with Back).
- The sound test moved from the title to the Options screen (row "Sound
  test"; diagnostics shown while it is selected). The title keeps one line
  of sound status.
- Save: `flashing_vfx`, `damage_numbers`; one `save_defaults()` for every
  fresh-save path. The 0/0 volume repair now only applies to saves written
  before these keys existed, so muting both on purpose is kept.
- Scripted input gains B/X/Y and S (Start).
- Checks: pause → change → resume → quit flow, options from the title,
  values in the save; gameplay hash unchanged without pausing; ASan/UBSan
  clean; C++03 clean.

## 2026-10-16: Peachone, Ebony Wings, Vandalier

- The three bird weapons (`notes/behaviour/weapons.md`): bird following the
  player, circling damage zone(s) with the lagging target circle, firing /
  resting cycle, volleys of curved bolts landing on random points of the
  zone, burst hit, trails (gradient / prism), sounds.
- Vandalier evolves from a chest with Peachone 8 + Ebony Wings 8 (table
  driven, removes both). Exdash (starts with Ebony Wings) is now listed once
  unlocked; the birds join the level-up pool once unlocked.
- Checks: video of Peachone + Ebony 8 and of Vandalier; gameplay hash of a
  bird-less run unchanged; ASan/UBSan clean; C++03 clean.

## 2026-10-16: chest animation and map bonus items

- Treasure chest screen (`engine/src/game/treasure.c`, `treasure.md`):
  OPEN, chest animation, reels of item icons, coins, ribbons, coin counter,
  prize icons with stars, DONE and the prize panels; level timelines, quick
  skip (B) for experienced players, ThingFound / Treasure sounds (the skip
  seeks the chest sound to its ending: new `vs_audio_seek_last`), music
  fades. Prizes are decided at pickup and given when the screen closes.
- Map bonus items (`stage-map.md`): the maps' `Weapon` / `Item` script
  objects are placed at the start (chance × luck, separate random stream);
  WEAPON pickups float, glow and sparkle; taking one opens the "Weapon
  Found!" screen (take / leave).
- The level-up panel is a shared function now (`vs_ui_weapon_panel`).
- Dev options: `--chest [LEVEL]`, `--show-chest`, `--map-item NAME`.
- Checks: chest screens of all levels and the item-found screen recorded;
  headless runs (screens skipped) keep their state hashes; ASan/UBSan
  clean; C++03 clean.

## 2026-10-16: shop

- PowerUp selection screen (`engine/src/game/shop.c`, `notes/behaviour/shop.md`):
  14 power-ups with ranks, the original's prices (rank and global markup),
  buy, refund (gold = lifetime gold); ranks applied at run start.
- Characters must be bought at the character select (price × markup,
  paid from gold and lifetime gold). Note for existing saves: characters
  unlocked earlier now need buying once.
- Save keys `powerup.<id>` and `bought_character`. Title menu gains POWER UP.
- Checks: prices against the formula (720 then 1560 for Armor with 2 Might
  ranks), refund, character purchase (10, then 110 / 550 with the markup),
  stats applied at run start (Might 1.25, Armor 3, Max HP 133, magnet 47);
  hash of runs without a save unchanged; ASan/UBSan clean; C++03 clean.

## 2026-10-16: Bone, Clock Lancet, Laurel, Pentagram

- The last four weapons of 0.2.10 (`notes/behaviour/weapons.md`): Bone
  (bouncing), Clock Lancet (turning freeze line; new per-enemy freeze),
  Laurel (shield charges with the coloured ring), Pentagram (screen wipe:
  erase or kill, shattering picture, flash, PentagramSFX).
- Every weapon of the version is now in the game; Mortaccio (Bone) can be
  listed once unlocked.
- Checks: recorded all four (and a Pentagram wipe at 1:30 with a crowd:
  enemies and gems gone, kills unchanged); bird-less/weapon-less hash
  unchanged; ASan/UBSan clean (including a long unlock-all auto run);
  C++03 clean.
