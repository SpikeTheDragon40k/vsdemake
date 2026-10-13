# Xbox 360 guide: setup, toolchain, patching, build and launch

This is the end-to-end procedure for running vsdemake on a real Xbox 360.
Read the status box first.

> **Status (2026-10-13):** the console backend
> (`engine/src/platform/xenon/`) has been written against libxenon's headers
> but has **never been compiled or run**: the development container could
> not build the free60 toolchain (no access to the GNU mirrors). Expect the
> first `make` to show errors and the first boot to need the fixes listed in
> `notes/toolchain.md` §"What the backend assumes". Treat this document as
> the intended path, and report what actually happens so the notes can be
> corrected.

The whole flow:

```
 [PC]  Steam depot ──vspatch build──▶ vsdata.vspack ─┐
 [PC]  engine/ ──xenon-gcc (free60)──▶ vsdemake.elf32 ─┼──▶ USB stick ──▶ [Xbox 360 + XeLL]
                                                       ┘
```

---

## 1. What you need

| Item | Notes |
| --- | --- |
| Xbox 360 with RGH or JTAG | Any revision that can run unsigned code. Setting this up is out of scope; follow a current RGH guide for your board. |
| XeLL (Xenon Linux Loader) | Normally installed with the RGH/JTAG hack. It boots `.elf32` files from USB and shows a text console. Hold the eject button (or use the dashboard's "launch XeLL") to start it. |
| USB stick, FAT32 | 1 GB is plenty. The console reads FAT32 only; exFAT/NTFS won't mount. |
| A PC with Linux (or WSL2 / Docker on Windows or macOS) | Needed to build the free60 toolchain and the engine. |
| Steam account that owns Vampire Survivors + the Steam client | For the data (step 3). |
| Node.js 20+ | For the patcher. |
| Wired Xbox 360 controller (or wireless + receiver) | libxenon reads controller 0 over USB. |

Nothing from the game is ever downloaded by this project; the data pack is
built on your PC from your own Steam copy.

---

## 2. Build the free60 toolchain (once, ~30–60 min)

The toolchain is a cross GCC for the 360's PowerPC core plus libxenon (the
bare-metal library: video, USB, FAT, controller, sound).

### 2a. Linux (Debian/Ubuntu)

```bash
sudo apt-get install build-essential git wget flex bison texinfo gcc-multilib \
     libgmp-dev libmpfr-dev libmpc-dev libz-dev python3

git clone https://github.com/Free60Project/libxenon.git
cd libxenon/toolchain
export DEVKITXENON=/usr/local/xenon
export PATH=$DEVKITXENON/bin:$PATH
sudo mkdir -p $DEVKITXENON && sudo chown $USER $DEVKITXENON

./build-xenon-toolchain toolchain     # binutils 2.32, gcc 16.2.0, newlib 3.1.0
./build-xenon-toolchain libxenon      # libxenon + libfat into $DEVKITXENON
```

Downloads come from `mirror.netcologne.de` and `sourceware.org`; if a
mirror is down, edit the `*_DL` variables at the top of the script.

Put the two `export` lines in your shell profile. Check:

```bash
xenon-gcc --version
ls $DEVKITXENON/usr/include/xenos/xe.h $DEVKITXENON/usr/lib/libxenon.a
```

### 2b. Docker (any OS)

The libxenon repo ships `toolchain.dockerfile` and `libxenon.dockerfile`:

```bash
cd libxenon
docker build -f toolchain.dockerfile -t xenon-toolchain .
docker build -f libxenon.dockerfile  -t xenon-libxenon .
```

Then run builds inside the image with the vsdemake checkout mounted:

```bash
docker run --rm -it -v "$PWD/../vsdemake:/src" -w /src xenon-libxenon bash
# inside: export DEVKITXENON=/usr/local/xenon; export PATH=$DEVKITXENON/bin:$PATH
```

(Check the Dockerfiles for the exact install prefix they use.)

### 2c. Windows

Use WSL2 (Ubuntu) and follow 2a, or Docker Desktop and follow 2b. Building
the toolchain natively on Windows is not supported.

---

## 3. Build the data pack (on the PC)

This is the same as the PC workflow in the README, repeated here so this
guide is complete.

1. In Steam, open the console (`Win+R` → `steam://open/console`, or
   `steam steam://open/console` on Linux/macOS) and run:

   ```
   download_depot 1794680 1794681 8292959317163221061
   ```

   Wait for `Depot download complete : "<folder>"`. That folder is the
   0.2.10 Electron build; your installed game is untouched.

2. Build the pack:

   ```bash
   cd vsdemake/patcher
   npm install
   node bin/vspatch.js verify "<folder>"
   node bin/vspatch.js build  "<folder>" --out ../vsdata.vspack
   ```

   `verify` must print `ok: manifest 8292959317163221061, 70 files match`.
   `build` writes a ~27 MB `vsdata.vspack`. It is already big-endian, so
   the console loads it as is.

   Keep this file to yourself; it is derived from your purchased copy.

---

## 4. Build the engine for the console

```bash
cd vsdemake/engine/xenon
export DEVKITXENON=/usr/local/xenon
export PATH=$DEVKITXENON/bin:$PATH
make
```

Output: `vsdemake.elf32` (the loadable image; `vsdemake.elf` is the
unstripped one for debugging with `xenon-objdump`/`xenon-gdb`).

