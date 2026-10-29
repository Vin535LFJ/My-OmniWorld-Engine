#!/usr/bin/env node
// png_to_rgb.mjs — decode a non-interlaced 8-bit PNG into the raw RGB8 layout
// the Observation contract uses (W*H*3, row-major), so a real photograph can be
// handed to a perception adapter. The slice13 real-photo check reads such a file
// through OW_RTDETR_TEST_IMAGE; the file itself is NOT in git (models/ is
// ignored, and photo licensing is the owner's call) -- regenerate it from any
// 8-bit RGB/RGBA PNG with this tool.
//
// Usage:
//   node tools/png_to_rgb.mjs <input.png> <output.rgb>
//   node tools/png_to_rgb.mjs models/photo_source.png models/photo_640x480.rgb
import { readFileSync, writeFileSync } from 'node:fs';
import { inflateSync } from 'node:zlib';

const [, , src, dst] = process.argv;
if (!src || !dst) {
  console.error('usage: node tools/png_to_rgb.mjs <input.png> <output.rgb>');
  process.exit(2);
}

const buf = readFileSync(src);
if (buf.readUInt32BE(0) !== 0x89504e47) throw new Error(`${src}: not a PNG`);

let pos = 8;
let width = 0;
let height = 0;
let depth = 0;
let color = 0;
let interlace = 0;
const idat = [];
while (pos < buf.length) {
  const len = buf.readUInt32BE(pos);
  const type = buf.toString('ascii', pos + 4, pos + 8);
  const data = buf.subarray(pos + 8, pos + 8 + len);
  if (type === 'IHDR') {
    width = data.readUInt32BE(0);
    height = data.readUInt32BE(4);
    depth = data[8];
    color = data[9];
    interlace = data[12];
  } else if (type === 'IDAT') {
    idat.push(data);
  } else if (type === 'IEND') {
    break;
  }
  pos += 12 + len;
}

// Palettes, 16-bit channels and Adam7 interlacing are out of scope: the test
// path only needs the 8-bit RGB/RGBA case, and failing loudly beats silently
// producing a mislabelled buffer.
if (depth !== 8 || interlace !== 0 || (color !== 2 && color !== 6)) {
  throw new Error(
    `${src}: unsupported PNG (depth=${depth} color=${color} interlace=${interlace})`,
  );
}

const channels = color === 6 ? 4 : 3;
const raw = inflateSync(Buffer.concat(idat));
const stride = width * channels;
const out = Buffer.alloc(width * height * 3);
let prev = Buffer.alloc(stride);
for (let y = 0; y < height; ++y) {
  const filter = raw[y * (stride + 1)];
  const line = Buffer.from(
    raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1)),
  );
  // Reverse the per-scanline PNG filters (None/Sub/Up/Average/Paeth).
  for (let i = 0; i < stride; ++i) {
    const a = i >= channels ? line[i - channels] : 0;
    const b = prev[i];
    const c = i >= channels ? prev[i - channels] : 0;
    let v = line[i];
    if (filter === 1) v += a;
    else if (filter === 2) v += b;
    else if (filter === 3) v += (a + b) >> 1;
    else if (filter === 4) {
      const p = a + b - c;
      const pa = Math.abs(p - a);
      const pb = Math.abs(p - b);
      const pc = Math.abs(p - c);
      v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
    }
    line[i] = v & 0xff;
  }
  for (let x = 0; x < width; ++x) {
    out[(y * width + x) * 3 + 0] = line[x * channels + 0];
    out[(y * width + x) * 3 + 1] = line[x * channels + 1];
    out[(y * width + x) * 3 + 2] = line[x * channels + 2];
  }
  prev = line;
}

writeFileSync(dst, out);
console.log(`${src} -> ${dst} ${width}x${height} color=${color} (${out.length} B)`);
