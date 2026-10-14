# Xbox 360 toolchain and port notes

**Decision 2026-10-13:** the console target is the Microsoft XDK
(`engine/src/platform/xdk/vs_platform_xdk.cpp`, `engine/xdk/`), producing a
`default.xex` for Aurora/XeXMenu. The libxenon notes below stay as the
fallback for people without the XDK. See `docs/XBOX360_GUIDE.md`.

## XDK backend assumptions (verify on first build)

- Direct3D 9-style API from `xtl.h`: `Direct3DCreate9`, `CreateDevice`
  with a 1280×720 A8R8G8B8 back buffer, `D3DPRESENT_INTERVAL_ONE`.
- Shaders compiled at run time with `D3DXCompileShader` (vs_3_0/ps_3_0);
  vertex = POSITION float3 (framebuffer px), COLOR D3DCOLOR, TEXCOORD float2.
- Textures `D3DFMT_LIN_A8R8G8B8`, pool DEFAULT, rows copied with the
  locked pitch; the pack's big-endian ARGB words are the native layout.
- `DrawPrimitiveUP` per texture batch; `Clear`/`BeginScene`/`EndScene`/`Present`.
- `XInputGetState(0)`; `GetTickCount`/`Sleep`; `fopen` on `game:\\`.
- Entry point `int main(void)` (no argv on the console); the core is compiled as C++
  (`vs_compat.h`); `vs_platform.h` has C linkage for the backend.
- Aspect-fit scale (not integer) to the 1280×720 back buffer; `vs_game` is a
  static (604 KB); boot failures show `fatal_screen` from `main.c`.
- Audio: XAudio2 (`xaudio2.h`; Release links `xaudio2.lib` + `xmcore.lib`,
  Debug `xaudiod2.lib` + `xmcored.lib`; xmcore provides the lock-free
  queues XAudio2 uses, `XLFQueueCreate` etc., found on the first build): one
  mastering voice, one 44.1 kHz 16-bit stereo PCM source voice in the
  console's native byte order, fed 1024-frame buffers from the frame loop
  (up to 7 queued). If the sound is noise, the
  first suspect is byte order (swap the samples before submitting).
- Saves: plain `fopen`/`fwrite` on `game:\vsdemake.sav` (no `rename`, which
  the first console test suggested is unreliable); `vsnprintf` is mapped to
  `_vsnprintf` in `vs_compat.h`. The game-over screen reports the result. If
  it says SAVE FAILED, `game:\` is not writable for this launch method.


## Toolchain (free60)

Source: https://github.com/Free60Project/libxenon (BSD; the makefile
rules are GPL, copied from devkitPro). The toolchain is built from source
by `toolchain/build-xenon-toolchain` in that repo:

- binutils 2.32, gcc 16.2.0 (patched), newlib 3.1.0; downloaded from
  mirror.netcologne.de / sourceware.org.
- `./build-xenon-toolchain toolchain` then `./build-xenon-toolchain libxenon`
  (installs to `/usr/local/xenon`; `export DEVKITXENON=/usr/local/xenon`).
- Alternatively `toolchain.dockerfile` + `libxenon.dockerfile` in the repo.

Status 2026-10-13: the cloud container cannot reach the GNU/sourceware
mirrors, so the toolchain was **not built** here. Nothing under
`engine/src/platform/xenon` or `engine/xenon/Makefile` has been compiled.

## What the backend assumes (verify on first build)

`engine/src/platform/xenon/vs_platform_xenon.c`, written against the
headers in libxenon `drivers/` and the `cube` example:

1. Init order as in the examples: `xenos_init(VIDEO_MODE_AUTO)`,
   `console_init`, `xenon_make_it_faster(XENON_SPEED_FULL)`, `usb_init`,
   `usb_do_poll`, `xenon_ata_init`, `fatInitDefault`, then `Xe_Init`,
   shaders, vertex buffer, `edram_init`, `console_close`.
2. Shaders: the cube example's precompiled `vs.vsu`/`ps.psu` embedded in
   `vs_xenon_shaders.c`. Vertex format POSITION float3, NORMAL float3,
   TEXCOORD float2 (32-byte stride), patched with
   `Xe_ShaderApplyVFetchPatches`. The VS computes `mul(transpose(c0..c3),
   pos)`; we load a row-major ortho matrix mapping framebuffer pixels to clip
   space. **If the picture is upside down or off-screen, this matrix is the
   first suspect** (the cube example negates column 3 of its projection; our
   ortho has the translation in that column, so a sign flip there is the
   likely fix).
3. The PS gives `rgb = dot(normal, lightDirection) * tex.rgb`, `a = tex.a`.
   We put brightness in `normal.x` and `lightDirection = (1,0,0,0)`. Colour
   tints become brightness; per-sprite alpha is unavailable (sprites with
   alpha < 64 are dropped). A real 2D shader (tint + alpha from a COLOR
   vertex element) needs Xenos HLSL compilation (Windows + XNA shader
   compiler, see the cube example README) — do this once hardware is
   available and replace the blobs.
4. Textures: `Xe_CreateTexture(w, h, 0, XE_FMT_8888 | XE_FMT_ARGB, 0)`
   (linear), rows copied with `wpitch`. The pack's big-endian ARGB words are
   the console's native byte order. `use_filtering = 0` for pixel art.
5. One vertex buffer of 20000 quads locked per frame; batches split on
   texture change; `Xe_DrawPrimitive(TRIANGLELIST)` per batch;
   `Xe_Resolve` + `Xe_Sync` per frame (vsync by resolve).
6. Input: `get_controller_data(&c, 0)` after `usb_do_poll()`. Stick y sign
   assumed up-positive (negated). Guide button exits to XeLL.
7. Time: `mftb() / (PPC_TIMEBASE_FREQ/1000)`; `mdelay` for sleeps.
8. Files: newlib `fopen` on `uda:/` (USB) or `sda:/` (HDD) after
   `fatInitDefault`; `main.c` looks for `vsdemake/vsdata.vspack` or
   `vsdata.vspack` on those devices.
9. Build: `engine/xenon/Makefile` uses `$(DEVKITXENON)/rules` (`MACHDEP`,
   `LIBXENON_INC/LIB`, `app.lds`) and links `-lxenon -lfat -lm`. The `-lfat`
   name should be checked against the installed libxenon.

## Memory

The pack is 27 MB and is read whole into RAM; textures are uploaded as
separate copies (~21 MB). The 360 has 512 MB, fine. The engine's static
pools are a few MB.

## Packaging (XEX/GOD)

Not researched yet. libxenon produces an ELF for XeLL; running it from a
dashboard needs a different path (an XEX wrapper such as the ones used by
other libxenon homebrew). Record the tool and version here when chosen.
