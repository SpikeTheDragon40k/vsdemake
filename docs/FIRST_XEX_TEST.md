# First XEX test: build and boot checklist

A one-sitting procedure for the **first** console build: build
`default.xex`, put it on the console next to the data pack, boot it, and
report back in a form that makes the next fix quick.

Background and reference material are in
[XBOX360_GUIDE.md](XBOX360_GUIDE.md). This file is the hands-on part.

> Nothing in `engine/src/platform/xdk/` has been compiled with the real XDK
> yet. A first build that fails is expected and useful: the goal of this
> session is to **get the exact errors or the exact on-screen result**, not
> to get a perfect run.

Time needed: about 1 hour (most of it is the first compile and copying).

---

## 0. Before you start (tick every box)

- [ ] Windows PC with **Visual Studio 2010** (Professional or higher) and the
      **Xbox 360 XDK** installed, in that order. In VS, *Build →
      Configuration Manager → Active solution platform* lists **Xbox 360**.
- [ ] **Node.js 20+** on the same PC (`node --version`).
- [ ] The **vsdemake source at the commit you are testing**. The engine and
      the pack must come from the same commit (the engine refuses packs whose
      table layout differs). Note the commit id: `git log -1 --format=%h`.
- [ ] The **Steam depot** downloaded with
      `download_depot 1794680 1794681 8292959317163221061`
      (see XBOX360_GUIDE.md §3). Note the folder Steam printed.
- [ ] Xbox 360 with RGH/JTAG booting into **Aurora**, **XeXMenu** or
      **FreeStyle Dash**, and a way to copy files: FTP (console and PC on the
      same network) or a FAT32 USB stick.
- [ ] A wired or wireless controller on **port 1** (controller 0).
- [ ] A phone to photograph the TV (for the report).

---

## 1. Build the data pack

Open a command prompt in the repository:

```bat
cd vsdemake\patcher
npm install
node bin\vspatch.js verify "C:\...\depot_1794681"
node bin\vspatch.js build  "C:\...\depot_1794681" --out ..\vsdata.vspack
```

Expected:

```
source: dir C:\...\depot_1794681\resources\app
ok: manifest 8292959317163221061, 70 files match
...
wrote ..\vsdata.vspack: 27.2 MB, 57 entries (20 TEX, 10 ATL, 2 MAP, 1 FNT, 12 ENM, 12 TAB)
```

If `verify` does not say `ok`, stop: the depot is a different build or
damaged (README → Troubleshooting).

Optional but useful: `npm test` should end with `# fail 0`.

---

## 2. Build `default.xex`

1. Open `engine\xdk\vsdemake.sln` in Visual Studio 2010.
2. Toolbar: configuration **Release**, platform **Xbox 360**.
3. *Build → Rebuild Solution*.

Expected output: `engine\xdk\out\Release\default.xex` (a few MB).

**Save the full build log either way.** The Output window can be copied, or
build from the *Xbox 360 Command Prompt* (Start menu, XDK folder), which
writes a log file:

```bat
cd vsdemake\engine\xdk
build-xex.bat
```

This runs `msbuild` on the solution and writes `build-Release.log` next to
it. Building inside a VirtualBox Windows VM is fine; see XBOX360_GUIDE.md §4
"Building in a VirtualBox VM" for the VM setup.

### If the solution doesn't load

Your XDK's Visual Studio integration may not accept the hand-written
project. Create the project from the wizard instead:

1. *File → New → Project → Visual C++ → Xbox 360 → Xbox 360 Console
   Application*, named `vsdemake`, location `vsdemake\engine\xdk2`.
