# Xbox 360 guide: setup, XDK, patching, build and launch

This is the end-to-end procedure for running vsdemake on a real Xbox 360
from **Aurora, XeXMenu or FreeStyle Dash** as a normal `default.xex`.

> **Status (2026-10-13):** the XDK backend (`engine/src/platform/xdk/`,
> `engine/xdk/`) was written without the XDK at hand and has **never been
> compiled or run**. Expect the first build to need small fixes (header
> names, library names, project-file quirks). The step-by-step first test is
> **[docs/FIRST_XEX_TEST.md](FIRST_XEX_TEST.md)**; §7 lists what to verify. The
> older libxenon/XeLL backend is kept in `engine/src/platform/xenon/` and
> `notes/toolchain.md` as a fallback for people without the XDK; it is not
> the plan.

The whole flow:

```
 [PC]  Steam depot ──vspatch build──▶ vsdata.vspack ─┐
 [PC]  engine/xdk ──VS2010 + XDK──▶ default.xex ───┼──▶ HDD or USB ──▶ Aurora / XeXMenu
                                                   ┘
```

---

## 1. What you need

| Item | Notes |
| --- | --- |
| Xbox 360 with RGH or JTAG | Any revision that runs unsigned XEX files. Setting this up is out of scope; follow a current RGH guide for your board. |
| A dashboard: Aurora, XeXMenu or FreeStyle Dash | Any of them launches `default.xex` from a folder. |
| Windows PC with Visual Studio 2010 and the **Xbox 360 XDK** | See §2. |
| Steam account that owns Vampire Survivors + the Steam client | For the data (§3). |
| Node.js 20+ | For the patcher. |
| USB stick (FAT32) or FTP access to the console (Aurora has an FTP server) | To copy the two files over. |
| Xbox 360 controller | XInput, controller 0. |

Nothing from the game is ever downloaded by this project; the data pack is
built on your PC from your own Steam copy.

---

## 2. The Xbox 360 XDK

The XDK is Microsoft's proprietary development kit for the 360: Visual
Studio integration, the PowerPC compiler, Direct3D/XInput/XAudio2 headers
and libraries, and tools (`imagexex`, `xbcp`, `xbwatson`). Microsoft only
distributed it to registered developers under NDA and the program is
closed, so this project cannot point you at a download; you need a copy you
already hold, and only Microsoft's licence terms govern its use. This guide
assumes the last release, **XDK 21256.3** (Visual Studio 2010 integration);
older 2008-era releases differ in paths and the shader compiler.

Install facts you'll need:

- Install Visual Studio 2010 (Professional or better; Express has no
  Xbox 360 platform) **before** the XDK.
- Default install folder: `C:\Program Files (x86)\Microsoft Xbox 360 SDK\`.
  The installer sets the environment variable `XEDK` to it. Headers are in
  `include\xbox`, libraries in `lib\xbox`, tools in `bin\win32`.
- After installing, Visual Studio shows an **"Xbox 360"** platform in the
  configuration manager and an "Xbox 360 > Console Application" wizard.
- Optional: "Xbox 360 Neighborhood" (`xbwatson`, `xbcp`) talks to a devkit
  over the network; a retail RGH console does not expose that, so use FTP
  or USB to copy files.

---

## 3. Build the data pack (on the PC)

1. In Steam, open the console (`Win+R` → `steam://open/console`) and run:

   ```
   download_depot 1794680 1794681 8292959317163221061
   ```

   Wait for `Depot download complete : "<folder>"`. That folder is the
   0.2.10 Electron build; your installed game is untouched.

2. Build the pack:

   ```bat
   cd vsdemake\patcher
   npm install
   node bin\vspatch.js verify "<folder>"
   node bin\vspatch.js build  "<folder>" --out ..\vsdata.vspack
   ```

   `verify` must print `ok: manifest 8292959317163221061, 70 files match`.
   `build` writes a ~27 MB `vsdata.vspack`. It is already big-endian, so
   the console loads it as is.

   Keep this file to yourself; it is derived from your purchased copy.

---

## 4. Build `default.xex`

1. Open `engine\xdk\vsdemake.sln` in Visual Studio 2010.
2. Pick the **Release | Xbox 360** configuration.
3. Build. Output: `engine\xdk\out\Release\default.xex` (the project runs
   `imagexex` on `default.exe` as the last step).

Project settings worth knowing (all in `vsdemake.vcxproj`):

- The game core is C99 and is compiled **as C++** (`CompileAs=CompileAsCpp`)
  because Visual C++ 2010 has no C99 mode. `core/vs_compat.h` maps
  `snprintf`/`strtoull` to the MSVC names.
