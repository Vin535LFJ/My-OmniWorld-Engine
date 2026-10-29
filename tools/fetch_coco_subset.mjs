#!/usr/bin/env node
// fetch_coco_subset.mjs — stage D slice D3: fetch the annotated-truth panel
// this repository scores against. The dataset is NOT in git (PLAN license
// red line + L1 sec 2 item 7): what is in git is this script, the pinned subset
// manifest (image ids + SHA256), and the license record it writes.
//
// Why sparse: the COCO buckets are ~1 GB zips, and a 200-image panel does not
// need them. The annotation zip is read through HTTP Range requests (central
// directory only, then one deflated entry), and each selected image is a plain
// GET. Measured cost: about 6.5 MB of compressed annotation + the images.
//
// License gate (stricter than the D1 note, and the reason is measured, not
// assumed): COCO's *annotations* are CC BY 4.0, but the *images* carry per-image
// Flickr Creative Commons licenses, and 3475 of val2017's 5000 are
// NonCommercial. A public portfolio panel admits only licenses that survive
// that: BY (4), no known copyright restrictions (7), US government work (8).
// NoDerivs and ShareAlike are excluded because the panel resizes the images.
//
// Usage:
//   node tools/fetch_coco_subset.mjs              # fetch anything missing
//   node tools/fetch_coco_subset.mjs --verify     # check pinned SHA256 only
//   node tools/fetch_coco_subset.mjs --limit 50   # subset size on the first pin
import { createHash } from 'node:crypto';
import { createReadStream, createWriteStream, existsSync } from 'node:fs';
import { mkdir, readFile, stat, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateRawSync } from 'node:zlib';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const DATA = join(ROOT, 'data', 'coco');
const PINNED = join(ROOT, 'tools', 'fetch_coco_subset.manifest.json');

const ANN_ZIP = 'http://images.cocodataset.org/annotations/annotations_trainval2017.zip';
const ANN_ENTRY = 'annotations/instances_val2017.json';
const IMAGE_BASE = 'http://images.cocodataset.org/val2017/';

const ALLOWED_LICENSE_IDS = new Set([4, 7, 8]);
const DEFAULT_LIMIT = 200;
// Protocol bounds the fetcher respects so the engine's own counters stay at
// zero on a clean panel: the per-frame cap must not bite silently.
const MAX_BOXES_PER_IMAGE = 20;

const u16 = (b, o) => b.readUInt16LE(o);
const u32 = (b, o) => b.readUInt32LE(o);

async function get(url, opts = {}) {
  for (let attempt = 1; attempt <= 3; ++attempt) {
    try {
      const res = await fetch(url, opts);
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      return res;
    } catch (e) {
      if (attempt === 3) throw new Error(`${url}: ${e.message} (3 attempts)`);
      await new Promise((r) => setTimeout(r, 500 * attempt));
    }
  }
}

async function getBuffer(url, from, to) {
  const res = await get(url, { headers: { range: `bytes=${from}-${to}` } });
  if (res.status !== 206) throw new Error(`${url}: range ${from}-${to} -> HTTP ${res.status}`);
  return Buffer.from(await res.arrayBuffer());
}

// Read a remote zip's central directory and inflate exactly one entry.
async function sparseExtract(url, entryName) {
  const probe = await get(url, { headers: { range: 'bytes=0-0' } });
  const match = /\/(\d+)$/.exec(probe.headers.get('content-range') || '');
  if (!match) throw new Error(`${url}: no usable content-range -- the bucket does not answer Range`);
  const size = Number(match[1]);
  const tail = await getBuffer(url, Math.max(0, size - 65536), size - 1);
  let eocd = -1;
  for (let i = tail.length - 22; i >= 0; --i) {
    if (tail.readUInt32LE(i) === 0x06054b50) { eocd = i; break; }
  }
  if (eocd < 0) throw new Error(`${url}: end-of-central-directory not found`);
  const entries = u16(tail, eocd + 10);
  const cdSize = u32(tail, eocd + 12);
  const cdOffset = u32(tail, eocd + 16);
  if (cdOffset === 0xffffffff || entries === 0xffff) {
    throw new Error(`${url}: zip64 root record -- this fetcher does not handle it`);
  }
  const cd = await getBuffer(url, cdOffset, cdOffset + cdSize - 1);
  for (let p = 0; p + 46 <= cd.length && cd.readUInt32LE(p) === 0x02014b50; ) {
    const method = u16(cd, p + 10);
    const csize = u32(cd, p + 20);
    const usize = u32(cd, p + 24);
    const nlen = u16(cd, p + 28);
    const elen = u16(cd, p + 30);
    const clen = u16(cd, p + 32);
    const localHeaderOffset = u32(cd, p + 42);
    const name = cd.toString('utf8', p + 46, p + 46 + nlen);
    if (name === entryName) {
      // The local header repeats the name and has its own extra length.
      const head = await getBuffer(url, localHeaderOffset, localHeaderOffset + 29);
      if (head.readUInt32LE(0) !== 0x04034b50) throw new Error(`${entryName}: bad local header`);
      const nl = u16(head, 26), el = u16(head, 28);
      const start = localHeaderOffset + 30 + nl + el;
      const data = await getBuffer(url, start, start + csize - 1);
      const out = method === 0 ? data : inflateRawSync(data);
      if (out.length !== usize) {
        throw new Error(`${entryName}: inflated ${out.length}, expected ${usize}`);
      }
      return out;
    }
    p += 46 + nlen + elen + clen;
  }
  throw new Error(`${url}: entry '${entryName}' not in the central directory`);
}