2. Delete the generated `.cpp` file from the project.
3. *Add → Existing Item*: every `.c` file in `engine\src\core\` and
   `engine\src\game\`, plus `engine\src\main.c` and
   `engine\src\platform\xdk\vs_platform_xdk.cpp`.
   **Do not add** anything from `platform\sdl2` or `platform\xenon`.
4. Project properties, *All Configurations*:
   - C/C++ → General → Additional Include Directories: the **full path**
     of the folder that contains `core\` and `game\`, for example
     `C:\vsdemake\engine\src`. A relative path breaks easily because the
     wizard nests the project two folders deep.
   - C/C++ → Preprocessor → Definitions: **add** `VS_XDK=1;VS_BIG_ENDIAN=1`
     at the front and keep what is already there. `_XBOX` must stay in the
     list, or every file fails with "Cannot open include file: ObjBase.h".
   - C/C++ → Advanced → Compile As: **Compile as C++ Code (/TP)**.
   - C/C++ → Precompiled Headers: **Not Using Precompiled Headers**.
   - Linker → Input → Additional Dependencies: add `d3dx9.lib` (release) /
     `d3dx9d.lib` (debug) and `xgraphics.lib` / `xgraphicsd.lib` if the
     wizard didn't.
5. Build Release.

### Check the copy first

If the errors say `Cannot open include file: 'core/vs_compat.h'` and the
file names in the log are in capitals and cut short, such as `VS_PLATF.CPP`,
the source was damaged while copying it into the VM. Copying through an ISO
image shortens every name to 8 characters. Copy it again with a shared
folder, a `.zip`, or `git clone`, then rebuild. `build-xex.bat` detects this
case and says so.

### If it doesn't compile or link

Don't spend long on it. Note the **first 20 errors** (file, line, message)
and check the table in XBOX360_GUIDE.md §4 ("Likely first-build errors").
The quick fixes listed there are fine to try; anything else, report as-is.

---

## 3. Copy to the console

Make this folder on the console (FTP or USB):

```
Hdd1:\Games\vsdemake\
├── default.xex        (engine\xdk\out\Release\default.xex)
└── vsdata.vspack      (from step 1)
```

Both files must be **in the same folder**. The engine finds the pack as
`game:\vsdata.vspack`, i.e. next to the XEX.

- FTP: connect to the console's IP with the user/password shown in your
  dashboard's FTP settings; create `Games\vsdemake` on `Hdd1` and upload
  both files in **binary** mode (FileZilla does this by default).
- USB: FAT32 stick, same folder layout under `Usb0:\Games\vsdemake\`.

Check the pack's size on the console matches the PC (about 27 MB). A
truncated upload gives the "damaged" error screen.

---

## 4. Boot it

Simplest first: **XeXMenu** → browse to `Hdd1:\Games\vsdemake` → press A on
`default.xex`. Or Aurora → file manager → same folder → launch
`default.xex`. (Adding it to Aurora's library can wait.)

Start a stopwatch when you press A.

---

## 5. Run the test script

Go through in order and note what happens at each step. Take a photo
whenever something looks wrong.

| # | Do | Expected |
| --- | --- | --- |
| 1 | Launch | Within ~5 s: title image ("Vampire Survivors" logo) on black, "PRESS START" below. |
| 2 | Look at the picture | Image upright and filling the screen (the 810×456 game is stretched evenly, ~1.58× at 720p; thin bars at most). Correct colours (not blue-tinted / swapped). Logo has no black box around it. |
| 3 | Press **Start** | Character list on the left (Antonio, Imelda, …), "STAGE (left/right)" on the right. |
| 4 | **Down** ×2, **Up** ×2 on the **d-pad** | The `>` marker moves one line per press. (Menus use the d-pad only; the stick is for movement in game.) |
| 5 | **Left/Right** | Stage switches between Mad Forest and Inlaid Library. |
| 6 | Select Antonio + Mad Forest, press **A** | Game starts: forest ground with trees/paths, Antonio in the centre, HUD (blue XP bar top, timer, LV). |
| 7 | Stand still 5 s | Whip slash appears left/right every ~1.35 s. Bats approach from off-screen. |
| 8 | Move with the **stick** in all 4 directions, then the **d-pad** | Antonio moves the way you push (up = up). Background scrolls. Sprite faces left/right correctly. |
| 9 | Let bats hit you | Antonio flashes red, small red HP bar under him shrinks. |
| 10 | Kill bats | White damage numbers pop up, bats play a death animation, blue gems drop and fly to you. |
| 11 | Reach level 2 | "LEVEL UP" list appears and the game pauses. D-pad Up/Down + **A** picks; game resumes. |
| 12 | Play to the **1:00** mark on the HUD | Compare with your stopwatch: should agree within ~1 s (60 Hz). Note any stutter. |
| 13 | Die (or keep playing until you do) | "GAME OVER", then the summary screen; **A** returns to the title. |
| 14 | From the title, start **Inlaid Library** | Library floor and bookshelves; enemies come from the left/right edges. |
| 15 | Press the **Guide** button and quit to the dashboard | Returns to Aurora/XeXMenu normally. |

---

## 6. If it fails at boot

| What you see | Meaning | What to report |
| --- | --- | --- |
| Back to the dashboard immediately, nothing drawn | Direct3D device or shader creation failed in `vs_plat_init` (before anything can be drawn) | That it happened; whether a **Debug** build behaves the same |
| Red screen "vsdemake: cannot start" | Boot got far enough to draw; the message says why (pack not found / damaged / table layout mismatch) | Photo of the screen |
| Black screen forever | Hang after start-up (texture upload or first frame) | How long you waited; a Debug build result |
| Console freezes / "fatal crash" screen | Crash | Photo; Debug build result |
| Title shows but wrong colours, upside down, or black boxes | Format/orientation/blend issue; see XBOX360_GUIDE.md §7 | Photo |

For a red screen, fix the cause it names (usually the pack location) and
relaunch. BACK returns to the dashboard.

A **Debug** build (`/p:Configuration=Debug`) has extra runtime checks and is
worth one try when the Release build crashes or hangs: build it, copy its
`default.xex` over the Release one, and launch again.

---

## 7. Report back

Copy this, fill it in, and send it with the photos and `build-Release.log`:

```
vsdemake first XEX test
commit:              <git log -1 --format=%h>
XDK version:         <e.g. 21256.3>
project:             vsdemake.sln as-is / recreated from wizard
build result:        OK / failed (first errors pasted below)
console / dashboard: <RGH/JTAG, Aurora x.y / XeXMenu / FSD>
video output:        <HDMI/component, 720p/1080p>

script step reached: <1-15>
steps with problems: <number: what you saw>
timer vs stopwatch at 1:00: <seconds difference>
stutter/slowdown:    none / when many enemies / always

first build errors (if any):
<paste>
```

With that, the next commit can fix exactly what broke instead of guessing.
