# vsdemake

An unofficial **Xbox 360 homebrew demake of Vampire Survivors**, built on
[libxenon](https://github.com/Free60Project/libxenon).

This repository contains **no game code and no game assets**. It contains:

- **the engine**: our own reimplementation of the gameplay in C99, written
  from the behaviour notes in `notes/behaviour/` (clean room);
- **the patcher**: a Node.js tool that reads *your own legally purchased
  Steam copy* of Vampire Survivors and converts its data and art into a file
  the engine can load.

You need to own Vampire Survivors on Steam to use this project.

> **Status: playable MVP on PC (SDL2); the Xbox 360 backend is not written
> yet.** See `notes/progress.md` for what exists and what is missing.

---

## Why an old version?

Current versions of Vampire Survivors run on Unity. The demake targets the
early **Electron/Phaser build** of the game, whose data and art are in formats
the patcher can read. Steam keeps old builds available to owners, so you can
download that build alongside your normal install without affecting it.

Supported build:

| Steam app | Depot | Manifest | Game version |
| --- | --- | --- | --- |
| 1794680 | 1794681 | `8292959317163221061` | 0.2.10 |

Other manifests are not supported. The patcher checks file hashes and refuses
any other build (`--allow-unknown` to try anyway).

## What you need

- A Steam account that **owns Vampire Survivors**.
- The Steam desktop client (Windows, macOS or Linux).
- [Node.js](https://nodejs.org/) 20 or newer, to run the patcher.
- To play on PC: a C compiler, CMake 3.16+ and SDL2 development files
  (`libsdl2-dev` on Debian/Ubuntu).
- For the console (later): an Xbox 360 able to run unsigned homebrew
  (RGH/JTAG). This project does not cover modifying your console.

## Step 1: Download the old build from Steam

Steam's console can download a specific old version of a game you own.

1. Open Steam and make sure you're signed in to the account that owns the game.
2. Open the Steam console:
   - **Windows:** press `Win + R`, enter `steam://open/console`.
   - **macOS / Linux:** open `steam://open/console` in your browser, or run
     `steam steam://open/console` in a terminal.
3. A **Console** tab appears in Steam. Type this command and press Enter:

   ```
   download_depot 1794680 1794681 8292959317163221061
   ```

4. Wait. Steam prints nothing while it works; when it finishes it prints the
   folder it downloaded to, for example:

   ```
   Depot download complete : "C:\Program Files (x86)\Steam\steamapps\content\app_1794680\depot_1794681"
   ```

   Note that path. This download doesn't replace or change your installed
   copy of the game.

## Step 2: Get the patcher

```bash
git clone https://github.com/<owner>/vsdemake.git
cd vsdemake/patcher
npm install
```

## Step 3: Patch

Point the patcher at the folder from Step 1 (the depot folder, its
`resources/app` folder or `resources/app.asar` all work):

```bash
node bin/vspatch.js verify "<path from step 1>"
node bin/vspatch.js build  "<path from step 1>" --out vsdata.vspack
```

The patcher:

1. checks that the files match manifest `8292959317163221061`;
2. reads the game data (weapons, characters, enemies, stages, …) directly
   from your files, without running the game;
3. converts the sprites, maps and font into console-friendly formats;
4. writes everything into a single `vsdata.vspack` (about 27 MB).

Nothing is uploaded or downloaded while patching. The output file is made from
your copy of the game and is for your personal use only. **Don't share it.**

Other commands: `vspatch extract <game> --out DIR` writes the tables as JSON
for inspection; `vspatch deob <game>` writes the de-obfuscated bundle.

## Step 4: Play on PC

```bash
cd engine && mkdir -p build && cd build
cmake .. && make
./vsdemake-sdl2 /path/to/vsdata.vspack            # window, 2x scale
./vsdemake-sdl2 /path/to/vsdata.vspack --scale 3
```

Keyboard: arrows or WASD move, Enter/Space confirm, Esc back/pause,
Shift+Esc quit. Game controllers work through SDL2.

Headless/regression runs (no window): `--headless --no-render --frames 18000
--seed 3 --input "R240 L240" --screenshot last.bmp` prints a state hash.

## Step 5: Copy to the console

*(planned)* The Xbox 360 backend (`engine/src/platform/xenon`) is not
implemented yet. The data pack is already big-endian so the console loads it
without conversion. Packaging (XEX/GOD) will be documented with the first
console release.

## Development

- `patcher/`: `npm test` runs the extraction and pack tests (they skip
  without a game folder; set `VSDEMAKE_GAME` or keep the depot in `vs-1.0/`).
- `engine/`: `ctest` runs the pack-reader test against `work/vsdata.vspack`.
- `notes/`: depot report, pack format, behaviour notes (one per system),
  progress log. `notes/FABLE_GUIDE.md` is the phase plan.
- Table layouts live once in `patcher/src/tables.js`;
  `node patcher/tools/gen-c-header.js` regenerates the C headers.

## Troubleshooting

| Problem | Fix |
| --- | --- |
| `download_depot` prints nothing for a long time | Normal, the download is about 200 MB. Wait for "Depot download complete". |
| `download_depot` fails with an access error | Check you're signed in to the account that owns the game. |
| Patcher reports *unknown build* | You downloaded a different manifest. Rerun Step 1 with exactly `8292959317163221061`. |
| Patcher reports *files modified* | Delete the downloaded folder and run Step 1 again. |
| Engine says *table … layout mismatch* | The pack was built with an older patcher. Rebuild it. |

## Legal

This is a fan project, not affiliated with or endorsed by poncle. Vampire
Survivors and all its content belong to their respective owners. The
repository contains only original code; all game data and art come from the
user's own purchased copy at patch time and are never distributed by this
project. If you enjoy the demake, buy and play the real game.
