// =====================================================================
//  _diag_page.js  -  run the settings page's picture JavaScript off a browser
//
//  The upload part of src/netconfig.cpp scales the picked picture on a canvas
//  and packs it into 160x128 RGB565. That code only ever runs on a phone, where
//  a typo stays invisible until the pet shows a wrong picture - so this pulls
//  the <script> block straight out of netconfig.cpp, runs it against a small
//  canvas/DOM stub and checks what would reach the panel:
//
//    * the geometry: cover fills the panel, contain letterboxes it,
//    * the byte order: RGB565 with the low byte first, the layout splash_map[]
//      has in the pet's flash,
//    * the letterbox colour: it has to be the 0x0842 the PC side
//      (tools/gen_splash.ps1) pads with, so both paths agree.
//
//  Run:  node tools/_diag_page.js
// =====================================================================
'use strict';
const fs   = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const src  = fs.readFileSync(path.join(root, 'src', 'netconfig.cpp'), 'utf8');

const OPEN = '"<script>\\n"', CLOSE = '"</script>"';
const a = src.indexOf(OPEN), b = src.indexOf(CLOSE);
if (a < 0 || b < 0) { console.log('FAIL: no <script> block in src/netconfig.cpp'); process.exit(1); }

// The block is a run of C string literals (with C++ comments in between), so
// every "..." in it is one piece of JavaScript.
let js = '', m;
const re = /"((?:[^"\\]|\\.)*)"/g;
const block = src.slice(a + OPEN.length, b);
while ((m = re.exec(block))) {
  js += m[1].replace(/\\n/g, '\n').replace(/\\"/g, '"').replace(/\\\\/g, '\\') + '\n';
}

// ------------------------------------------------------------ browser stubs
const W = 160, H = 128;

function hex2rgb(s) {
  return [parseInt(s.substr(1, 2), 16), parseInt(s.substr(3, 2), 16), parseInt(s.substr(5, 2), 16)];
}

// A canvas that really stores pixels: fillRect, a nearest neighbour drawImage
// (good enough to see *where* something was drawn) and getImageData.
function canvas(w, h) {
  const c = { width: w, height: h, buf: null };
  const px = () => {
    if (!c.buf || c.buf.length !== c.width * c.height * 4) c.buf = new Uint8ClampedArray(c.width * c.height * 4);
    return c.buf;
  };
  const at = (x, y) => {                       // what a source picture looks like
    if (c.src) return c.src(x, y);             // ... a plain image stub
    const i = (y * c.width + x) * 4, b = px();
    return [b[i], b[i + 1], b[i + 2], b[i + 3]];
  };
  c.at = at;
  const ctx = {
    imageSmoothingQuality: '', fillStyle: '#000',
    fillRect(x, y, w2, h2) {
      const [r, g, bl] = hex2rgb(ctx.fillStyle), b = px();
      for (let yy = 0; yy < h2; yy++)
        for (let xx = 0; xx < w2; xx++) {
          const i = ((y + yy) * c.width + x + xx) * 4;
          b[i] = r; b[i + 1] = g; b[i + 2] = bl; b[i + 3] = 255;
        }
    },
    drawImage(s, dx, dy, dw, dh) {
      ctx.drawn = [dx, dy, dw, dh];
      const b = px();
      for (let y = 0; y < dh; y++)
        for (let x = 0; x < dw; x++) {
          const [r, g, bl] = s.at(Math.floor(x * s.width / dw), Math.floor(y * s.height / dh));
          const i = ((dy + y) * c.width + dx + x) * 4;
          b[i] = r; b[i + 1] = g; b[i + 2] = bl; b[i + 3] = 255;
        }
    },
    getImageData() { return { data: px() }; },
  };
  c.getContext = () => ctx;
  return c;
}

const main = canvas(W, H);
const els = { cv: main, st: {}, up: {} };
const doc = {
  getElementById: id => els[id] || (els[id] = {}),
  createElement: () => canvas(1, 1),
  querySelectorAll: () => [{}, {}],
  querySelector: () => ({ value: doc._fit }),
  _fit: 'cover',
};

const api = new Function('document', 'URL', 'XMLHttpRequest', 'FormData', 'Blob', 'Image',
                         js + '\nreturn { shrink: shrink, fit: fit };')(
  doc,
  { createObjectURL: () => 'blob:x', revokeObjectURL: () => {} },
  function () { this.open = this.send = () => {}; },
  function () { this.append = () => {}; },
  function () {},
  function () {}
);

// --------------------------------------------------------------- the checks
let bad = 0;
const check = (name, ok, extra) => {
  console.log((ok ? 'PASS  ' : 'FAIL  ') + name + (extra === undefined ? '' : '   ' + extra));
  if (!ok) bad++;
};

const solid = (w, h, rgb) => ({ width: w, height: h, at: () => rgb });
const image = (w, h, f) => ({ width: w, height: h, at: (x, y) => f(x, y, w, h) });
const drawn = () => main.getContext('2d').drawn;
const pixels = () => main.getContext('2d').getImageData(0, 0, W, H).data;

// 1. size and byte order, with a picture that is one flat colour
doc._fit = 'cover';
let dat = api.shrink(solid(800, 600, [255, 0, 0]));            // pure red -> 0xF800
check('a picture comes out as 160*128*2 bytes', dat.length === W * H * 2, dat.length + ' B');
check('pure red packs to 0xF800, low byte first', dat[0] === 0x00 && dat[1] === 0xF8,
      '0x' + dat[1].toString(16) + dat[0].toString(16));

// 2. every byte has to match the pixels the canvas ended up with
let same = true;
{
  const p = pixels();
  for (let i = 0, j = 0; i < p.length; i += 4, j += 2) {
    const v = ((p[i] & 0xF8) << 8) | ((p[i + 1] & 0xFC) << 3) | (p[i + 2] >> 3);
    if (dat[j] !== (v & 0xFF) || dat[j + 1] !== ((v >> 8) & 0xFF)) { same = false; break; }
  }
}
check('the packed bytes are the canvas, pixel for pixel', same);

// 3. geometry: a 4000x3000 photo covers the whole panel
doc._fit = 'cover';
api.shrink(image(4000, 3000, () => [255, 255, 255, 255]));
check('cover scales past the panel and centres it',
      drawn()[2] >= W && drawn()[3] >= H && drawn()[2] === 171 && drawn()[3] === 128 &&
      drawn()[0] === Math.round((W - drawn()[2]) / 2),
      'dx=' + drawn()[0] + ' dw=' + drawn()[2] + ' dh=' + drawn()[3]);

// 4. contain leaves the backdrop visible, and that backdrop has to be the same
//    colour the PC side pads with (COL_BG 8,10,20 -> 0x0842 in splash_data.cpp)
doc._fit = 'contain';
dat = api.shrink(solid(4000, 3000, [255, 255, 255]));
check('contain letterboxes on the UI backdrop (0x0842, like gen_splash.ps1)',
      dat[0] === 0x42 && dat[1] === 0x08, '0x' + dat[1].toString(16) + dat[0].toString(16));
check('contain keeps the whole picture, 32 rows of backdrop',
      drawn()[2] === W && drawn()[3] === 120, 'dw=' + drawn()[2] + ' dh=' + drawn()[3]);

// 5. a tall picture is the mirror case: cover fits the width and trims the top
//    and bottom, while the flat colour checks still have to survive
doc._fit = 'cover';
api.shrink(image(600, 4000, () => [0, 255, 0, 255]));
check('cover on a tall picture fits the width, trims top/bottom',
      drawn()[2] === W && drawn()[3] > H, 'dw=' + drawn()[2] + ' dh=' + drawn()[3]);
dat = api.shrink(solid(600, 600, [0, 255, 0]));
check('green survives the round trip (0x07E0, low byte first)',
      dat[0] === 0xE0 && dat[1] === 0x07, '0x' + dat[1].toString(16) + dat[0].toString(16));

// 6. a picture smaller than the panel is scaled up, never left tiny
doc._fit = 'cover';
api.shrink(image(40, 32, () => [1, 2, 3, 255]));
check('a small picture is scaled up to the panel', drawn()[2] === 160 && drawn()[3] === 128,
      'dw=' + drawn()[2] + ' dh=' + drawn()[3]);

console.log(bad ? '\n' + bad + ' check(s) FAILED' : '\nall checks passed');
process.exit(bad ? 1 : 0);