The Makefile includes `$(DEVKITXENON)/rules`, which provides `MACHDEP`
(`-m32 -maltivec -mpowerpc64 -fno-pic -mhard-float`), the include/lib
paths and `app.lds`. It links `-lxenon -lfat -lm`.

### If it does not build

Likely first failures and what to do:

| Error | Fix |
| --- | --- |
| `libfat/fat.h: No such file` or `cannot find -lfat` | libfat was not installed into `$DEVKITXENON`. Re-run `./build-xenon-toolchain libxenon`, or check the header path (`find $DEVKITXENON -name fat.h`) and adjust the `#include` and `LIBS` in `engine/xenon/Makefile`. |
| Unknown function in `vs_platform_xenon.c` | The backend was written against the libxenon headers of October 2026 (`drivers/xenos/xe.h`, `drivers/input/input.h`, `drivers/usb/usbmain.h`, `drivers/ppc/timebase.h`). Compare with your installed headers; the needed calls are listed in `notes/toolchain.md`. |
| Warnings about `-std=gnu99` or AltiVec | Harmless. |
| Link errors about `read`/`_start`/`exc_base` | Add `-u read -u _start -u exc_base` to `LDFLAGS` (older rules files needed this). |

Before touching the console you can sanity-check the core on the PC build
(`engine/build/test_pack`, `vsdemake-sdl2 --headless …`), which shares all
code except the platform file.

---

## 5. Put it on the USB stick

Format the stick FAT32. Create this layout:

```
USB:/
└── vsdemake/
    ├── vsdemake.elf32
    └── vsdata.vspack
```

The engine looks for the pack at `uda:/vsdemake/vsdata.vspack`,
`uda:/vsdata.vspack`, `sda:/vsdemake/vsdata.vspack`, `sda:/vsdata.vspack`,
`udb:/vsdata.vspack` in that order (`uda:` = first USB drive, `sda:` =
internal HDD). The ELF can also live on the HDD if you prefer.

Eject the stick cleanly; FAT32 corruption shows up on the console as a
missing pack.

---

## 6. Launch on the console

1. Plug the USB stick and a wired controller into the console.
2. Boot into XeLL (eject-button boot on most RGH setups, or your
   dashboard's XeLL/"launch homebrew" entry).
3. XeLL scans USB for a bootable ELF. Depending on your XeLL build either:
   - it lists files and you pick `vsdemake/vsdemake.elf32`, or
   - it only auto-boots `xenon.elf`/`updflash.bin` from the root. In that
     case copy `vsdemake.elf32` to `USB:/xenon.elf` as well (and keep the pack
     path above), then reboot into XeLL.
   - Alternatively use a homebrew file browser (the libxenon `browser`
     example, or the dashboard's homebrew launcher) to run the ELF.
4. The console prints a few lines (`pack: 56 entries, manifest
   8292959317163221061`) on the XeLL text console, then switches to the
   game. Title screen: **Start/A** to begin, d-pad/stick to pick character
   and stage (left/right switches Mad Forest / Inlaid Library), **A** to
   confirm. In game: stick or d-pad to move; level-up choices with
   up/down + A. **Guide button** exits back to XeLL.

Video mode is whatever XeLL/libxenon negotiated (`VIDEO_MODE_AUTO`, HDMI or
component). The 810×456 game area is integer-scaled and letterboxed.

---

## 7. First-boot checklist (things to verify and report)

Because nothing has been run on hardware yet, confirm these in order and
note the result in `notes/toolchain.md`:

1. **Boots and prints the pack line.** If it stops at `cannot read`, the
   pack path/USB mount is wrong (try the HDD or the root of the stick).
2. **Picture orientation.** If the title is upside down or mirrored, the
   ortho matrix in `vs_plat_begin_frame` has the wrong sign convention (see
   `notes/toolchain.md` item 2). Flip the sign of the translation terms
   first.
3. **Textures.** Garbage colours mean the texture format flag is wrong
   (try `XE_FMT_8888 | XE_FMT_BGRA`, or tiled textures). Black sprites with
   correct shapes mean the brightness/normal trick isn't working: set
   `lightDirection` to `(1,1,1,0)` and `normal` to `(b,0,0)` → still `b`.
4. **Transparency.** Sprites with opaque black boxes mean blending isn't
   enabled; check the `Xe_SetBlendControl` call order relative to
   `Xe_InvalidateState`.
5. **Controller.** If the stick is inverted, remove the negation on
   `pad->ly`.
6. **Speed.** Watch the HUD clock against a stopwatch; 60 Hz is assumed.
   If the game runs slow, start with the O(n²) enemy separation in
   `enemy.c` (switch to a grid) and the per-frame vertex buffer lock.

Known limitations of this first backend: colour tints show as brightness,
per-sprite alpha fades are dropped (sprites below alpha 64 vanish instead of
fading), no audio. All three go away once a 2D Xenos shader with a COLOR
input is compiled (needs the XNA shader compiler, see the libxenon cube
example README) and a mixer is added.

---

## 8. Updating

- New patcher version → rebuild `vsdata.vspack` (the engine refuses packs
  whose table layout differs: "table … layout mismatch").
- New engine → rebuild `vsdemake.elf32`, copy over the old one.
- The pack and the ELF are independent; you only need to redo the one that
  changed.

## 9. Packaging as XEX/GOD (not done)

A libxenon ELF runs from XeLL, not from the dashboard. Wrapping it for
dashboard launch (XEX or GOD container) is not researched yet; see
`notes/toolchain.md` §Packaging. Until then XeLL is the launcher.
