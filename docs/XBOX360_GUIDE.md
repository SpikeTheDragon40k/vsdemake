# Xbox 360 guide: setup, XDK, patching, build and launch

This is the end-to-end procedure for running vsdemake on a real Xbox 360
from **Aurora, XeXMenu or FreeStyle Dash** as a normal `default.xex`.

> **Status (2026-10-13):** the XDK backend (`engine/src/platform/xdk/`,
> `engine/xdk/`) was written without the XDK at hand and has **never been
> compiled or run**. Expect the first build to need small fixes (header
> names, library names, project-file quirks). §7 lists what to verify. The
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
| C++ errors in the core (`void*` conversions) | Add the cast; report it so the PC side gets the same fix. |
| `_vsnprintf` warnings | Harmless. |

---

## 5. Install on the console

Create one folder containing both files:

```
Hdd1:\Games\vsdemake\          (or Usb0:\Games\vsdemake\)
├── default.xex
└── vsdata.vspack
```

The engine looks for the pack at `game:\vsdata.vspack` (the folder the XEX
was launched from), then `game:\data\vsdata.vspack`,
`hdd1:\vsdemake\vsdata.vspack`, `usb0:\vsdemake\vsdata.vspack`.

How to copy:

- **FTP** (recommended): Aurora/FSD run an FTP server; connect with any FTP
  client to the console's IP (user/password `xbox`/`xbox` by default) and
  upload the folder to `Hdd1:\Games\`.
- **USB**: format FAT32, copy the folder, plug it in. The pack is 27 MB so a
  FAT32 stick is fine.

---

## 6. Launch

- **Aurora**: Settings → Content → add `Hdd1:\Games` as a scan path (or
  the folder itself), run a scan; vsdemake appears as a title. Or use
  Aurora's file manager to browse to `default.xex` and launch it.
- **XeXMenu**: browse to the folder and press A on `default.xex`.
- **FreeStyle Dash**: add the folder as a game path and scan.

Controls: Start/A begins; d-pad or stick picks character, left/right
switches Mad Forest / Inlaid Library, A confirms. In game: stick or d-pad
to move; level-up choices with up/down + A. The guide button returns to
the dashboard as usual.

Optional for Aurora: add `nxeart` (a 64×64 PNG) next to `default.xex` for
an icon, and a title in Aurora's details. Don't use the real game's art.

---

## 7. First-boot checklist

Nothing has run on hardware yet. Confirm these in order and report:

1. **It boots and the pack loads.** A black screen that returns to the
   dashboard means `vs_plat_init` or the pack read failed; check the
   folder layout in §5. (Debug output goes to `OutputDebugString`; without a
   devkit you won't see it, so a missing-pack check will be added as an
   on-screen message once the first build works.)
2. **Picture orientation and size.** The 810×456 game area should be
   integer-scaled and centred. Upside down → flip the `1.0 - y` term in
   `VS_SRC`.
3. **Colours.** Swapped channels (blue Antonio) → the texture format; try
   tiled `D3DFMT_A8R8G8B8` with `XGTileTextureLevel`, or `D3DFMT_LIN_A8B8G8R8`.
4. **Transparency.** Black boxes around sprites → `D3DRS_ALPHABLENDENABLE`
   isn't taking; make sure it is set after `BeginScene`.
5. **Controller.** Inverted stick → remove the negation on `ly`.
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
