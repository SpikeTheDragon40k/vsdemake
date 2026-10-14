'use strict';
// Sound files → SND blobs (notes/pack-format.md, notes/behaviour/audio.md).
//
// The OGG Vorbis files are stored unchanged; the engine decodes them. Each
// entry is named by the sound key the game uses for it (e.g. "Gem",
// "BGM_Forest"), read from the preload list in the de-obfuscated bundle:
// load.audio(<enum>["Key"], ['assets/sfx/<file>.ogg']). The enum values
// equal their member names for these enums (checked in the tests).

const SND_OGG = 1;

/** file basename (without .ogg) → sound key, from the preload calls. */
function findSoundKeys(code) {
  const out = {};
  const re = /\[\s*["']audio["']\s*\]\s*\(\s*[\w$]+\s*\[\s*["']([A-Za-z0-9_]+)["']\s*\]\s*,\s*\[\s*["']assets\/sfx\/([^"']+)\.ogg["']\s*\]\s*\)/g;
  let m;
  while ((m = re.exec(code)) !== null) out[m[2]] = m[1];
  return out;
}

/** SND blob: u32 format (1 = OGG Vorbis), u32 byte size, 2 × u32 reserved, then the file. */
function soundBlob(bytes) {
  const head = Buffer.alloc(16);
  head.writeUInt32BE(SND_OGG, 0);
  head.writeUInt32BE(bytes.length, 4);
  return Buffer.concat([head, bytes]);
}

module.exports = { findSoundKeys, soundBlob, SND_OGG };
