# Audio: work plan

Status: planned, not started (2026-10-14). Saves and unlocks are done and
confirmed on the console; audio is the next feature.

## What the original has (facts gathered so far)

- 31 OGG Vorbis files in `assets/sfx/`, 5 MB total, 44.1 kHz, mostly mono.
  Music: `VS_Music_v04-01.ogg` (Mad Forest, 74 s, mono),
  `bgm_elrond_library.ogg` (Inlaid Library, 91 s, stereo),
  `bgm_library.ogg` (legacy library track), title `VS_TitleIntro_v01-03.ogg`,
  game over `VS_GameOver_v02-01.ogg`.
- Sound keys → files: the preload list in `work/main.deob.js` (search
  `load"]["audio"]`), e.g. Whip → `sfx_sounds_impact15_2`, Shot →
  `VS_Projectile_v06-02`, Gem → `VS_Gem_v04-01-B`, LevelUp, Treasure1–3,
  Roast, Rosary, Orologion, Vacuum, Lightning, Holywater, MagicMissile, Alert,
  Fireloop, ClickIn/ClickOut.
- `PlaySound(key, config, delay, maxInstances)` (line 7212): does nothing at
  sound volume 0; if `delay > 0` and `maxInstances > 0`, refuses to play
  when `maxInstances` copies started within the last `delay` ms; volume =
  `config.volume × 0.5` (or 0.1 when no volume is given) × the sound-volume
  option (default 0.5); `config` may also carry `detune` (cents) and `rate`.
- 87 `PlaySound` call sites (weapons, pickups, gems with a 24-step detune
  melody, level-up, chests, UI clicks); music through a separate library
  with its own volume (`UpdateMusicVolume`).

## Steps

1. **Behaviour note** `notes/behaviour/audio.md` (clean room): the key→file
   table; `PlaySound` rules; every call site as a table (event, key,
   volume, detune, rate, delay/maxInstances); music: which track per stage
   and screen, looping, volume, fades, what stops it; option defaults.
2. **Patcher:** add the OGG files to the pack unchanged as `SND` entries
   named by sound key (kind already reserved in `notes/pack-format.md`);
   music too. No decoder in Node. Update pack-format, tests, entry counts
   (packs must be rebuilt).
3. **Engine mixer** (`engine/src/core/vs_audio.c`): vendor stb_vorbis
   (public domain, single file) under `engine/src/third_party/`; output
   44.1 kHz stereo 16-bit; ~32 voices with volume, pitch
   (`2^(cents/1200) × rate`, linear resampling); sound effects decoded once
   at start-up (they are short), music decoded while playing from the OGG in
   the pack, looped; the `delay/maxInstances` throttle; sound and music
   volumes stored in the save file.
4. **Platform output:** `vs_plat_audio_open(rate, mix callback)`. PC: SDL2
   audio callback. Xbox (XDK): XAudio2 source voice fed with small buffers
   (3 × ~1024 frames queued), from the frame loop or a worker thread.
   libxenon fallback: `xenon_sound` submit, lowest priority.
5. **Hook the call sites** in the game code following `audio.md`; music on
   title, stage start, game over.
6. **Testing:** a `--audio-dump out.wav` option renders the mixed output of
   a headless run, so mixing can be checked without speakers; then PC
   listening; then the console.
7. **Docs:** README, Xbox guide (no setup expected), progress log.

Rough size: steps 1–2 one session; 3, 4 (PC) and 5 one session; 4 (XDK)
plus the console test one session.

## Before starting in a new session

- Push `dev` to GitHub first (this container is temporary), and start the
  new session from the repository.
- The game files are not in the repository. Upload `main.bundle.js` and the
  `assets/` folder from the depot again (into `vs-1.0/resources/app/.webpack/renderer/`),
  then run `node patcher/bin/vspatch.js deob vs-1.0 --out work/main.deob.js`
  and build a pack, as in the earlier sessions.
- Commits: author Adriano Inghingolo
  `<68734231+SpikeTheDragon40k@users.noreply.github.com>`, no co-author
  lines, on the `dev` branch.
