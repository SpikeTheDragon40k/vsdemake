# Data pack format (`vsdata.vspack`), version 1

Produced by `vspatch build` (`patcher/src/pack.js`), read by
`engine/src/core/vs_pack.c`. Everything is
**big-endian** and 16-byte aligned so the console reads it without swapping.
All offsets are from the start of the file.

## Header (32 bytes)

| Offset | Type | Value |
| --- | --- | --- |
| 0 | char[4] | `VSPK` |
| 4 | u32 | format version = 1 |
| 8 | u64 | source Steam manifest id (8292959317163221061) |
| 16 | u32 | number of directory entries |
| 20 | u32 | directory offset |
| 24 | u32 | total file size |
| 28 | u32 | flags: bit 0 set = payload is big-endian (always 1) |

## Directory entry (48 bytes each)

| Offset | Type | Value |
| --- | --- | --- |
| 0 | char[4] | kind: `TEX`, `ATL`, `MAP`, `FNT`, `TAB`, `ENM`, `SND` |
| 4 | u32 | reserved (0) |
| 8 | char[32] | name, NUL-padded (`enemies`, `Forest`, `weapons`, …) |
| 40 | u32 | blob offset |
| 44 | u32 | blob size |

Names are unique per kind. Lookups are `(kind, name)`.

## `TEX`: texture

```
u32 width        (power of two)
u32 height       (power of two)
u32 format       1 = ARGB8 (one u32 per pixel, A in the high byte)
u32 usedWidth    original image width
u32 usedHeight   original image height
u32 reserved     0
u32 pixels[width*height]   row-major, big-endian words  (header = 24 bytes)
```

Pad pixels are transparent black. Textures are never resized, only padded,
so atlas frame rectangles stay valid. PC loader byte-swaps once into an
SDL texture; the console backends upload the words as is.

One TEX is not a copy of a game file: `title_vsdemake`, the demake's
"VSDEMAKE" logo, assembled by `patcher/src/assets/logo.js` from letters of
the user's `img/title.png` (D and K built from the I, O and R strokes). The
engine falls back to `title` when a pack lacks it.

## `ATL`: TexturePacker atlas → frame table

```
u32 textureEntry        directory index of the TEX
u32 frameCount
u32 stringsOffset       offset (from blob start) of the string table
u32 stringsSize
Frame frames[frameCount], sorted by nameHash ascending:
  u32 nameHash          FNV-1a 32-bit of the frame name (e.g. "Antonio_01.png")
  u32 nameOffset        into the string table (NUL-terminated)
  u16 x, y, w, h        rectangle in the texture (trimmed frame)
  i16 offX, offY        trimmed frame's offset inside the source sprite
  u16 srcW, srcH        source sprite size (untrimmed)
                        (frames with "trimmed": false get offX = offY = 0
                        and srcW/srcH = w/h, as Phaser ignores their
                        spriteSourceSize; ~1,200 such frames carry stale
                        offsets in this build. The engine applies the same
                        rule to packs built before the fix.)
  u16 rotated (0/1), u16 pad
```

## `MAP`: Tiled map

```
u32 widthTiles, heightTiles
u32 tileW, tileH
u32 tilesetTexture      directory index of the TEX
u32 firstGid, columns, margin, spacing, tileCount
u32 layerCount, objectGroupCount
u32 collidesOffset      u8[tileCount], 1 if the tile has collides=true
u32 layersOffset, objectGroupsOffset, reserved    (header = 64 bytes)
Layer (at layersOffset, each):
  char[32] name
  u32 depth (i32)       draw depth from notes/behaviour/stage-map.md
  u32 flags             bit0 collision layer, bit1 hidden in original,
                        bit2 collides for the player only (PlayerWall)
  u32 gids[width*height]   raw Tiled gid incl. flip bits (0 = empty)
ObjectGroup (each):
  char[32] name
  u32 objectCount
  Object (each):
    char[32] name
    char[32] type
    f32 x, y, w, h
    u32 propertyCount
    Property (each): char[32] name, f32 value   (only numeric props kept)
```

## `FNT`: bitmap font

```
u32 textureEntry
u32 lineHeight
u32 glyphCount, reserved                          (header = 16 bytes)
Glyph (each): u32 codepoint; u16 x, y, w, h; i16 xoff, yoff, xadvance, pad
```

## `ENM`: enum

```
u32 memberCount
u32 stringsOffset, stringsSize, reserved           (header = 16 bytes)
Member (each): u32 nameOffset, u32 valueOffset (string value), i32 index
```

Member order is the declaration order in the game, so `index` is the stable
id used by `TAB` records (`enum:` fields).

## `TAB`: data table

Game tables (weapons, characters, enemies, stages, …) are stored as
fixed-size records with a presence mask, so sparse per-level deltas keep
their "field absent" meaning.

```
u32 fieldCount
u32 recordSize          = 8 + 4*fieldCount
u32 groupCount
u32 recordCount
u32 fieldsOffset, groupsOffset, recordsOffset, poolOffset, poolSize, stringsOffset, stringsSize
(header padded to 48 bytes; each section 16-byte aligned)
Field (each): char[32] name, u32 type
  type: 1 f32, 2 i32, 3 bool(i32 0/1), 4 string (u32 offset), 5 enum (i32 index, -1 = null),
        6 list (u32 pool offset; pool holds u32 count then count × u32 items),
        7 subtable (u32 pool offset; pool holds u32 count then count × record index
          into the sub-table named "<table>.<field>")
Group (each): u32 keyOffset (string), i32 keyEnumIndex, u32 firstRecord, u32 recordCount
Record (each): u64 presence (bit i = field i present), then fieldCount × 4 bytes
```

The field lists live in `patcher/src/tables.js` and generate
`engine/src/core/vs_tables.h` (C structs with the same layout) with
`node patcher/tools/gen-c-header.js`. Both are committed; the pack carries
the field names too, so a mismatch is detected at load.

`.json` tables map to groups: `weapons.json` → groups `WHIP`, `AXE`, … each
with one record per level entry; `enemies.json` → one record per group;
`stages.json` → one record per minute row, with `events` and `treasure`
in sub-tables `stages.events`, `stages.treasure`.

## `SND`: audio (planned)

PCM 16-bit big-endian mono/stereo at the source rate. Not produced yet.