- Defines: `_XBOX`, `VS_XDK=1`, `VS_BIG_ENDIAN=1`.
- Libraries: `xapilib.lib d3d9.lib d3dx9.lib xgraphics.lib xboxkrnl.lib`
  (debug: the `…d.lib` variants).
- Shaders are two tiny HLSL programs compiled at start-up with
  `D3DXCompileShader` (`vs_3_0`/`ps_3_0`), so no offline shader step.
- The platform API (`core/vs_platform.h`) has C linkage even when compiled
  as C++, matching the `extern "C"` definitions in `vs_platform_xdk.cpp`.
- The XEX step: with the XDK integration, Visual Studio turns
  `default.exe` into `default.xex` automatically (the `ImageXex` tool). If
  your project shows no `.xex`, check Configuration Properties → Xbox 360
  Image Conversion, or run `imagexex /out:default.xex default.exe` from
  the XDK command prompt.

If the `.vcxproj` refuses to load (XDK integration versions differ), create
a new "Xbox 360 > Console Application" from the wizard, delete its
generated `main.cpp`, add every file listed in `vsdemake.vcxproj`
(`..\src\core\*.c`, `..\src\game\*.c`, `..\src\main.c`,
`..\src\platform\xdk\vs_platform_xdk.cpp`), and copy the settings above into
the project properties (C/C++ → Advanced → Compile As: C++; Preprocessor
definitions; Linker → Input).

### Likely first-build errors

| Error | Fix |
| --- | --- |
| `xgraphics.h` not found / `XGetVideoMode` undefined | Remove the `XGetVideoMode` block in `vs_plat_init` and hard-code 1280×720, or use `XGetVideoMode` from `xtl.h` only. |
| `D3DFMT_LIN_A8R8G8B8` undefined | Use `D3DFMT_A8R8G8B8` and tile the texture with `XGTileTextureLevel`, or create with `D3DUSAGE_...` linear flags per your XDK's `d3d9.h`. |
| `DrawPrimitiveUP` missing | Replace with a dynamic vertex buffer (`CreateVertexBuffer` + `Lock`, `SetStreamSource`, `DrawPrimitive`). |
| `stdint.h` not found | Your XDK's CRT lacks it: add a `stdint.h` to `engine/src` with `typedef` lines for `int8_t`…`uint64_t` (`__int64` for 64-bit) and report it. |
| `unresolved external symbol vs_plat_…` | The platform header lost its `extern "C"` block; it must wrap all declarations in `core/vs_platform.h`. |
| C++ errors in the core (`void*` conversions) | Add the cast; report it so the PC side gets the same fix. |
| `Cannot open include file: 'ObjBase.h'` from `d3dx9.h` | `_XBOX` is missing from the preprocessor definitions. Add it back; the hand-made wizard project is the usual cause. |
| `Cannot open include file: 'core/vs_compat.h'`, file names in capitals like `VS_PLATF.CPP` | The source was copied with 8.3 names. Copy it again (see "Building in a VirtualBox VM"). |
| `Cannot open include file: 'core/...'`, file names normal | The include directory does not point at `engine\src`. Use its full path. |
| `_vsnprintf` warnings | Harmless. |

### Building in a VirtualBox VM

The XDK is Windows-only and proprietary, so there is no Docker image for
the XEX step. A Windows VM you set up once from your own media is the
reproducible equivalent. Nothing from the VM needs to leave it except
`default.xex` and the build log.

VM settings that work:

| Setting | Value |
| --- | --- |
| Guest OS | Windows 7 SP1 x64 or Windows 10 x64 (VS2010 and XDK 21256.3 install on both; Windows 11 works but the XDK installer may need "Run as administrator") |
| RAM / CPUs | 4 GB / 2 cores minimum; the compile is small |
| Disk | 40 GB dynamically allocated (VS2010 ≈ 6 GB, XDK ≈ 4 GB) |
| Shared folder | The `vsdemake` checkout, mounted as a drive (Devices → Shared Folders → Auto-mount); or clone inside the VM with Git for Windows |
| Network | NAT is enough; the build does not need the console reachable |
| Guest Additions | Install them: shared folders and clipboard need them |

Install order inside the VM, then take a snapshot:

1. Windows updates, Guest Additions.
2. Visual Studio 2010 Professional (or higher), then **Service Pack 1**.
3. The Xbox 360 XDK (your own copy). Confirm `echo %XEDK%` in a new
   command prompt prints the install folder and that Visual Studio lists
   the **Xbox 360** platform.
4. Node.js 20+ if you also want to build the pack in the VM. Otherwise
   build the pack on the host (§3) and only copy `default.xex` out.
5. **Snapshot** the VM here ("clean toolchain"). Every build starts from
   this snapshot, which is what makes the result reproducible.

Build with the script instead of the IDE:

```bat
cd \\vboxsvr\vsdemake\engine\xdk      (or the mapped drive letter)
build-xex.bat
```

