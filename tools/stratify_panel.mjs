#!/usr/bin/env node
// stratify_panel.mjs — split the real panel by TARGET SCALE (stage D slice D4).
//
//   data/panel/frames_small.txt + truth_small.txt   frames whose typical target is small
//   data/panel/frames_large.txt + truth_large.txt   frames whose typical target is large
//   data/panel/strata.json                           the split, for the report
//
// Why a data-side split and not an engine-side one: the engine's truth entities
// carry no size field, so "score only the small targets" would mean adding a
// field to Entity or Proposal -- and that would move world_hash / ledger_hash
// (ADR-0013 R1). The sub-panel approach moves NOTHING: a frame's truth, claims
// and matching are byte-identical in the sub-panel, only the set of frames that
// run changes. That is the difference between this and filtering frames by
// truth (which would turn matched claims into unmatched ones -- a different
// kind of pollution).
//
// The protocol is replicated here (crowd / degenerate / cap) ONLY to know which
// boxes the engine kept -- the engine remains the one that filters, and the
// artifact keeps its counters. The kept-box count is checked against the
// artifact so a drift between this script and annotation.cpp cannot pass
// unnoticed.
//
// Usage:
//   node tools/stratify_panel.mjs --artifact data/panel/d4_m1_observation.json
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const PANEL = join(ROOT, 'data', 'panel');

// The engine's protocol constants (src/truth_world/annotation.hpp). They are
// definitions, not knobs; a change there changes what "kept" means.
const MIN_AREA = 1e-6;
const PER_FRAME_CAP = 20;

const argIdx = process.argv.indexOf('--artifact');
const artifactPath = argIdx > 0 ? process.argv[argIdx + 1] : null;
const framesPath = join(PANEL, 'frames.txt');
const truthPath = join(PANEL, 'truth.txt');

const clamp01 = (v) => (v < 0 ? 0 : v > 1 ? 1 : v);

// median of a numeric array (even counts average the two middle values).
function median(xs) {
  if (xs.length === 0) return null;
  const s = xs.slice().sort((a, b) => a - b);
  const mid = s.length >> 1;
  return s.length % 2 === 1 ? s[mid] : (s[mid - 1] + s[mid]) / 2;
}

// --- parse the truth manifest (header verbatim + tick blocks) -----------------
const truthText = await readFile(truthPath, 'utf8');
const headerLines = [];
const blocks = [];  // [{ tick, lines[] }]
let inBody = false;
for (const raw of truthText.split('\n')) {
  const line = raw.trim();
  if (line === '' || line.startsWith('#')) {
    if (!inBody) headerLines.push(raw);
    continue;
  }
  if (line.startsWith('tick=')) {
    blocks.push({ tick: Number(line.slice(5)), lines: [] });
    inBody = true;
    continue;
  }
  if (!inBody) {
    headerLines.push(raw);
    continue;
  }
  blocks[blocks.length - 1].lines.push(line);
}
if (blocks.length === 0) throw new Error(`no tick= block in ${truthPath}`);

// --- replicate the engine protocol to learn which boxes were kept -------------
function keptBoxes(block) {
  const kept = [];
  for (const line of block.lines) {
    const tok = line.split(/\s+/);
    if (tok[0] !== 'box' || tok.length !== 8) {
      throw new Error(`truth: cannot parse box line '${line}'`);
    }
    const crowd = tok[7] === '1';
    if (crowd) continue;                       // rule 1
    const u0 = clamp01(Number(tok[1]));
    const v0 = clamp01(Number(tok[2]));
    const u1 = clamp01(Number(tok[3]));
    const v1 = clamp01(Number(tok[4]));
    const area = (u1 - u0) * (v1 - v0);        // rule 2
    if (!(area >= MIN_AREA)) continue;
    kept.push({ sourceId: Number(tok[6]), area, line });
  }
  kept.sort((a, b) => a.sourceId - b.sourceId);  // rule 7
  if (kept.length > PER_FRAME_CAP) kept.length = PER_FRAME_CAP;  // rule 3
  return kept;
}

// --- parse the frame manifest (the frame order the panel runs in) -------------
const framesText = await readFile(framesPath, 'utf8');
const frameLines = [];
const canvasLines = [];
let canvasWidth = 0;
let canvasHeight = 0;
for (const raw of framesText.split('\n')) {
  const line = raw.trim();
  if (line === '' || line.startsWith('#')) continue;
  if (line.includes('=')) {
    const eq = line.indexOf('=');
    const key = line.slice(0, eq);
    const val = line.slice(eq + 1);
    if (key === 'width') canvasWidth = Number(val);
    if (key === 'height') canvasHeight = Number(val);
    canvasLines.push(raw);   // the header travels with the sub-panel verbatim
    continue;
  }
  frameLines.push(line);
}
if (frameLines.length !== blocks.length) {
  throw new Error(`frames.txt has ${frameLines.length} entries, truth.txt has ${blocks.length} blocks`);
}

