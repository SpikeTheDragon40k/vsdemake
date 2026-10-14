# vsdemake

An unofficial Xbox 360 homebrew demake of Vampire Survivors, with a C99
gameplay engine, an SDL2 backend for PC development, and an Xbox 360 XDK
backend targeting `default.xex`.

The project separates the engine from the original game's content:

- **Engine:** a reimplementation using documented gameplay behaviour.
- **Patcher:** a Node.js tool that reads files from your own legally
  purchased Steam copy of Vampire Survivors and converts the required
  data and assets into a pack the engine can load.

The project does not distribute the original Vampire Survivors source
code, game assets, or generated data packs. You must own Vampire Survivors
on Steam and supply the supported game files yourself.

> **Status: work in progress.** A playable MVP exists on PC using SDL2,
> and the first XDK-built Xbox 360 executable has successfully booted on
> hardware. Console compatibility, performance, and gameplay accuracy
> remain under testing. Audio, save data, and several gameplay systems
> are not yet implemented.
>
> See `notes/progress.md` for the latest development status.

---

## How it works

```text
Your supported Steam depot files
              |
              v
       Local Node.js patcher
              |
              v
        vsdata.vspack
              |
              v
    vsdemake engine on PC / Xbox 360
```

The patcher reads the game files without executing the original game.
It converts textures, atlases, maps, fonts, language strings, and gameplay
tables into a single binary pack.

The title-screen lettering is also generated locally: the patcher
assembles the “VSDEMAKE” title from the user's original title image.
The resulting texture is stored in the generated pack, not distributed
as a project asset.

Patching does not upload game files or download additional content.
Installing the patcher's dependencies with `npm install` may require
network access.

**Do not distribute `vsdata.vspack`, extracted game data, converted assets,
or de-obfuscated game source.**

## Why an old version?

The demake targets the early Electron/Phaser version of Vampire Survivors.
Its data and assets are in formats supported by the patcher.

Supported build:

| Steam app | Depot | Manifest | Game version |
| --- | --- | --- | --- |
| 1794680 | 1794681 | `8292959317163221061` | 0.2.10 |

Other manifests are not supported. The patcher checks hashes against the
known build. The `--allow-unknown` override is intended for development
and does not make another build supported.

## What you need

### For patching

- A Steam account that owns Vampire Survivors.
- The Steam desktop client.
- Node.js 20 or newer.
- The supported depot files listed above.

### For playing on PC

- A C compiler.
- CMake 3.16 or newer.
- SDL2 development files (`libsdl2-dev` on Debian/Ubuntu).

### For building and playing on Xbox 360

- An Xbox 360 capable of running unsigned executables.
- A Windows build environment with Visual Studio 2010 and the Xbox 360 XDK,
  obtained and used in accordance with its applicable license.
- A way to transfer the executable and your generated data pack to the console.

This project does not provide the XDK, instructions for obtaining
unauthorized SDK copies, or console-modification instructions.

The current console workflow targets the XDK backend. A libxenon backend
also exists, but is not the verified console path described here.

## Step 1: Download the supported build from Steam

Sign in to the Steam account that owns Vampire Survivors.

1. Open the Steam console:

   - **Windows:** press `Win + R` and enter `steam://open/console`.
   - **Linux:** open `steam://open/console`, or run:

     ```bash
     steam steam://open/console
     ```

   - **macOS:** open `steam://open/console` using Steam's registered
     URL handler.

2. Enter:

   ```text
   download_depot 1794680 1794681 8292959317163221061
   ```

3. Wait for Steam to report that the download is complete. Console
   output may be sparse while downloading.

   A Windows example:

   ```text
   Depot download complete : "C:\Program Files (x86)\Steam\steamapps\content\app_1794680\depot_1794681"
   ```

Keep the reported path. The depot download is separate from your normal
installed copy.

If Steam refuses access, confirm that the account owns the game. Access
also depends on Steam making the requested depot and manifest available.

## Step 2: Get the patcher

From a local checkout of this repository:

```bash
cd vsdemake/patcher
npm install
```

## Step 3: Verify and build your data pack

The patcher accepts the depot directory, its `resources/app` directory,
or `resources/app.asar` when present.

From the `patcher/` directory:

```bash
node bin/vspatch.js verify "<path to supported game files>"
node bin/vspatch.js build "<path to supported game files>" --out vsdata.vspack
```

The patcher:

1. Verifies the input against the known hashes for the supported build.
2. Reads gameplay tables without running the original game.
3. Converts supported assets into engine-readable formats.
4. Generates the demake title texture from the user's title image.
5. Writes `vsdata.vspack`, currently about 27 MB.

Audio conversion is not implemented yet.

The generated pack contains content derived from your game files.
Keep it local; do not upload it to the repository, attach it to issues,
or include it in releases.

### Inspection commands

For local development:

```bash
node bin/vspatch.js extract "<path to supported game files>" --out "<local output directory>"
node bin/vspatch.js deob "<path to supported game files>"
```

These commands produce extracted data or de-obfuscated game source.
Their output must remain outside version control and must not be
distributed with the project.

## Step 4: Play on PC

From the repository root:

```bash
cmake -S engine -B engine/build
cmake --build engine/build

./engine/build/vsdemake-sdl2 /path/to/vsdata.vspack
./engine/build/vsdemake-sdl2 /path/to/vsdata.vspack --scale 3
```

### Controls

