# Working guide: building the demake and the patcher

This is the step-by-step plan for the agent (Fable 5.1) that picks up after the
depot reconnaissance. Read `notes/depot-report.md` first. It is the source of
truth for file locations, data shapes and line numbers, and this guide refers
to it instead of repeating it.

Work through the phases in order. Each phase ends with a checklist. Don't start
the next phase until every box in the current one is true, and record what you
did in `notes/progress.md` (one dated entry per session).

---

## Ground rules (unchanged from the recon, still binding)

1. **`vs-1.0/` is read-only.** Never write into it. Extracted or generated
   material goes in `work/` (git-ignored).
2. **Never commit game files or derived game data.** That includes extracted
   JSON, converted textures, transcoded audio and `work/main.deob.js`. The repo
   holds only our engine, our patcher, our tests and our notes. Check
   `git status` before every commit.
3. **Clean room.** The engine is written from the behaviour descriptions in
   `notes/`, not by translating the original source. Notes may hold formulas,
   numbers, data shapes, names and file:line references, but **no pasted blocks
   of original code**. When a behaviour is unclear, write the description into
   `notes/behaviour/<system>.md` first, then implement from that note.
4. **Static analysis only.** Don't run the original game. Don't `eval` or
   `require` any part of the bundle, including in the patcher.
5. **Ask before anything outward-facing:** creating the GitHub repo, pushing,
   publishing releases.
6. If something blocks you, write it in `notes/progress.md` and move on to the
   next task you can do.

## Reference facts

| Item | Value |
| --- | --- |
| Steam app / depot / manifest | 1794680 / 1794681 / **8292959317163221061** |
| Game version | 0.2.10 (Electron 15.3.0, Phaser 3.55.2) |
| Game code + data | `resources/app/.webpack/renderer/main.bundle.js` (string-array obfuscated) |
| Assets | `resources/app/.webpack/renderer/assets/` (PNG + TexturePacker JSON, Tiled JSON, BMFont, OGG) |
| Existing tools | `work/tools/deob.mjs` (de-obfuscator), `work/tools/probe.mjs` (table extraction probe) |
| Content | 3 playable stages (Mad Forest, Inlaid Library, Green Acres), 27 weapons (9 evolutions), 14 passives, about 13 real characters, 53 enemy types |

## Target architecture

```
vsdemake/
├── patcher/            Node.js CLI: user's game files → our data pack
│   ├── src/locate.js        find the install, verify hashes
│   ├── src/deobfuscate.js   static string-array resolver (from work/tools/deob.mjs)
│   ├── src/extract.js       tables → JSON (from work/tools/probe.mjs)
│   ├── src/assets/          atlas, texture, tilemap, font, audio converters
│   ├── src/pack.js          writes the data pack
│   └── test/                schema + golden-count tests (no game data committed)
├── engine/             C (C99) game, our own code
│   ├── src/core/            loop, ECS-lite pools, RNG, math
│   ├── src/game/            player, enemies, weapons, pickups, stage, level-up
│   ├── src/platform/sdl2/   PC backend (dev + tests)
│   └── src/platform/xenon/  libxenon backend
├── schema/             JSON Schemas for the data pack (ours, committed)
├── notes/              clean-room documentation
├── work/               git-ignored scratch
└── README.md
```

Design decisions, with the reasoning, so you don't relitigate them:

- **Patcher in Node.js.** The de-obfuscator and probe already use acorn, and
  the input is JavaScript. Users run it on a PC, never on the console.
- **Engine in C99 with a platform layer.** libxenon is C. A second SDL2
  backend lets you develop, debug and run determinism tests on Linux without
  hardware, so most of the work never touches the Xbox.
- **The data pack is the only interface** between patcher and engine. The
  engine never parses game JSON or PNG at runtime on the console. It loads a
  pre-baked, big-endian binary pack produced by the patcher.

---

## Phase 0: Repository and provenance

1. `git init` in `/home/spike/dev/vsdemake`. Confirm `.gitignore` has
   `vs-1.0/`, `work/`, `*.asar`, and add `build/`, `out/`, `node_modules/` and
   `*.vspack`.
2. Move `vs-1.0/INSPECT_DEPOT.md` to `notes/INSPECT_DEPOT.md`. This is the one
   allowed change to `vs-1.0/`, since the file is ours, not the game's. Ask the
   user first.
