#!/usr/bin/env node
// coco_to_panel.mjs — turn the fetched COCO subset into a runnable panel:
//   data/panel/frames.txt   frame-sequence manifest (slice D2 format)
//   data/panel/truth.txt    annotation manifest (slice D3 format)
//   data/panel/panel_stats.json   what the panel is made of, for the report
//
// Division of labor, deliberately: this tool does *data plumbing* (decode,
// resize, normalize, emit the declared category table) and the engine does the
// *protocol* (crowd / degenerate / cap filtering, in annotation.cpp). Filtering
// here instead would hide the protocol inside a script and leave the artifact with
// no counters -- which is exactly how "the annotation did not say" gets
// misread as "the model is wrong" (L1 sec 7 R-D4).
//
// Crowd boxes are written with flag 1 on purpose: real COCO has them, and a
// panel where rule 1 never fires proves nothing about rule 1.
//
// Usage: node tools/coco_to_panel.mjs [--width 640] [--height 480]
import { createHash } from 'node:crypto';
import { readFileSync, rmSync } from 'node:fs';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const DATA = join(ROOT, 'data');
const PANEL = join(DATA, 'panel');

const wIdx = process.argv.indexOf('--width');
const hIdx = process.argv.indexOf('--height');
const width = wIdx > 0 ? Number(process.argv[wIdx + 1]) : 640;
const height = hIdx > 0 ? Number(process.argv[hIdx + 1]) : 480;
if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1) {
  throw new Error('--width/--height must be positive integers');
}

const pinned = JSON.parse(await readFile(join(ROOT, 'tools', 'fetch_coco_subset.manifest.json'), 'utf8'));
const coco = JSON.parse(await readFile(join(DATA, 'coco', 'instances_val2017.json'), 'utf8'));
const annById = new Map(coco.annotations.map((a) => [a.id, a]));
// The manifest tokenizes on whitespace, so a class name must be one token.
// COCO has 8 multi-word names ("traffic light" ...); '_' keeps them lossless.
const catName = new Map(coco.categories.map((c) => [c.id, c.name.replace(/ /g, '_')]));
const categories = coco.categories.map((c) => c.name.replace(/ /g, '_'));
const classOf = (a) => {
  const name = catName.get(a.category_id);
  if (name === undefined) {
    throw new Error(`annotation ${a.id}: category_id ${a.category_id} has no name in categories[]`);
  }
  return name;
};

await mkdir(join(PANEL, 'frames'), { recursive: true });
const tmp = join(PANEL, 'tmp_bgr');
await mkdir(tmp, { recursive: true });

// 1. Decode + stretch through the one dependency-free decoder on this platform.
const list = pinned.images.map((row, i) =>
  `${join(DATA, 'coco', 'images', row.file_name)};${join(tmp, String(i).padStart(4, '0'))}.bgr`);
const listFile = join(PANEL, 'decode_list.txt');
await writeFile(listFile, list.join('\n') + '\n');
console.log(`decoding ${list.length} image(s) to ${width}x${height} ...`);
const psOut = execFileSync('powershell.exe',
  ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', join(ROOT, 'tools', 'jpeg_to_rgb.ps1'),
    '-ListFile', listFile, '-Width', String(width), '-Height', String(height)],
  { encoding: 'utf8' });
process.stdout.write(psOut.trimEnd() + '\n');

