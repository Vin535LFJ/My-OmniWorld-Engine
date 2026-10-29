#!/usr/bin/env node
// fetch_video_sample.mjs — stage D slice D5, Gate 0 item G0-3: fetch the one
// decodable video sample the NVDEC path needs. The file is NOT in git (same rule
// as the COCO panel): what lands is this script, the pinned manifest (URL +
// bytes + SHA256), and the license record.
//
// License gate: the sample must survive commercial redistribution of the
// repository's own artifacts. Sintel (Blender Durian Open Movie) is CC BY 3.0 —
// attribution required, commercial use permitted. Verified 2026-10-27; sources
// are recorded in tools/fetch_video_sample.manifest.json.
//
// Usage:
//   node tools/fetch_video_sample.mjs            # fetch anything missing
//   node tools/fetch_video_sample.mjs --verify   # check pinned SHA256 only
import { createHash } from 'node:crypto';
import { createReadStream, createWriteStream } from 'node:fs';
import { mkdir, readFile, stat, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const PINNED = join(ROOT, 'tools', 'fetch_video_sample.manifest.json');

const verifyOnly = process.argv.includes('--verify');

async function sha256OfFile(path) {
  const h = createHash('sha256');
  await new Promise((res, rej) => {
    const rs = createReadStream(path);
    rs.on('data', (c) => h.update(c));
    rs.on('end', res);
    rs.on('error', rej);
  });
  return h.digest('hex');
}

async function fetchOne(item) {
  const out = resolve(ROOT, item.path);
  await mkdir(dirname(out), { recursive: true });
  const res = await fetch(item.url);
  if (!res.ok) throw new Error(`HTTP ${res.status} for ${item.url}`);
  await new Promise((res2, rej) => {
    const ws = createWriteStream(out);
    ws.on('error', rej);
    ws.on('finish', res2);
    res.body.pipe(ws);
  });
  return out;
}

const manifest = JSON.parse(await readFile(PINNED, 'utf8'));
let failures = 0;

for (const item of manifest.items) {
  const out = resolve(ROOT, item.path);
  let present = false;
  try { present = (await stat(out)).size === item.bytes; } catch { present = false; }

  if (present) {
    const sum = await sha256OfFile(out);
    if (sum === item.sha256) { console.log(`ok       ${item.path}`); continue; }
    console.log(`hash     ${item.path}: mismatch (will refetch)`);
  } else if (verifyOnly) {
    console.log(`missing  ${item.path}`);
    failures += 1;
    continue;
  }

  if (verifyOnly) continue;
  await fetchOne(item);
  const got = (await stat(out)).size;
  const sum = await sha256OfFile(out);
  if (got !== item.bytes || sum !== item.sha256) {
    console.error(`FAIL     ${item.path}: got ${got} bytes sha256 ${sum}`);
    failures += 1;
  } else {
    console.log(`fetched  ${item.path}`);
  }
}

// The license record travels with the panel it feeds, so a reviewer can check
// the gate without opening the manifest.
const licenseBlock = manifest.items
  .map((i) => `license=${i.license} rights_holder=${i.rights_holder} file=${i.path}`)
  .join('\n');
await writeFile(resolve(ROOT, 'data', 'sample', 'LICENSE.txt'), licenseBlock + '\n');

process.exit(failures ? 1 : 0);