async function sha256File(path) {
  const h = createHash('sha256');
  await new Promise((resolve, reject) => {
    createReadStream(path).on('data', (d) => h.update(d)).on('end', resolve).on('error', reject);
  });
  return h.digest('hex');
}

function selectImages(coco, limit) {
  const byImage = new Map();
  for (const a of coco.annotations) {
    const row = byImage.get(a.image_id) || { boxes: [], crowd_ids: [] };
    if (a.iscrowd === 1) row.crowd_ids.push(a.id);
    else row.boxes.push(a);
    byImage.set(a.image_id, row);
  }
  const cats = new Map(coco.categories.map((c) => [c.id, c.name]));
  const picked = [];
  const rejected = { license: 0, no_boxes: 0, over_cap: 0 };
  for (const im of [...coco.images].sort((x, y) => x.id - y.id)) {
    if (picked.length >= limit) break;
    if (!ALLOWED_LICENSE_IDS.has(im.license)) { rejected.license += 1; continue; }
    const row = byImage.get(im.id);
    if (!row || row.boxes.length === 0) { rejected.no_boxes += 1; continue; }
    if (row.boxes.length > MAX_BOXES_PER_IMAGE) { rejected.over_cap += 1; continue; }
    // The category table travels with the panel: the engine checks every class
    // against it (protocol rule 4), so an unmapped id must never be written.
    for (const a of row.boxes) {
      if (!cats.has(a.category_id)) throw new Error(`category_id ${a.category_id} has no name`);
    }
    picked.push({
      image_id: im.id,
      file_name: im.file_name,
      license: im.license,
      license_name: (coco.licenses.find((l) => l.id === im.license) || {}).name || 'unknown',
      flickr_url: im.flickr_url,
      width: im.width,
      height: im.height,
      // Ids only, no geometry. The annotation file is hash-pinned below, so
      // selection data alone reproduces the panel -- and copying the dataset's
      // boxes into git would contradict "数据不入库" (L1 sec 2 item 7).
      annotation_ids: row.boxes.map((a) => a.id).sort((x, y) => x - y),
      crowd_ids: row.crowd_ids.sort((x, y) => x - y),
      sha256: '',
    });
  }
  return { picked, rejected };
}