| Action | Keyboard |
| --- | --- |
| Move | Arrow keys / WASD |
| Confirm / A | Enter / Space |
| Start | Esc |
| Back / B | Backspace |
| Quit | Shift + Esc |

Controllers are supported through SDL2. A pause screen is not implemented yet.

### Development options

| Option | Purpose |
| --- | --- |
| `--char N` | Start with character N, e.g. 1 Antonio, 2 Imelda |
| `--hitboxes` | Draw collision overlays |
| `--menu` | Stay on the title screen in headless mode |
| `--give NAME[:LEVEL]` | Add an item at startup; repeatable |
| `--chest` | Open a treasure chest immediately |
| `--locked` | Apply fresh-save item availability rules |
| `--show-levelup` | Open and hold the level-up screen |

Example headless regression run:

```bash
./engine/build/vsdemake-sdl2 /path/to/vsdata.vspack \
  --headless --no-render --frames 18000 \
  --seed 3 --input "R240 L240" --screenshot last.bmp
```

The engine prints a state hash for regression comparisons.

## Step 5: Play on Xbox 360

See:

- `docs/XBOX360_GUIDE.md` for the build and installation workflow.
- `docs/FIRST_XEX_TEST.md` for first-boot checks and troubleshooting.

The current workflow builds `default.xex` using the Xbox 360 XDK and
launches it through a compatible homebrew dashboard or launcher.

The first executable has successfully booted on hardware. This is an
early development milestone, not a guarantee of compatibility,
performance, or stability.

Transfer your locally generated `vsdata.vspack` according to the guide.
Do not include it in a shared executable package.

## Current limitations

The implementation is incomplete. Known gaps include:

- No audio playback or audio data in the pack.
- No persistent save data, shop, or achievement system.
- Incomplete weapon coverage.
- Some enemy and pickup behaviours remain unimplemented.
- Menus and presentation are still being refined.
- Xbox 360 testing and performance validation are ongoing.

Without persistent unlocks, the default item pool is more permissive
than a fresh original-game save. Use `--locked` for the implemented
fresh-save availability rules.

See `notes/progress.md` for detailed implementation status.

## Development and tests

### Patcher

```bash
cd patcher
npm test
```

Game-dependent tests skip when no game directory is available.
Set `VSDEMAKE_GAME` to a supported local game directory, or use the
documented local `vs-1.0/` layout.

### Engine

```bash
cmake -S engine -B engine/build
cmake --build engine/build
ctest --test-dir engine/build --output-on-failure
```

The pack-reader test expects a locally generated `work/vsdata.vspack`
and reports a skipped test when the pack is absent.

From the repository root, generate it with:

```bash
node patcher/bin/vspatch.js build vs-1.0 --out work/vsdata.vspack
```

### Repository layout

- `patcher/`: verification, static extraction, asset conversion, and packing.
- `engine/`: gameplay engine and platform backends.
- `schema/`: data schemas.
- `notes/`: depot report, behaviour descriptions, pack format, and progress.
- `docs/`: build, installation, and testing guides.
- `work/`: ignored local generated output.

Table layouts are defined in `patcher/src/tables.js`.
Regenerate the corresponding C headers with:

```bash
node patcher/tools/gen-c-header.js
```

Some planning documents describe earlier development phases or the
original libxenon target. Consult the current build guides and progress
log for the active workflow.

## Contribution and release rules

Do not commit or distribute:

- Original Steam depot files.
- Original or de-obfuscated Vampire Survivors source.
- Extracted game tables or language files.
- Original or converted game textures, fonts, maps, or audio.
- Generated `.vspack` files.
- Microsoft XDK files or other proprietary SDK material.

Keep local game files and generated output in ignored directories.
Check both staged files and repository history before publishing.

Tests and examples intended for distribution should use independently
created fixtures with documented licenses.

Only distribute third-party code and other dependencies where their
licenses permit it, and preserve the required notices. Do not assume
that a source license also authorizes redistribution of proprietary
assets, SDK components, or console binaries.

## Troubleshooting

| Problem | What to check |
| --- | --- |
| `download_depot` produces little output | Wait for completion; the depot is approximately 200 MB. |
| Steam reports an access error | Confirm account ownership and availability of the requested depot/manifest. |
| Patcher reports an unknown build | Check the depot and manifest IDs. Other builds are unsupported. |
| Patcher reports modified files | Redownload into a clean directory and verify again. |
| Engine reports a table-layout mismatch | Rebuild the pack with the current patcher. |
| Sprites are offset from their hitboxes | Update the engine and rebuild the pack; an earlier atlas-trimming bug affected sprite placement. |
| Console fails to load the pack | Follow the path and first-boot checks in the Xbox 360 guides. |

## Legal and attribution

vsdemake is an unofficial fan project. It is not affiliated with,
authorized by, or endorsed by poncle or Microsoft.

Vampire Survivors and its original content belong to their respective
rights holders. Xbox and related names belong to their respective
rights holders.

The project does not distribute the original game's source code or
assets. Game-derived content, including the generated title lettering,
is produced locally from files supplied by the user.

Requiring a purchased copy and keeping conversion local do not, by
themselves, establish permission from the rights holders. Use of the
game files, SDK, and resulting outputs remains subject to applicable
law and license terms.

Any license applied to this project's code covers only material the
project is entitled to license. It does not grant rights to
Vampire Survivors content or proprietary SDK components.

Support poncle by purchasing and playing the original game.