`build-xex.bat` runs the same `msbuild` command as §4, writes
`build-Release.log` next to the solution, prints the first errors on
failure, and prints the size of `out\Release\default.xex` on success.
`build-xex.bat Debug` builds the Debug configuration. Run it from the
*Xbox 360 Command Prompt* or the *Visual Studio Command Prompt (2010)*
so `msbuild` and `XEDK` are set.

Getting the source into the VM: use the shared folder, a `.zip` you
extract inside the VM, or `git clone`. **Do not** burn the folder to an ISO
and mount it: most ISO tools shorten names to 8.3 (`vs_platform_xdk.cpp`
becomes `VS_PLATF.CPP`) and nothing compiles. A `.zip` placed inside an ISO
is fine.

Notes:

- Building directly on a VirtualBox shared folder works but is slow and
  occasionally trips the MSVC "file in use" error (`LNK1104`). If that
  happens, copy the repo to `C:\vsdemake` inside the VM, build there, and
  copy the XEX back.
- The shared folder keeps `out\` and `build-*.log` on the host too. Both
  are git-ignored (`out/`), so they never get committed by accident.
- The VM never needs the game data or the pack unless you build the pack
  in it. Keep the data pack out of any VM image you might hand to others.

---

## 5. Install on the console

Create one folder containing both files:

```
Hdd1:\Games\vsdemake\          (or Usb0:\Games\vsdemake\)
├── default.xex
└── vsdata.vspack
```

The engine looks for the pack at `game:\vsdata.vspack` (the folder the XEX
was launched from), then `game:\data\vsdata.vspack`. It also tries
`hdd1:\vsdemake\…` and `usb0:\vsdemake\…`, but those drive names are not
mounted for a homebrew XEX by default, so **keep the pack next to
`default.xex`**. If the pack is missing or damaged, a red error screen
says so (press BACK to return to the dashboard).

How to copy:

- **FTP** (recommended): Aurora/FSD run an FTP server; connect with any FTP
  client (passive mode off works best) to the console's IP. The user name
  and password are shown in the dashboard's network/FTP settings. Upload
  the folder to `Hdd1:\Games\`.
- **USB**: format FAT32, copy the folder, plug it in. The pack is 27 MB so a
  FAT32 stick is fine.

---

## 6. Launch

- **Aurora**: add `Hdd1:\Games` (or the folder itself) to the content scan
  paths in Aurora's content settings and rescan; vsdemake appears as a
  title. Or open Aurora's file manager, browse to `default.xex` and launch
  it (fastest for a first test).
- **XeXMenu**: browse to the folder and press A on `default.xex`.
- **FreeStyle Dash**: add the folder as a game path and scan.

Controls: Start/A begins; the d-pad picks the character (up/down) and
stage (left/right), A confirms. In game: stick or d-pad to move; level-up
choices with d-pad up/down + A. Menus don't react to the stick yet. The guide button returns to
the dashboard as usual.

A homebrew XEX has no title artwork; Aurora shows a generic tile. Don't
add the real game's art to it.

---

## 7. First-boot checklist

Nothing has run on hardware yet. Confirm these in order and report:

1. **It boots and the pack loads.** A red "cannot start" screen names the
   problem (pack missing, damaged, or from another patcher version). An
   immediate return to the dashboard with no screen means Direct3D or the
   shaders failed in `vs_plat_init`; see FIRST_XEX_TEST.md §6.
2. **Picture orientation and size.** The 810×456 game area should fill
   the screen (aspect-fit, ~1.58× at 720p, point-sampled). Upside down → flip the `1.0 - y` term in
   `VS_SRC`.
3. **Colours.** Swapped channels (blue Antonio) → the texture format; try
   tiled `D3DFMT_A8R8G8B8` with `XGTileTextureLevel`, or `D3DFMT_LIN_A8B8G8R8`.
4. **Transparency.** Black boxes around sprites → `D3DRS_ALPHABLENDENABLE`
   isn't taking; make sure it is set after `BeginScene`.
5. **Controller.** Inverted stick → remove the negation on `ly` in
   `vs_plat_poll`.
6. **Speed.** HUD clock vs a stopwatch; it should be 60 Hz.

---

## 8. Updating

- New patcher version → rebuild `vsdata.vspack` (the engine refuses packs
  whose table layout differs: "table … layout mismatch").
- New engine → rebuild `default.xex`, copy it over.

## 9. Fallback: libxenon / XeLL

Without the XDK the only route is the bare-metal libxenon ELF booted from
XeLL. `engine/xenon/Makefile` and `notes/toolchain.md` cover it (free60
toolchain build from source, `vsdemake.elf32` on USB, boot into XeLL). That
backend has no colour tint, no alpha fades and no audio until a Xenos
shader is compiled for it.
