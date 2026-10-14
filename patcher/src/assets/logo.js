'use strict';
// Builds the "VSDEMAKE" title logo from the user's own title.png at pack time.
//
// The original logo spells VAMPIRE / SURVIVORS in gold serif letters with a red
// outline and a soft shadow. V, S, E, M and A are cut out of it as they are; D
// and K do not exist in it and are assembled from other letters' strokes:
//   D = the I's stem + the right half of the O
//   K = the I's stem + the R's leg (lower arm) + the leg mirrored vertically
//       (upper arm)
// Letters are separated by their bright gold cores (which never touch), and
// each outline/shadow pixel goes to the nearest core within 7 px.
//
// The seed points below are positions in the 332x133 title.png of the known
// build (manifest 8292959317163221061); `buildLogo` refuses other sizes.

const LOGO_SRC_W = 332, LOGO_SRC_H = 133;

class Img {
  constructor(w, h, data) { this.w = w; this.h = h; this.data = data || Buffer.alloc(w * h * 4); }
  idx(x, y) { return (y * this.w + x) * 4; }
  alpha(x, y) { return this.data[this.idx(x, y) + 3]; }

  /** Bounding box of pixels with alpha > 0: [x0, y0, x1, y1) or null. */
  bbox() {
    let x0 = this.w, y0 = this.h, x1 = -1, y1 = -1;
    for (let y = 0; y < this.h; y++) {
      for (let x = 0; x < this.w; x++) {
        if (this.alpha(x, y)) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
      }
    }
    return x1 < 0 ? null : [x0, y0, x1 + 1, y1 + 1];
  }

  /** Copy of [x0, y0, x1, y1); outside the image reads as transparent. */
  crop(x0, y0, x1, y1) {
    const out = new Img(x1 - x0, y1 - y0);
    for (let y = y0; y < y1; y++) {
      for (let x = x0; x < x1; x++) {
        if (x < 0 || y < 0 || x >= this.w || y >= this.h) continue;
        this.data.copy(out.data, out.idx(x - x0, y - y0), this.idx(x, y), this.idx(x, y) + 4);
      }
    }
    return out;
  }

  trim() { const b = this.bbox(); return b ? this.crop(...b) : new Img(1, 1); }

  flipVertical() {
    const out = new Img(this.w, this.h);
    for (let y = 0; y < this.h; y++) this.data.copy(out.data, out.idx(0, this.h - 1 - y), this.idx(0, y), this.idx(0, y + 1));
    return out;
  }

  /** Source-over composite of `src` at (dx, dy), straight (un-premultiplied) alpha. */
  over(src, dx, dy) {
    for (let y = 0; y < src.h; y++) {
      for (let x = 0; x < src.w; x++) {
        const tx = x + dx, ty = y + dy;
        if (tx < 0 || ty < 0 || tx >= this.w || ty >= this.h) continue;
        const s = src.idx(x, y), d = this.idx(tx, ty);
        const sa = src.data[s + 3] / 255;
        if (!sa) continue;
        const da = this.data[d + 3] / 255;
        const oa = sa + da * (1 - sa);
        for (let c = 0; c < 3; c++) {
          this.data[d + c] = Math.round((src.data[s + c] * sa + this.data[d + c] * da * (1 - sa)) / oa);
        }
        this.data[d + 3] = Math.round(oa * 255);
      }
    }
  }
}

