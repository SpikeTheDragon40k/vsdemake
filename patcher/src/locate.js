'use strict';
// Find the game files in whatever the user pointed us at, and check them
// against the hash list of the supported build.

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

class LocateError extends Error {}

const BUNDLE = '.webpack/renderer/main.bundle.js';
const ASSETS = '.webpack/renderer/assets';
const KNOWN_BUILDS_DIR = path.join(__dirname, '..', 'known-builds');

/**
 * A source abstracts over an unpacked `resources/app/` directory and a
 * packed `resources/app.asar`; both expose the same relative paths.
 */
class DirSource {
  constructor(root) { this.root = root; this.kind = 'dir'; }
  read(rel) { return fs.readFileSync(path.join(this.root, rel)); }
  exists(rel) { return fs.existsSync(path.join(this.root, rel)); }
  list(relDir) {
    const out = [];
    const rec = (d) => {
      for (const e of fs.readdirSync(path.join(this.root, d), { withFileTypes: true })) {
        const p = path.posix.join(d, e.name);
        if (e.isDirectory()) rec(p); else out.push(p);
      }
    };
    if (this.exists(relDir)) rec(relDir);
    return out.sort();
  }
}

class AsarSource {
  constructor(file) {
    this.file = file; this.kind = 'asar';
    this.asar = require('@electron/asar');
    this.files = this.asar.listPackage(file).map((f) => f.replace(/^[\\/]/, '').split(path.sep).join('/'));
  }
  read(rel) { return this.asar.extractFile(this.file, rel); }
  exists(rel) { return this.files.includes(rel); }
  list(relDir) { return this.files.filter((f) => f.startsWith(relDir + '/')).sort(); }
}

/**
 * Accepts: the depot folder, `resources/`, `resources/app/`, `resources/app.asar`,
 * or the `.webpack/renderer/` folder itself.
 */
function openSource(userPath) {
  const p = path.resolve(userPath);
  if (!fs.existsSync(p)) throw new LocateError(`path does not exist: ${p}`);
  const candidates = fs.statSync(p).isDirectory()
    ? [p, path.join(p, 'resources', 'app'), path.join(p, 'app'), path.join(p, 'resources', 'app.asar'), path.join(p, 'app.asar'), path.join(p, '..', '..')]
    : [p];
  for (const c of candidates) {
    if (!fs.existsSync(c)) continue;
    if (fs.statSync(c).isFile() && c.endsWith('.asar')) {
      const s = new AsarSource(c);
      if (s.exists(BUNDLE)) return s;
    } else if (fs.statSync(c).isDirectory() && fs.existsSync(path.join(c, BUNDLE))) {
      return new DirSource(c);
    }
  }
  throw new LocateError(`could not find ${BUNDLE} under ${p}. Point to the depot folder, to resources/app or to resources/app.asar.`);
}

function sha256(buf) { return crypto.createHash('sha256').update(buf).digest('hex'); }

function loadKnownBuilds() {
  const builds = [];
  for (const f of fs.readdirSync(KNOWN_BUILDS_DIR)) {
    if (!f.endsWith('.sha256')) continue;
    const hashes = new Map();
    for (const line of fs.readFileSync(path.join(KNOWN_BUILDS_DIR, f), 'utf8').split('\n')) {
      const m = /^([0-9a-f]{64})\s+\*?(.+)$/.exec(line.trim());
      if (m) hashes.set(m[2].replace(/^\.\//, ''), m[1]);
    }
    builds.push({ manifest: path.basename(f, '.sha256'), hashes });
  }
  return builds;
}

/**
 * Hash every file the patcher reads and compare with the known builds.
 * @returns {{ manifest: string|null, status: 'ok'|'modified'|'unknown', files: number, mismatches: string[], missing: string[], extra: string[], bundleHash: string }}
 */
function verify(source) {
  const files = [BUNDLE, ...source.list(ASSETS)];
  const actual = new Map(files.map((f) => [f, sha256(source.read(f))]));
  const builds = loadKnownBuilds();
  const bundleHash = actual.get(BUNDLE);

  // The bundle hash identifies the build; the asset hashes tell tampering apart.
  const build = builds.find((b) => b.hashes.get(BUNDLE) === bundleHash);
  if (!build) return { manifest: null, status: 'unknown', files: files.length, mismatches: [], missing: [], extra: [], bundleHash };

  const mismatches = [], missing = [], extra = [];
  for (const [f, h] of build.hashes) {
    if (!actual.has(f)) missing.push(f);
    else if (actual.get(f) !== h) mismatches.push(f);
  }
  for (const f of actual.keys()) if (!build.hashes.has(f)) extra.push(f);
  const status = mismatches.length || missing.length ? 'modified' : 'ok';
  return { manifest: build.manifest, status, files: files.length, mismatches, missing, extra, bundleHash };
}

module.exports = { openSource, verify, loadKnownBuilds, LocateError, BUNDLE, ASSETS };
