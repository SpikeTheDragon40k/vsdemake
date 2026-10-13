# vsdemake

An unofficial **Xbox 360 homebrew demake of Vampire Survivors**, built on
[libxenon](https://github.com/Free60Project/libxenon).

This repository contains **no game code and no game assets**. It contains:

- **the engine**: our own reimplementation of the gameplay, written from
  scratch;
- **the patcher**: a tool that reads *your own legally purchased Steam copy*
  of Vampire Survivors and converts its data and art into a file the engine
  can load.

You need to own Vampire Survivors on Steam to use this project.

> **Status: work in progress.** The patcher and engine are under active
> development and not yet usable. The steps below describe the intended
> workflow; commands marked *(planned)* don't exist yet.

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

Other manifests are not supported. The patcher checks file hashes and will
refuse (or warn about) any other build.

## What you need

- A Steam account that **owns Vampire Survivors**.
- The Steam desktop client (Windows, macOS or Linux).
- [Node.js](https://nodejs.org/) 20 or newer, to run the patcher.
- An Xbox 360 able to run unsigned homebrew (RGH/JTAG). This project does not
  cover modifying your console.
- A USB drive or a way to copy files to the console.

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

*(planned)* Download the latest release from the Releases page, or build it
from source:

```bash
git clone https://github.com/<owner>/vsdemake.git
cd vsdemake/patcher
npm install
```

## Step 3: Patch

*(planned)* Point the patcher at the folder from Step 1:

```bash
node bin/vspatch.js build "<path from step 1>" --out vsdata.vspack
```

The patcher:

1. checks that the files match manifest `8292959317163221061`;
2. reads the game data (weapons, characters, enemies, stages) directly from
   your files, without running the game;
3. converts the sprites, maps, font and sounds into console-friendly formats;
4. writes everything into a single `vsdata.vspack`.

Nothing is uploaded or downloaded while patching. The output file is made from
your copy of the game and is for your personal use only. **Don't share it.**

## Step 4: Copy to the console

*(planned)* Put the demake build and your `vsdata.vspack` in the same folder on
a USB drive, plug it into the console and launch the demake from your homebrew
launcher. Exact file names and packaging (XEX/GOD) will be documented with the
first release.

## Troubleshooting

| Problem | Fix |
| --- | --- |
| `download_depot` prints nothing for a long time | Normal, the download is about 200 MB. Wait for "Depot download complete". |
| `download_depot` fails with an access error | Check you're signed in to the account that owns the game. |
| Patcher reports *unknown build* | You downloaded a different manifest. Rerun Step 1 with exactly `8292959317163221061`. |
| Patcher reports *files modified* | Delete the downloaded folder and run Step 1 again. |

## Building from source

*(planned)* The engine builds for PC (SDL2, used for development) and for
Xbox 360 (libxenon toolchain). Instructions will be added once the build
system is in place.

## Legal

This is a fan project, not affiliated with or endorsed by poncle. Vampire
Survivors and all its content belong to their respective owners. The
repository contains only original code; all game data and art come from the
user's own purchased copy at patch time and are never distributed by this
project. If you enjoy the demake, buy and play the real game.