3. Generate the hash list from the depot:
   `cd vs-1.0 && find . -type f ! -name INSPECT_DEPOT.md -print0 | sort -z | xargs -0 sha256sum > ../vs-1.0.sha256`.
   Commit **only the hashes of the files the patcher reads** (bundle, asset
   files) to `patcher/known-builds/8292959317163221061.sha256`. Hashes are not
   game content.
4. Promote `work/tools/deob.mjs` and `probe.mjs` into `patcher/src/`
   (`deobfuscate.js`, `extract.js`).
5. First commit. The message ends with the attribution line from the session.

**Done when:** the repo exists, `git status` shows no game files, and the hash
list for manifest 8292959317163221061 is committed.

## Phase 1: Patcher, extraction stage

1. **Locate and verify** (`locate.js`): take a path to the game folder, accept
   either `resources/app/` or `resources/app.asar` (use `@electron/asar` to
   read it), and check SHA-256 against `known-builds/`. Unknown hashes are a
   warning plus an `--allow-unknown` flag, not a crash.
2. **De-obfuscate** (`deobfuscate.js`): keep the approach of `deob.mjs`, and
   make it robust:
   - detect the array function, decoder function, offset and rotation target
     from the AST, never from constants;
   - compute the rotation by interpreting the checksum expression, no `eval`;
   - abort with a clear message if any decoder call stays unresolved, or if a
     string encoding (base64/RC4) is detected;
   - skip prettier in the patcher. Formatting only matters for humans.
3. **Extract** (`extract.js`): resolve TS enums, find each table by its first
   key (`WHIP`, `ANTONIO`, `BAT1`, `FOREST`, `GEM`, `MIGHT`, `BRAZIER`,
   `ReachLV5`, `NONE`), and evaluate with JS semantics:
   - a missing enum member becomes `null` and is logged (known cases:
     `MEDUSA1_FAST`, `MEDUSA2_FAST`, `BOSS_XLDETH`);
   - duplicate keys, last one wins (known case: `BOSS_XLDEATH`);
   - also extract the global constants block (PlayerPxSpeed, EnemySpeed,
     ProjectileSpeed, PixelScale…, report §8);
   - turn on acorn `locations: true` so every extracted value can carry its
     source line in debug output.
4. **Schema** (`schema/*.json`): write JSON Schemas for weapons, characters,
   enemies, stages, pickups, power-ups and constants. Validate every run.
5. **Golden tests** (`patcher/test/`): assert counts only, never values that
   would leak data: 41 weapon-table entries, 40 characters, 53 enemies,
   4 stages with 31/1/31/31 minute rows, 17 pickups, 14 shop power-ups. Tests
   skip with a notice when no game folder is configured (CI has no game).

**Done when:** `node patcher/bin/vspatch.js extract <game>` writes
`work/out/data/*.json` that pass schema validation, golden tests pass, and an
unknown or tampered bundle fails with a readable error.

## Phase 2: Clean-room behaviour notes

Before writing engine gameplay code, write one note per system in
`notes/behaviour/`. Each note has: what the system does, every number and
formula, update order and timing, edge cases, and `main.deob.js` line refs.
Start from report §6, and read the code to fill the gaps.

Order (matches what the engine needs first):

1. `loop.md`: frame order inside Core.Update, the every-4th-frame tasks, 60 Hz
   physics step, time events vs frame counters.
2. `player.md`: movement speed, stats and how they combine, damage, armour,
   shields, i-frames, revive, character bonuses (Antonio, Imelda, …).
3. `enemies.md`: movement, knockback, death, gem drop, HP scaling, skills
   (HPxLevel, FixedDirection, SelfDestruct, Medusa).
4. `spawning.md`: minute rows, spawn timer, the three spawn shapes, caps,
   bosses, culling and teleport.
5. `stage-events.md`: each event type (BAT_SWARM, FLOWER_WALL, MEDUSA_WALL, …).
   This was only located, not described, in the recon.
6. `weapons.md`: base weapon timing and stat formulas, then one section per
   weapon. Start with Whip and Magic Wand.
7. `pickups.md`: gems, merging past 400, magnet, chicken, coins, rosary,
   orologion, vacuum.
8. `levelup.md`: XP curve, option count, weighting, slot limits, existing-item
   chance.
9. `treasure.md`: tier chances, beginner's luck, prize types, evolution pull.
10. `meta.md`: achievements, unlocks, shop, save data (read
    `.webpack/main/index.js` for the save location, not covered in the recon).

**Done when:** each note exists and a reader could implement the system
without opening the original code.

## Phase 3: Engine skeleton on PC (SDL2)