async function main() {
  const args = new Set(process.argv.slice(2));
  const verifyOnly = args.has('--verify');
  const limitIdx = process.argv.indexOf('--limit');
  const limit = limitIdx > 0 ? Number(process.argv[limitIdx + 1]) : DEFAULT_LIMIT;
  if (!Number.isInteger(limit) || limit < 1) throw new Error('--limit must be a positive integer');

  let pinned = existsSync(PINNED) ? JSON.parse(await readFile(PINNED, 'utf8')) : null;
  if (pinned && !verifyOnly && pinned.images.length !== limit) {
    console.log(`note: subset already pinned at ${pinned.images.length} images (--limit ${limit} ignored)`);
  }

  await mkdir(join(DATA, 'images'), { recursive: true });

  // 1. The annotation file: sparse-extracted, then pinned by SHA256.
  const annPath = join(DATA, 'instances_val2017.json');
  if (!existsSync(annPath)) {
    console.log(`range-reading ${ANN_ENTRY} out of the annotation zip ...`);
    const raw = await sparseExtract(ANN_ZIP, ANN_ENTRY);
    await writeFile(annPath, raw);
    console.log(`  ${raw.length} bytes from ${ANN_ZIP}`);
  }
  const annSha = await sha256File(annPath);
  if (pinned && pinned.annotations_sha256 !== annSha) {
    throw new Error(`annotation file SHA256 does not match the pin: ${annSha} != ${pinned.annotations_sha256}`);
  }

  const coco = JSON.parse(await readFile(annPath, 'utf8'));
  if (!pinned) {
    const { picked, rejected } = selectImages(coco, limit);
    pinned = {
      source: { annotations_zip: ANN_ZIP, annotation_entry: ANN_ENTRY, image_base: IMAGE_BASE },
      attribution: 'COCO Consortium (cocodataset.org); annotations CC BY 4.0, images per license below',
      license_rule: 'admitted license ids: 4 (CC-BY), 7 (no known copyright restrictions), 8 (US government work); ' +
        'excluded: all NonCommercial ids and ND/SA (the panel resizes the images)',
      selection: `license admitted, 1..${MAX_BOXES_PER_IMAGE} non-crowd boxes, ascending image_id, first ${limit}`,
      rejected_by_rule: rejected,
      annotations_sha256: annSha,
      images: picked,
    };
    await writeFile(PINNED, JSON.stringify(pinned, null, 2) + '\n');
    console.log(`pinned ${pinned.images.length} images -> tools/fetch_coco_subset.manifest.json`);
    console.log(`  skipped by the license rule: ${rejected.license}, over the box cap: ${rejected.over_cap}`);
  }

  // 2. Images: plain GETs, one per pinned file, verified or pinned.
  const ids = new Map(coco.images.map((im) => [im.id, im]));
  let downloaded = 0, verified = 0, pinned_now = 0;
  for (const row of pinned.images) {
    const target = join(DATA, 'images', row.file_name);
    if (!existsSync(target)) {
      if (verifyOnly) throw new Error(`--verify: missing ${row.file_name}`);
      const res = await get(IMAGE_BASE + row.file_name);
      const buf = Buffer.from(await res.arrayBuffer());
      if (buf.length < 1000) throw new Error(`${row.file_name}: suspiciously small (${buf.length} B)`);
      await writeFile(target, buf);
      downloaded += 1;
    }
    const sha = await sha256File(target);
    if (row.sha256 === '') {
      if (verifyOnly) throw new Error(`--verify: ${row.file_name} has no pinned SHA256`);
      row.sha256 = sha;
      pinned_now += 1;
    } else if (sha !== row.sha256) {
      throw new Error(`${row.file_name}: SHA256 ${sha} != pinned ${row.sha256}`);
    } else {
      verified += 1;
    }
    if (!ids.has(row.image_id)) throw new Error(`image_id ${row.image_id} is not in the annotation file`);
  }
  if (pinned_now > 0) {
    await writeFile(PINNED, JSON.stringify(pinned, null, 2) + '\n');
    console.log(`pinned SHA256 for ${pinned_now} images (first run)`);
  }

  // 3. The license record: one line per image, so attribution is checkable.
  const lines = [
    `# COCO subset license record -- annotations: CC BY 4.0 (COCO Consortium)`,
    `# images keep their own Flickr Creative Commons license; each row is one of them.`,
    `# annotations_sha256=${annSha}`,
    ...pinned.images.map((r) =>
      `${r.file_name}\timage_id=${r.image_id}\tlicense_id=${r.license}\t` +
      `license="${r.license_name}"\tsource=${r.flickr_url}\tsha256=${r.sha256}`),
  ];
  await writeFile(join(DATA, 'LICENSE.tsv'), lines.join('\n') + '\n');
  const sums = (await Promise.all(pinned.images.map(async (r) =>
    `${r.sha256}  images/${r.file_name}`))).join('\n');
  await writeFile(join(DATA, 'SHA256SUMS.txt'), `${sums}\n${annSha}  instances_val2017.json\n`);

  const total = (await Promise.all(pinned.images.map((r) =>
    stat(join(DATA, 'images', r.file_name))))).reduce((a, s) => a + s.size, 0);
  console.log(`images: ${pinned.images.length} (${(total / 1e6).toFixed(1)} MB) ` +
    `downloaded=${downloaded} verified=${verified}`);
  console.log(`data dir: ${DATA} (git-ignored)`);
}

main().catch((e) => { console.error(`error: ${e.message}`); process.exit(1); });