// --- per-frame scale, then the panel threshold --------------------------------
const perFrame = blocks.map((b, i) => {
  const kept = keptBoxes(b);
  const areas = kept.map((k) => k.area);
  return { index: i, frame: frameLines[i], kept, scale: median(areas) };
});
const withScale = perFrame.filter((f) => f.scale !== null);
const emptyFrames = perFrame.length - withScale.length;
const threshold = median(withScale.map((f) => f.scale));

const small = withScale.filter((f) => f.scale <= threshold);
const large = withScale.filter((f) => f.scale > threshold);

// --- self-check against the artifact the engine produced ----------------------
if (!artifactPath) {
  throw new Error('--artifact <run artifact> is required: this script must prove that the boxes ' +
    'it counted are the boxes the engine kept (annotation_truth.boxes_kept)');
}
const artifact = JSON.parse(await readFile(artifactPath, 'utf8'));
if (!artifact.annotation_truth) {
  throw new Error(`${artifactPath} has no annotation_truth section (was the run made with --annotations?)`);
}
const engineKept = artifact.annotation_truth.boxes_kept;
const myKept = perFrame.reduce((n, f) => n + f.kept.length, 0);
if (engineKept !== myKept) {
  throw new Error(`kept-box mismatch: the engine kept ${engineKept}, this script counted ${myKept}. ` +
    `The protocol here has drifted from src/truth_world/annotation.cpp -- fix the script, not the artifact.`);
}

// --- write the sub-panels -----------------------------------------------------
// The frame manifests stay next to the original one, so the relative frame paths
// keep resolving against their own directory (problem-log 014).
async function writeStratum(name, frames) {
  const fLines = [
    `# frame sequence for the ${name} sub-panel (stage D D4: target-scale stratum)`,
    ...canvasLines,
    ...frames.map((f) => f.frame),
  ];
  await writeFile(join(PANEL, `frames_${name}.txt`), fLines.join('\n') + '\n');

  const tLines = [...headerLines];
  frames.forEach((f, i) => {
    tLines.push(`tick=${i + 1}`);
    tLines.push(...f.kept.map((k) => k.line));
  });
  await writeFile(join(PANEL, `truth_${name}.txt`), tLines.join('\n') + '\n');

  return {
    frames: frames.length,
    boxes_kept: frames.reduce((n, f) => n + f.kept.length, 0),
    scale_median_of_frame_medians: median(frames.map((f) => f.scale)),
    scale_min: Math.min(...frames.map((f) => f.scale)),
    scale_max: Math.max(...frames.map((f) => f.scale)),
  };
}

await mkdir(PANEL, { recursive: true });
const smallStats = await writeStratum('small', small);
const largeStats = await writeStratum('large', large);

const strata = {
  note: 'frame-level strata by the median normalized area of the kept boxes; ' +
    'the sub-panels keep every frame byte-identical, only the frame set changes',
  canvas: { width: canvasWidth, height: canvasHeight },
  protocol: { min_area_normalized: MIN_AREA, per_frame_cap: PER_FRAME_CAP },
  panel: {
    frames: perFrame.length,
    frames_with_scale: withScale.length,
    frames_without_kept_boxes_excluded: emptyFrames,
    boxes_kept: myKept,
    boxes_kept_confirmed_by_artifact: engineKept,
    threshold_frame_scale: threshold,
    threshold_rule: 'small = frame scale <= threshold, large = frame scale > threshold',
    frame_scale_median: median(withScale.map((f) => f.scale)),
    frame_scale_min: Math.min(...withScale.map((f) => f.scale)),
    frame_scale_max: Math.max(...withScale.map((f) => f.scale)),
  },
  small: smallStats,
  large: largeStats,
};
await writeFile(join(PANEL, 'strata.json'), JSON.stringify(strata, null, 2) + '\n');

console.log(`threshold (median frame scale) = ${threshold}`);
console.log(`small: ${smallStats.frames} frames, ${smallStats.boxes_kept} boxes`);
console.log(`large: ${largeStats.frames} frames, ${largeStats.boxes_kept} boxes`);
console.log(`frames with no kept box (excluded from both strata): ${emptyFrames}`);
console.log(`kept boxes ${myKept} confirmed against the artifact (${engineKept})`);
console.log(`manifests: ${join(PANEL, 'frames_small.txt')} / ${join(PANEL, 'truth_small.txt')} ` +
  `and ${join(PANEL, 'frames_large.txt')} / ${join(PANEL, 'truth_large.txt')}`);
