# Depot report: Vampire Survivors 0.2.10 (manifest 8292959317163221061)

Written during the cloud session of 2026-10-13 from the files the user
uploaded (`main.bundle.js` + `assets/`). The original recon report from the
local machine was not available, so this is a fresh one. Line numbers refer to
`work/main.deob.js` as produced by `vspatch deob` (line-broken after `;{}`).

## 1. Files

```
resources/app/.webpack/renderer/
├── main.bundle.js        899,336 B   game code + data, obfuscator.io string-array scheme
└── assets/               (69 files, 8.5 MB)
    ├── img/              TexturePacker atlases (PNG + JSON hash format): UI, characters,
    │                     enemies, items, vfx, illustrations, angel, randomazzo;
    │                     plain PNGs: title, introBG, gameOver, stageComplete,
    │                     bg_forest, bg_green, bgTile1024B, hit1
    ├── tilesets/         Tiled JSON maps Forest.json, Library.json + packed tilesets
    │                     (ForestTexturePacked, LibraryTexturePacked .png/.json)
    ├── fonts/            damNum-export.png/.xml (BMFont, damage digits)
    ├── lang/             lang, weaponLang, itemLang, powerupLang, stageLang,
    │                     achievementLang (.json)
    └── sfx/              32 OGG files (2 BGM tracks + title + effects)
```

Hashes of all 70 files: `patcher/known-builds/8292959317163221061.sha256`.
The bundle's sha256 identifies the build; asset hashes detect tampering.

## 2. Obfuscation

- Array function `a0_0x17b3` (1958 strings), decoder `a0_0x16bb`, index
  offset 332 (0x14c), rotation target 0xd2cd0, rotation found at 230 shifts.
- 1249 local aliases of the decoder (`var x = a0_0x16bb`), 19,383 call sites,
  all resolved. No base64/RC4 variant.
- Webpack module map object with numeric keys (`{0x156: (module, exports, require) => …}`).

## 3. Data layout

TypeScript enums compile to `!function(p){p["A"]='A', …}(h||(h={}))`; the
tables are object literals keyed by `[alias["MEMBER"]]`. 16 enums found.
Per-level tables are arrays whose first element is the full definition and
the following elements are deltas applied on level-up.

| Table | Key enum (first member) | Line | Entries | Notes |
| --- | --- | --- | --- | --- |
| characters | VOID/ANTONIO | 71 | 40 | 13 have real stats, rest are placeholders/hidden; `POPPEA` key duplicated |
| pickups | VOID/GEM | 502 | 17 | |
| weapons | VOID/WHIP | 837 | 41 | 27 weapons (9 evolutions) + 14 passives (`isPowerUp`) |
| powerups | MIGHT (value `POWER`) | 1692 | 14 | shop ranks |
| enemies | BAT1 | 2035 | 52 | 3 entries keyed by non-existent members `MEDUSA1_FAST`, `MEDUSA2_FAST`, `BOSS_XLDETH` (dropped, logged); `BOSS_XLDEATH` duplicated (last wins) |
| destructibles | CANDLE/BRAZIER | 2162 | 4 | |
| achievements | ReachLV5 | 2181 | 40 | |
| stages | FOREST | 2299 | 4 | minute rows 31/1/31/31 (FOREST, SINKING, LIBRARY, GREENACRES); row 0 holds the stage header |
| hitvfx | NONE | 4427 | 8 | hit tint/VFX per hit type |

Enum `MIGHT` has value `'POWER'`, so the stat key in the data is `POWER`.
Stage LIBRARY events reference `MEDUSA1_FAST`/`MEDUSA2_FAST` in `moreY`
(6 places): those evaluate to `undefined` in the original, i.e. the field is
effectively unset. Treat as "no override".

## 4. Constants

Static class fields assigned at line 6752 (holder `_0x35b6c4`, alias `_0x4bf3d4`):

| Name | Value |
| --- | --- |
| _basePlayerPxSpeed / PlayerPxSpeed | 82.5 |
| _baseEnemySpeed / EnemySpeed | 0.77 × 0.3 = 0.231 |
| _baseProjectileSpeed / ProjectileSpeed | 165 |
| GoldMultiplier, EnemyHealthMultiplier | 1 |
| PixelScale, RPixelScale | 1 |
| ZDamageNumber | MAX_SAFE_INTEGER − 10000 |
| ZInGameUI | MAX_SAFE_INTEGER − 1000 |

The non-`_base` values are reset at runtime from the stage `mods`/`hyper`
multipliers.

## 5. Still to do in recon

- `.webpack/main/index.js` (Electron main process, save location) was not
  uploaded; needed for `notes/behaviour/meta.md`.
- Enum-to-id mapping for the pack format: use the enum member order from
  `work/out/data/enums.json`.