1. CMake project with two targets: `vsdemake-sdl2` (Linux/Windows) and
   `vsdemake-xenon` (stub for now).
2. Platform layer interface: window/video, input (keyboard + gamepad mapped to
   one virtual pad), audio mixer, file I/O, monotonic time. Keep it small;
   everything else is platform-independent.
3. Fixed 60 Hz simulation step with interpolation-free rendering (the original
   is pixel art at PixelScale 1). Seeded RNG owned by the game, never `rand()`.
4. Data pack loader (Phase 4 defines the format; start by loading the JSON
   from `work/out/data` on PC only).
5. Sprite batcher + atlas lookup by frame name, camera following the player,
   tiled background.

**Done when:** the PC build opens a window, shows Antonio walking on the Mad
Forest background with keyboard or pad input, at a steady 60 Hz.

## Phase 4: Patcher, asset and pack stage

1. **Atlases:** read TexturePacker multiatlas JSON, keep frame rects, trims and
   pivots. Repack `enemies.png` (1021×1467) and anything non-power-of-two into
   power-of-two pages.
2. **Textures:** emit a console-ready format. Start with uncompressed ARGB8
   big-endian (20.7 MB total fits easily). Consider DXT later only if load
   times demand it.
3. **Tilemaps:** convert Tiled JSON, keeping the Spawners and Destructibles
   object layers and collision layers (Walls, PlayerWall, Obstacle).
4. **Font:** convert the BMFont damage digits, and ship our own UI font. The
   original used system Arial, which we can't redistribute. Use an openly
   licensed pixel font, committed with its licence.
5. **Audio:** decode OGG and re-encode to what the xenon backend plays (decide
   in Phase 5; PCM 16-bit big-endian is the safe default, about 50 MB for
   4.8 min of mostly mono audio, acceptable).
6. **Pack:** one file `vsdata.vspack` with a header (magic, version, source
   manifest ID, endianness), a directory, and aligned blobs. Document the
   format in `notes/pack-format.md` and put a reader in `engine/src/core/`.

**Done when:** `vspatch build <game> --out vsdata.vspack` produces one file
that the PC build loads instead of the JSON.

## Phase 5: Gameplay (PC first)

Implement from the Phase 2 notes, one system at a time, each with a test in
`engine/test/` where it can be tested headless:

1. Player movement and stats → 2. Enemy pool, chase, knockback → 3. Spawner
   with minute rows and the 300-enemy cap → 4. Whip and Magic Wand → 5. Gems,
   magnet, XP curve, level-up screen → 6. Remaining weapons and passives →
   7. Treasure and evolutions → 8. Stage events and bosses → 9. Stages 2 and 3
   → 10. Menus, character select, shop, save file.

Add a headless replay mode (seed + input log → final state hash) so gameplay
changes can be regression-tested without watching the screen.

**Done when:** a 30-minute Mad Forest run is playable on PC with Antonio,
including level-ups, evolutions and the end-of-stage boss.

## Phase 6: Xbox 360 port

1. Set up the free60 toolchain (xenon-gcc, libxenon) in a container or script
   under `engine/platform/xenon/`. Write the exact versions into
   `notes/toolchain.md`.
2. Implement the xenon platform layer: Xenos video (shaders for sprite
   batching), controller input, audio, file I/O from USB/HDD.
3. Byte order: the console is big-endian. The pack is written big-endian by
   the patcher, so the engine never swaps at runtime.
4. Performance budget: 300 enemies + ~350 bullets + 400 gems at 60 Hz. Profile
   with the replay mode early; batch all sprites into as few draws as possible.
5. Packaging: libxenon builds an ELF. Research and document how to turn it
   into the XEX/GOD outputs requested (tool, version, steps) before
   implementing. Don't guess at tools; record sources in `notes/toolchain.md`.

**Done when:** the same replay produces the same state hash on PC and Xbox
360, and Mad Forest runs at 60 Hz on hardware.

## Phase 7: Release preparation

1. README matches reality (commands, outputs, supported manifest list).
2. CI: build engine for SDL2 and xenon, run patcher unit tests (golden tests
   skip without a game folder).
3. Licence for our code, third-party licences (font, libraries), and a clear
   statement that no game content is distributed.
4. Ask the user before creating the GitHub repo, pushing or tagging a release.

---

## Per-session habit

- Start: read `notes/progress.md` and the relevant `notes/behaviour/*.md`.
- End: append to `notes/progress.md` what changed, what's blocked, and the
  next step. Run `git status` and confirm no game data is staged.