// 2. BGR -> RGB8 (top-down already), one file per frame, tick order = pinned order.
const frames = [];
const boxStats = [];
let crowdWritten = 0;
for (let i = 0; i < pinned.images.length; ++i) {
  const bgr = readFileSync(join(tmp, String(i).padStart(4, '0') + '.bgr'));
  if (bgr.length !== width * height * 3) {
    throw new Error(`frame ${i}: got ${bgr.length} bytes, expected ${width * height * 3}`);
  }
  const rgb = Buffer.alloc(bgr.length);
  for (let p = 0; p < bgr.length; p += 3) {
    rgb[p] = bgr[p + 2];
    rgb[p + 1] = bgr[p + 1];
    rgb[p + 2] = bgr[p];
  }
  const name = `frame_${String(i).padStart(4, '0')}.rgb`;
  await writeFile(join(PANEL, 'frames', name), rgb);
  // Manifest entries are resolved against the manifest's own directory, so the
  // subdirectory is part of the path the loop will read.
  frames.push(`frames/${name}`);

  const row = pinned.images[i];
  // The pinned manifest carries ids only; geometry comes from the hash-pinned
  // annotation file, so the panel stays reproducible without dataset boxes in git.
  const entries = [...row.annotation_ids, ...row.crowd_ids].map((id) => {
    const a = annById.get(id);
    if (!a) throw new Error(`annotation ${id} is not in the annotation file`);
    return a;
  });
  entries.sort((x, y) => x.id - y.id);
  row.truth = entries.map((a) => {
    // Normalized on the SOURCE pixels: stretching the whole canvas to
    // width x height leaves normalized coordinates unchanged, so no padding
    // arithmetic is needed here (and none can go wrong).
    const u = a.bbox[0] / row.width, v = a.bbox[1] / row.height;
    const u2 = (a.bbox[0] + a.bbox[2]) / row.width, v2 = (a.bbox[1] + a.bbox[3]) / row.height;
    if (a.iscrowd === 1) crowdWritten += 1;
    boxStats.push({ area: Math.max(0, (u2 - u) * (v2 - v)), crowd: a.iscrowd === 1 });
    return `box ${u.toFixed(6)} ${v.toFixed(6)} ${u2.toFixed(6)} ${v2.toFixed(6)} ` +
      `${classOf(a)} ` +
      `${a.id} ${a.iscrowd === 1 ? 1 : 0}`;
  });
}
rmSync(tmp, { recursive: true, force: true });

// 3. The two manifests the loop consumes.
const licenseLine = `annotations CC BY 4.0 (COCO Consortium); images per-image CC, ` +
  `admitted license ids ${[...new Set(pinned.images.map((r) => r.license))].sort().join('/')} ` +
  `(see tools/fetch_coco_subset.manifest.json)`;
await writeFile(join(PANEL, 'frames.txt'),
  `# frame sequence for the COCO panel (stage D D3)\n` +
  `width=${width}\nheight=${height}\nrgb=rgb8\n` + frames.join('\n') + '\n');
await writeFile(join(PANEL, 'truth.txt'),
  `# annotation manifest for the COCO panel (stage D D3)\n` +
  `dataset=COCO val2017 subset (${pinned.images.length} images, pinned in ` +
  `tools/fetch_coco_subset.manifest.json)\n` +
  `license=${licenseLine}\n` +
  `width=${width}\nheight=${height}\n` +
  `categories=${categories.join(',')}\n` +
  pinned.images.map((row, i) => `tick=${i + 1}\n` + row.truth.join('\n')).join('\n') + '\n');

// 4. Stats for the regression report (nothing here is a claim; it is the
//    denominator the claims are made against).
const areas = boxStats.filter((b) => !b.crowd).map((b) => b.area).sort((a, b) => a - b);
const pct = (q) => areas[Math.min(areas.length - 1, Math.floor(q * (areas.length - 1)))];
const aspects = pinned.images.map((r) => (r.width / r.height) / (width / height));
const stats = {
  canvas: { width, height },
  frames: frames.length,
  boxes_non_crowd: areas.length,
  boxes_crowd_written: crowdWritten,
  normalized_area: { min: areas[0], p25: pct(0.25), median: pct(0.5), p75: pct(0.75), max: areas[areas.length - 1] },
  small_lt_100px2: areas.filter((a) => a * width * height < 100).length,
  aspect_distortion_vs_source: {
    note: 'stretching the source onto the canvas multiplies this ratio into the object aspect; ' +
      '1.00 means no distortion. It is a model-side confound, not a truth error.',
    min: Math.min(...aspects), median: aspects.slice().sort((a, b) => a - b)[Math.floor(aspects.length / 2)],
    max: Math.max(...aspects),
  },
  frames_at_or_over_cap: pinned.images.filter((r) => r.annotation_ids.length > 20).length,
  panel_sha256: {
    frames_txt: createHash('sha256').update(readFileSync(join(PANEL, 'frames.txt'))).digest('hex'),
    truth_txt: createHash('sha256').update(readFileSync(join(PANEL, 'truth.txt'))).digest('hex'),
  },
};
await writeFile(join(PANEL, 'panel_stats.json'), JSON.stringify(stats, null, 2) + '\n');
console.log(`panel: ${stats.frames} frames, ${stats.boxes_non_crowd} real boxes ` +
  `(+${stats.boxes_crowd_written} crowd boxes flagged for rule 1)`);
console.log(`manifests: ${join(PANEL, 'frames.txt')} / ${join(PANEL, 'truth.txt')}`);