/** Labels every visible pixel with the index of its letter core (-1 = none). */
function labelCores(img) {
  const { w, h, data } = img;
  const lab = new Int32Array(w * h).fill(-1);
  const isCore = (i) => {
    const r = data[i * 4], g = data[i * 4 + 1], b = data[i * 4 + 2], a = data[i * 4 + 3];
    return a > 200 && r > 180 && g > 90 && r - b > 90;
  };
  const comps = []; // {bbox, size}
  for (let start = 0; start < w * h; start++) {
    if (lab[start] !== -1 || !isCore(start)) continue;
    const id = comps.length, stack = [start], pts = [];
    lab[start] = id;
    while (stack.length) {
      const p = stack.pop(); pts.push(p);
      const x = p % w, y = (p - x) / w;
      for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
        const n = ny * w + nx;
        if (lab[n] === -1 && isCore(n)) { lab[n] = id; stack.push(n); }
      }
    }
    let x0 = w, y0 = h, x1 = 0, y1 = 0;
    for (const p of pts) { const x = p % w, y = (p - x) / w; x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y); }
    comps.push({ bbox: [x0, y0, x1, y1], size: pts.length });
    if (pts.length <= 30) for (const p of pts) lab[p] = -1; // specks
  }
  // Grow each core over the outline and shadow, up to 7 px (8-connected).
  const dist = new Uint8Array(w * h);
  let queue = [];
  for (let i = 0; i < w * h; i++) if (lab[i] >= 0) queue.push(i);
  for (let head = 0; head < queue.length; head++) {
    const p = queue[head];
    if (dist[p] >= 7) continue;
    const x = p % w, y = (p - x) / w;
    for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [1, -1], [-1, 1], [-1, -1]]) {
      const nx = x + dx, ny = y + dy;
      if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
      const n = ny * w + nx;
      if (lab[n] === -1 && data[n * 4 + 3] > 12) { lab[n] = lab[p]; dist[n] = dist[p] + 1; queue.push(n); }
    }
  }
  queue = null;
  const at = (x, y) => {
    const i = comps.findIndex((c) => c.size > 30 && c.bbox[0] <= x && x <= c.bbox[2] && c.bbox[1] <= y && y <= c.bbox[3]);
    if (i < 0) throw new Error(`title logo: no letter stroke at ${x},${y} (different title.png?)`);
    return i;
  };
  return { lab, at };
}

/**
 * @param src {width, height, data} RGBA8 (pngjs) of the original title.png
 * @returns {width, height, data} RGBA8 of the VSDEMAKE logo
 */
function buildLogo(src) {
  if (src.width !== LOGO_SRC_W || src.height !== LOGO_SRC_H) throw new Error(`title logo: expected ${LOGO_SRC_W}x${LOGO_SRC_H}, got ${src.width}x${src.height}`);
  const img = new Img(src.width, src.height, Buffer.from(src.data));
  const { lab, at } = labelCores(img);

  const select = (ids, clip = () => true) => {
    const set = new Set(ids), out = new Img(img.w, img.h);
    for (let y = 0; y < img.h; y++) {
      for (let x = 0; x < img.w; x++) {
        if (set.has(lab[y * img.w + x]) && clip(x, y)) img.data.copy(out.data, out.idx(x, y), img.idx(x, y), img.idx(x, y) + 4);
      }
    }
    return out;
  };

  const V = select([at(60, 30)]).trim();
  const A = select([at(90, 30)]).trim();
  const M = select([at(130, 30), at(155, 30)]).trim();
  const E = select([at(262, 30), at(275, 50)]).trim();
  const S = select([at(25, 100)]).trim();
  const iFull = select([at(208, 30)]);
  const iBox = iFull.bbox();
  const I = iFull.trim();

  // D: stem + right half of the O
  const oIds = [at(220, 100), at(240, 100)];
  const oBox = select(oIds).bbox();
  const oMid = Math.floor((oBox[0] + oBox[2]) / 2);
  const oHalf = select(oIds, (x) => x >= oMid - 1).trim();
  const dCanvas = new Img(70, img.h);
  dCanvas.over(I, 0, 10);
  dCanvas.over(oHalf, I.w - 12, 10);
  const D = dCanvas.trim();

  // K: stem + R's leg below the bowl junction + the leg mirrored above it
  const rBox = select([at(225, 30), at(240, 30)]).bbox();
  const JUNCTION_Y = 36, LEG_X = 230;
  const leg = select([at(240, 30)], (x, y) => x >= LEG_X && y >= JUNCTION_Y).crop(LEG_X - 3, JUNCTION_Y, rBox[2], rBox[3]);
  const arm = leg.flipVertical();
  const kCanvas = new Img(60, img.h);
  kCanvas.over(I, 0, iBox[1]);
  const armX = I.w - 20;
  kCanvas.over(arm, armX, JUNCTION_Y - arm.h);
  kCanvas.over(leg, armX, JUNCTION_Y);
  const K = kCanvas.trim();

  const word = [V, S, D, E, M, A, K, E];
  const GAP = -2;
  const width = word.reduce((s, l) => s + l.w, 0) + GAP * (word.length - 1);
  const height = Math.max(...word.map((l) => l.h));
  const logo = new Img(width, height);
  let x = 0;
  for (const l of word) { logo.over(l, x, height - l.h); x += l.w + GAP; }
  return { width, height, data: logo.data };
}

module.exports = { buildLogo };
