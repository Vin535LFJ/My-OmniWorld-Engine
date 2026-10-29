#!/usr/bin/env node
// build_dashboard.mjs — generate a self-contained data dashboard from REAL run
// artifacts. It executes the engine for a small panel matrix, reads each JSON
// run report, and emits one offline HTML page with the numbers embedded.
//
// Nothing here is hand-typed: if the engine's behaviour changes, regenerate and
// the page follows. The page carries the commit it was cut from.
//
// Usage:
//   node tools/build_dashboard.mjs                 # auto-detect exes, write docs/dashboard.html
//   node tools/build_dashboard.mjs --out path.html
//   node tools/build_dashboard.mjs --skip-real     # CV panels only (no ONNX build needed)
import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const outIdx = args.indexOf('--out');
const OUT = outIdx >= 0 ? args[outIdx + 1] : join(ROOT, 'docs', 'dashboard.html');
const SKIP_REAL = args.includes('--skip-real');

// Prefer a Release binary for EVERY panel so the whole page shares one build
// provenance: latency claims are only valid from Release, and mixing a Debug CV
// run with Release model runs in one chart would be exactly the kind of
// cross-condition comparison this repo forbids. The ON binary runs CV-only
// panels fine, so it doubles as the Release default.
// Do not guess the build type from a directory name: probe the binary and read
// `environment.build` from a one-tick run. Latency claims are only valid from a
// Release build, so the generator refuses to mix one in silently.
function probeBuild(exe) {
  const dir = mkdtempSync(join(tmpdir(), 'ow-probe-'));
  const f = join(dir, 'p.json');
  try {
    execFileSync(exe, ['--ticks', '1', '--seed', '1', '--out', f], { stdio: 'pipe', timeout: 120000 });
  } catch {
    if (!existsSync(f)) return null; // non-zero exit still writes the artifact by design
  }
  try {
    return JSON.parse(readFileSync(f, 'utf8')).environment.build || null;
  } catch {
    return null;
  }
}

function findExe(kind) {
  const candidates =
    kind === 'on'
      ? ['build-ort-rel/omniworld_loop.exe', 'build-ort-c5/omniworld_loop.exe', 'build-ort/omniworld_loop.exe']
      : [
          // The ON binaries also run CV-only panels and are usually Release.
          'build-ort-rel/omniworld_loop.exe',
          'build-ort-c5/omniworld_loop.exe',
          'build-rel/omniworld_loop.exe',
          'build/omniworld_loop.exe',
        ];
  const existing = candidates.map((c) => join(ROOT, c)).filter((p) => existsSync(p));
  // Newest first, then first one that actually reports Release.
  existing.sort((a, b) => statSync(b).mtimeMs - statSync(a).mtimeMs);
  for (const p of existing) {
    if (probeBuild(p) === 'release') return p;
  }
  return existing.length ? existing[0] : null; // fall back; the page warns loudly
}

const OFF = findExe('off');
const ON = SKIP_REAL ? null : findExe('on');
if (!OFF) throw new Error('no default-build omniworld_loop.exe found; run build.bat --test first');

const RUNS = [
  { id: 'cv-ref', label: '内建 CV · 参考', exe: 'off', mode: 'reference', ticks: 600, seed: 42 },
  { id: 'cv-ref-2', label: '内建 CV · 参考（复跑）', exe: 'off', mode: 'reference', ticks: 600, seed: 42 },
  { id: 'cv-perf', label: '内建 CV · 性能', exe: 'off', mode: 'performance', ticks: 600, seed: 42 },
  { id: 'det', label: 'RT-DETR · 性能', exe: 'on', mode: 'performance', ticks: 5, seed: 42, adapters: 'rtdetr_v2_s', models: 'zero_delta' },
  { id: 'depth', label: 'DA-V2 · 性能', exe: 'on', mode: 'performance', ticks: 5, seed: 42, adapters: 'depth_anything_v2_s', models: 'zero_delta' },
  { id: 'both', label: '两家族同面板 · 性能', exe: 'on', mode: 'performance', ticks: 5, seed: 42, adapters: 'rtdetr_v2_s,depth_anything_v2_s', models: 'zero_delta' },
  { id: 'rt-over', label: '真模型 vs 注入者 · 参考', exe: 'on', mode: 'reference', ticks: 20, seed: 42, adapters: 'rtdetr_v2_s,blob_detect_overconfident', models: 'zero_delta' },
];

const tmp = mkdtempSync(join(tmpdir(), 'ow-dash-'));
const results = [];
const skipped = [];

for (const r of RUNS) {
  const exe = r.exe === 'on' ? ON : OFF;
  if (!exe) {
    skipped.push({ id: r.id, label: r.label, reason: '没有 ONNX 构建（需要 -DOW_WITH_ONNXRUNTIME=ON）' });
    continue;
  }
  const out = join(tmp, r.id + '.json');
  const cli = ['--ticks', String(r.ticks), '--seed', String(r.seed), '--mode', r.mode, '--out', out];
  if (r.adapters) cli.push('--adapters', r.adapters);
  if (r.models) cli.push('--models', r.models);
  process.stdout.write('run ' + r.id + ' … ');
  try {
    execFileSync(exe, cli, { stdio: 'pipe', timeout: 300000 });
  } catch (e) {
    // A non-zero exit is meaningful (performance + fault), and the artifact is
    // still written by design -- keep going, record the exit code.
    if (!existsSync(out)) throw new Error('run ' + r.id + ' produced no artifact: ' + e.message);
  }
  const j = JSON.parse(readFileSync(out, 'utf8'));
  results.push({
    ...r,
    exePath: exe.replace(ROOT + '/', '').replace(/\\/g, '/'),
    build: j.environment ? j.environment.build : 'unknown',
    buildCommit: j.environment ? j.environment.git_commit : 'unknown',
    artifact: j,
  });
  console.log('ok (' + j.ledger_entries + ' entries, ' + (j.environment ? j.environment.build : '?') + ')');
}

function want(id) {
  return results.find((r) => r.id === id) || null;
}
const cv = want('cv-ref');
const cv2 = want('cv-ref-2');
const rtOver = want('rt-over');

const ece = (r, producer) => (r ? r.artifact.metrics['ece:' + producer] ?? null : null);
const sep = (a, b) => (a !== null && b !== null ? Math.abs(a - b) : null);

// The artifact stores stage_percentiles in MICROSECONDS (a detect p50 of
// 518229.6 is 518 ms). The page works in milliseconds, so convert once here --
// a dashboard that silently mislabels units is worse than no dashboard.
function stagesMs(sp) {
  const out = {};
  for (const k of Object.keys(sp || {})) {
    out[k] = { p50: sp[k].p50 / 1000, p95: sp[k].p95 / 1000 };
  }
  return out;
}

const builds = [...new Set(results.map((r) => (r.artifact.environment || {}).build || 'unknown'))];
const commits = [...new Set(results.map((r) => (r.artifact.environment || {}).git_commit || 'unknown'))];

const DATA = {
  generatedAt: new Date().toISOString(),
  commit: cv ? cv.artifact.environment.git_commit : 'unknown',
  build: cv ? cv.artifact.environment.build : 'unknown',
  builds,
  commits,
  machine: cv ? { os: cv.artifact.environment.os, arch: cv.artifact.environment.arch, cores: cv.artifact.environment.cpu_logical_cores, compiler: cv.artifact.environment.compiler } : null,
  runs: results.map((r) => ({
    id: r.id,
    label: r.label,
    exe: r.exePath,
    build: r.build,
    buildCommit: r.buildCommit,
    mode: r.artifact.mode,
    ep: r.artifact.config.ep,
    ticks: r.ticks,
    seed: r.seed,
    stages: stagesMs(r.artifact.stage_percentiles),
    metrics: r.artifact.metrics,
    entries: r.artifact.ledger_entries,
    applied: r.artifact.applied,
    announced: r.artifact.announced,
    rejected: r.artifact.rejected,
    faults: r.artifact.adapter_faults,
    firstFault: r.artifact.first_fault,
    worldHash: r.artifact.world_hash,
    ledgerHash: r.artifact.ledger_hash,
  })),
  skipped,
  separations: [
    {
      pair: '内建诚实例 vs 内建注入例',
      honest: { name: 'blob_detect', v: ece(cv, 'blob_detect') },
      injected: { name: 'blob_detect_overconfident', v: ece(cv, 'blob_detect_overconfident') },
      separation: sep(ece(cv, 'blob_detect'), ece(cv, 'blob_detect_overconfident')),
      source: 'cv-ref',
      sample: cv ? cv.ticks + ' ticks' : '',
    },
    {
      pair: '真模型 vs 注入例',
      honest: { name: 'rtdetr_v2_s', v: ece(rtOver, 'rtdetr_v2_s') },
      injected: { name: 'blob_detect_overconfident', v: ece(rtOver, 'blob_detect_overconfident') },
      separation: sep(ece(rtOver, 'rtdetr_v2_s'), ece(rtOver, 'blob_detect_overconfident')),
      source: 'rt-over',
      sample: rtOver ? rtOver.ticks + ' ticks' : '',
    },
  ],
  determinism: cv && cv2
    ? {
        a: { world: cv.artifact.world_hash, ledger: cv.artifact.ledger_hash, entries: cv.artifact.ledger_entries },
        b: { world: cv2.artifact.world_hash, ledger: cv2.artifact.ledger_hash, entries: cv2.artifact.ledger_entries },
        equal: cv.artifact.world_hash === cv2.artifact.world_hash && cv.artifact.ledger_hash === cv2.artifact.ledger_hash,
      }
    : null,
};

// The page script is written without template literals so this outer template
// cannot interpolate it accidentally.
const HTML = `<!doctype html>
<html lang="zh-CN" data-theme="auto">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OmniWorld Engine — 运行数据看板</title>
<meta name="description" content="由真实运行工件生成的引擎数据看板：逐阶段延迟、per-producer ECE、账本构成、确定性与单 tick 时间轴。">
<style>
  :root { color-scheme: light dark;
    --bg:#fff; --surface:#f7f8fa; --surface-2:#eef0f4; --fg:#16181d; --muted:#5b6472;
    --border:#d6dae1; --border-soft:#e6e9ee; --accent:#2f6feb; --ok:#1a7f4b; --warn:#9a5b00; --bad:#b3261e;
    --s1:#2f6feb; --s2:#c2410c; --s3:#0f766e; --s4:#7c3aed;
    --font: system-ui, -apple-system, "Segoe UI", Roboto, "Helvetica Neue", "PingFang SC", "Microsoft YaHei", sans-serif;
    --code: ui-monospace, SFMono-Regular, "Cascadia Mono", Consolas, monospace; }
  @media (prefers-color-scheme: dark) { :root { --bg:#15171c; --surface:#1c1f26; --surface-2:#242832; --fg:#e6e8ec; --muted:#9aa4b2;
    --border:#333845; --border-soft:#282d37; --accent:#6ea8fe; --ok:#4ec98a; --warn:#e0a54a; --bad:#f2726b;
    --s1:#6ea8fe; --s2:#fb923c; --s3:#2dd4bf; --s4:#c084fc; } }
  html[data-theme="light"] { --bg:#fff; --surface:#f7f8fa; --surface-2:#eef0f4; --fg:#16181d; --muted:#5b6472; --border:#d6dae1; --border-soft:#e6e9ee; --accent:#2f6feb; --ok:#1a7f4b; --warn:#9a5b00; --bad:#b3261e; color-scheme:light; }
  html[data-theme="dark"] { --bg:#15171c; --surface:#1c1f26; --surface-2:#242832; --fg:#e6e8ec; --muted:#9aa4b2; --border:#333845; --border-soft:#282d37; --accent:#6ea8fe; --ok:#4ec98a; --warn:#e0a54a; --bad:#f2726b; color-scheme:dark; }
  * { box-sizing: border-box; }
  body { margin:0; background:var(--bg); color:var(--fg); font-family:var(--font); font-size:15px; line-height:1.6; }
  .wrap { max-width:1180px; margin:0 auto; padding:24px 20px 64px; }
  header.page { display:flex; flex-wrap:wrap; gap:12px; align-items:baseline; justify-content:space-between; border-bottom:1px solid var(--border-soft); padding-bottom:14px; }
  h1 { font-size:20px; margin:0; font-weight:600; }
  h2 { font-size:15px; margin:34px 0 6px; font-weight:600; }
  .sub { color:var(--muted); font-size:13px; margin:0 0 12px; }
  button { font:inherit; cursor:pointer; color:var(--fg); background:var(--surface); border:1px solid var(--border); border-radius:8px; padding:7px 12px; min-height:38px; }
  button:hover { border-color:var(--accent); }
  button[aria-pressed="true"] { border-color:var(--accent); background:var(--surface-2); font-weight:600; }
  .cards { display:grid; grid-template-columns:repeat(auto-fit,minmax(150px,1fr)); gap:8px; margin-top:16px; }
  .card { border:1px solid var(--border-soft); border-radius:8px; padding:9px 11px; background:var(--surface); }
  .card span { display:block; font-size:11px; color:var(--muted); }
  .card strong { font-weight:600; font-variant-numeric:tabular-nums; font-family:var(--code); font-size:15px; }
  table { border-collapse:collapse; width:100%; font-size:13.5px; }
  th,td { border-bottom:1px solid var(--border-soft); padding:7px 9px; text-align:left; vertical-align:top; }
  th { color:var(--muted); font-size:11px; text-transform:uppercase; letter-spacing:.04em; font-weight:600; }
  td.num { font-family:var(--code); font-variant-numeric:tabular-nums; white-space:nowrap; }
  .badge { display:inline-block; font-size:11.5px; font-weight:600; border-radius:999px; padding:1px 9px; border:1px solid var(--border); color:var(--muted); }
  .badge.ok { color:var(--ok); border-color:var(--ok); }
  .badge.warn { color:var(--warn); border-color:var(--warn); }
  .badge.bad { color:var(--bad); border-color:var(--bad); }
  .chart { width:100%; height:auto; display:block; }
  .axis { stroke:var(--border); stroke-width:1; }
  .gridline { stroke:var(--border-soft); stroke-width:1; }
  .tick { fill:var(--muted); font-size:11px; font-family:var(--code); }
  .rowlab { fill:var(--fg); font-size:12.5px; }
  .val { fill:var(--fg); font-size:11.5px; font-family:var(--code); }
  .note { color:var(--muted); font-size:13px; margin:8px 0 0; }
  a { color:var(--accent); text-decoration:none; }
  a:hover { text-decoration:underline; }
  code { font-family:var(--code); font-size:.92em; background:var(--surface-2); border-radius:4px; padding:1px 5px; }
  .seg { display:flex; flex-wrap:wrap; gap:6px; margin:10px 0 4px; }
  footer { margin-top:40px; padding-top:14px; border-top:1px solid var(--border-soft); color:var(--muted); font-size:12.5px; }
  .legend { display:flex; flex-wrap:wrap; gap:14px; font-size:11.5px; color:var(--muted); margin-top:6px; }
  .legend i { width:9px; height:9px; border-radius:2px; display:inline-block; margin-right:4px; }
  @media (prefers-reduced-motion: no-preference) { .cellrect { transition: opacity 120ms linear; } }
</style>
</head>
<body>
<div class="wrap">
<header class="page">
  <div>
    <h1>OmniWorld Engine — 运行数据看板</h1>
    <p class="sub" id="meta"></p>
  </div>
  <div class="seg"><button id="theme" type="button">主题：跟随系统</button></div>
</header>

<div class="cards" id="cards"></div>

<h2>逐阶段延迟（p50，对数色阶）</h2>
<p class="sub">每一行是一次真实运行；格子里的数字是毫秒。悬停看 p95。注意量级差：内建 CV 是亚毫秒，真实模型是秒级——这不是优化问题，是"模型在环"的成本本身。</p>
<div id="heat" aria-live="polite"></div>

<h2>判据 (c)：ECE 与分离量</h2>
<p class="sub">点在各自生产者的 ECE 上；分离量是两条横杠之间差的绝对值。阈值 0.30 只作用于<strong>分离量</strong>，不作用于单个生产者的 ECE。</p>
<div id="ece"></div>

<h2>账本构成</h2>
<p class="sub">applied = 关联到真值实体；announced = 新实体声明（未锚定）；rejected = 被拒。</p>
<div id="ledger"></div>

<h2>确定性</h2>
<p class="sub">同配置复跑两次，世界哈希与账本哈希必须逐位相同。时间不进哈希。</p>
<div id="determinism"></div>

<h2>单 tick 时间轴</h2>
<div class="seg" id="timelineRuns"></div>
<div id="timeline"></div>

<footer id="footer"></footer>
</div>

<script>
(function () {
  var DATA = __DATA__;
  var NS = 'http://www.w3.org/2000/svg';
  function el(t, a) { var n = document.createElementNS(NS, t); for (var k in a) if (Object.prototype.hasOwnProperty.call(a, k)) n.setAttribute(k, a[k]); return n; }
  function txt(s, cls, x, y, anchor) { var t = el('text', { x: x, y: y, 'class': cls, 'text-anchor': anchor || 'start' }); t.textContent = s; return t; }
  function fmt(ms) {
    if (ms === null || ms === undefined) return '—';
    if (ms >= 1000) return (ms / 1000).toFixed(2) + ' s';
    if (ms >= 10) return ms.toFixed(1) + ' ms';
    if (ms >= 1) return ms.toFixed(2) + ' ms';
    if (ms >= 0.01) return ms.toFixed(3) + ' ms';
    // Below 10 µs "0.000 ms" reads as zero; switch units instead.
    if (ms * 1000 < 0.1) return '<0.1 µs';
    return (ms * 1000).toFixed(1) + ' µs';
  }
  function pct(v) { return (v * 100).toFixed(1) + '%'; }

  document.getElementById('meta').textContent =
    '由 ' + DATA.runs.length + ' 次真实运行生成 · ' + DATA.builds.join('/') + ' 构建 · commit ' + DATA.commits.join('/') + ' · ' +
    (DATA.machine ? DATA.machine.os + '/' + DATA.machine.arch + ' · ' + DATA.machine.compiler + ' · ' + DATA.machine.cores + ' 核' : '') +
    ' · ' + DATA.generatedAt.slice(0, 16).replace('T', ' ');
  if (DATA.builds.indexOf('release') < 0) {
    var warn = document.createElement('p');
    warn.className = 'sub';
    warn.setAttribute('role', 'alert');
    warn.textContent = '⚠ 本次生成没有使用 Release 构建（' + DATA.builds.join(', ') + '）：延迟数字仅供结构参考，不能作为性能主张。先构建一个 Release 版本再重新生成。';
    document.getElementById('meta').parentNode.appendChild(warn);
  }

  // ---- summary cards ----
  var totalEntries = DATA.runs.reduce(function (s, r) { return s + r.entries; }, 0);
  var faults = DATA.runs.reduce(function (s, r) { return s + r.faults; }, 0);
  var cards = [
    ['运行次数', String(DATA.runs.length)],
    ['总账本条数', String(totalEntries)],
    ['适配器故障', String(faults)],
    ['确定性', DATA.determinism ? (DATA.determinism.equal ? '逐位一致' : '不一致') : '—'],
    ['未执行的运行', String(DATA.skipped.length)],
  ];
  var ch = document.getElementById('cards');
  cards.forEach(function (c) {
    var d = document.createElement('div'); d.className = 'card';
    var s = document.createElement('span'); s.textContent = c[0];
    var v = document.createElement('strong'); v.textContent = c[1];
    d.appendChild(s); d.appendChild(v); ch.appendChild(d);
  });

  // ---- heatmap: runs x REAL stage keys, log-scaled color ----
  // Stage keys are per participant in the artifact (detect:<adapter>,
  // predict:<model>), not a flat "perceive" bucket. Derive the union instead of
  // guessing a schema -- the first version of this page invented "perceive" and
  // silently showed nothing.
  var PREF = ['render', 'truth_step', 'adjudicate', 'metrics'];
  function stageRank(k) {
    var i = PREF.indexOf(k);
    if (i >= 0) return i * 100;
    if (k.indexOf('detect:') === 0) return 200;
    if (k.indexOf('predict:') === 0) return 300;
    return 400;
  }
  function stageKeys() {
    var set = {};
    DATA.runs.forEach(function (r) { Object.keys(r.stages).forEach(function (k) { set[k] = 1; }); });
    return Object.keys(set).sort(function (a, b) { return stageRank(a) - stageRank(b) || a.localeCompare(b); });
  }
  function shortKey(k) { return k.replace(/^detect:/, '').replace(/^predict:/, ''); }
  (function heat() {
    var host = document.getElementById('heat');
    var keys = stageKeys();
    var W = 1000, labW = 210, rowH = 34, top = keys.length > 6 ? 58 : 30, right = 16;
    var cellW = (W - labW - right) / keys.length;
    var H = top + DATA.runs.length * rowH + 10;
    var svg = el('svg', { viewBox: '0 0 ' + W + ' ' + H, 'class': 'chart', role: 'img', 'aria-label': '逐阶段延迟热力图' });
    keys.forEach(function (k, i) {
      var x = labW + i * cellW + cellW / 2;
      var t = txt(shortKey(k), 'tick', x, top - 8, keys.length > 6 ? 'end' : 'middle');
      if (keys.length > 6) t.setAttribute('transform', 'rotate(-32 ' + x + ' ' + (top - 8) + ')');
      svg.appendChild(t);
    });
    var maxLog = 0;
    DATA.runs.forEach(function (r) {
      keys.forEach(function (k) {
        var st = r.stages[k];
        if (st && st.p50 > 0) maxLog = Math.max(maxLog, Math.log10(st.p50));
      });
    });
    maxLog = Math.max(3, Math.ceil(maxLog));
    DATA.runs.forEach(function (r, ri) {
      var y = top + ri * rowH;
      svg.appendChild(txt(r.label, 'rowlab', 0, y + 21));
      svg.appendChild(txt(r.mode + ' · ' + r.ticks + ' ticks · ' + r.build, 'tick', labW - 8, y + 33, 'end'));
      keys.forEach(function (k, i) {
        var st = r.stages[k];
        var x = labW + i * cellW;
        var g = el('g', {});
        var rect = el('rect', { x: x + 2, y: y + 2, width: cellW - 4, height: rowH - 4, rx: 5, 'class': 'cellrect', fill: st ? 'var(--s1)' : 'var(--surface-2)', opacity: '0.08' });
        if (st) {
          var logv = st.p50 > 0 ? Math.log10(st.p50) : -3;
          var t2 = Math.min(1, Math.max(0, (logv + 3) / (maxLog + 3)));
          rect.setAttribute('opacity', (0.08 + 0.82 * t2).toFixed(3));
          var title = el('title', {});
          title.textContent = r.label + ' / ' + shortKey(k) + ': p50 ' + fmt(st.p50) + ', p95 ' + fmt(st.p95);
          g.appendChild(title);
        }
        g.appendChild(rect);
        var label = txt(st ? fmt(st.p50) : '·', 'val', x + cellW / 2, y + 21, 'middle');
        if (st) {
          var logv2 = st.p50 > 0 ? Math.log10(st.p50) : -3;
          if ((logv2 + 3) / (maxLog + 3) > 0.55) label.setAttribute('fill', 'var(--bg)');
        } else {
          label.setAttribute('fill', 'var(--muted)');
        }
        g.appendChild(label);
        svg.appendChild(g);
      });
    });
    host.appendChild(svg);
    var lg = document.createElement('p'); lg.className = 'note';
    lg.textContent = '列是工件里真实的阶段键（detect:/predict: 前缀已省略）；格子内为 p50，悬停看 p95。色深按 log10(ms)。内建 CV 与真实模型相差 4 个数量级——这是"模型在环"的固有成本，不是可优化项。';
    host.appendChild(lg);
  })();

  // ---- ECE: dots per producer + separation bars with 0.30 line ----
  (function eceChart() {
    var host = document.getElementById('ece');
    var W = 1000, H = 230, left = 230, right = 40, top = 30, axisMax = 0.7;
    var svg = el('svg', { viewBox: '0 0 ' + W + ' ' + H, 'class': 'chart', role: 'img', 'aria-label': 'ECE 与分离量' });
    function xOf(v) { return left + (v / axisMax) * (W - left - right); }
    [0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7].forEach(function (v) {
      svg.appendChild(el('line', { x1: xOf(v), y1: top - 8, x2: xOf(v), y2: H - 46, 'class': 'gridline' }));
      svg.appendChild(txt(v.toFixed(1), 'tick', xOf(v), H - 30, 'middle'));
    });
    DATA.separations.forEach(function (sep, i) {
      if (sep.honest.v === null) return;
      var y = top + i * 66;
      svg.appendChild(txt(sep.pair, 'rowlab', 0, y + 6));
      svg.appendChild(txt(sep.sample, 'tick', 0, y + 24));
      var a = xOf(sep.honest.v), b = xOf(sep.injected.v);
      svg.appendChild(el('line', { x1: a, y1: y - 4, x2: b, y2: y - 4, stroke: 'var(--muted)', 'stroke-width': 1.5, 'stroke-dasharray': '4 3' }));
      svg.appendChild(el('circle', { cx: a, cy: y - 4, r: 5, fill: 'var(--s1)' }));
      svg.appendChild(el('circle', { cx: b, cy: y - 4, r: 5, fill: 'var(--s2)' }));
      svg.appendChild(txt(sep.honest.name + ' ' + sep.honest.v.toFixed(4), 'val', a, y + 20, 'middle'));
      svg.appendChild(txt(sep.injected.name + ' ' + sep.injected.v.toFixed(4), 'val', b, y + 20, 'middle'));
      svg.appendChild(txt('分离量 ' + sep.separation.toFixed(5) + (sep.separation >= 0.3 ? ' ≥ 0.30' : ' < 0.30'), 'val', (a + b) / 2, y - 14, 'middle'));
      var bx = left + (sep.separation / axisMax) * (W - left - right);
      svg.appendChild(el('rect', { x: left, y: y + 30, width: Math.max(1, bx - left), height: 10, rx: 5, fill: 'var(--s3)' }));
    });
    var thr = el('line', { x1: xOf(0.3), y1: top - 10, x2: xOf(0.3), y2: H - 46, stroke: 'var(--bad)', 'stroke-width': 1.5, 'stroke-dasharray': '5 4' });
    svg.appendChild(thr);
    svg.appendChild(txt('0.30 阈值（作用于分离量）', 'tick', xOf(0.3) + 6, top - 14));
    host.appendChild(svg);
    var p = document.createElement('p'); p.className = 'note';
    p.textContent = '⚠ 两个面板里被注入错误的那个（橙色点）都是 ECE 更低的一方——数值上"分离"成立，方向却与判据意图相反。这就是判据 (c) 维持 PARTIAL 的直接证据（problem-log 012）。';
    host.appendChild(p);
  })();

  // ---- ledger composition ----
  (function ledger() {
    var host = document.getElementById('ledger');
    var W = 1000, rowH = 30, top = 26, left = 230, right = 30;
    var H = top + DATA.runs.length * rowH + 16;
    var svg = el('svg', { viewBox: '0 0 ' + W + ' ' + H, 'class': 'chart', role: 'img', 'aria-label': '账本构成' });
    var maxE = 1;
    DATA.runs.forEach(function (r) { maxE = Math.max(maxE, r.entries); });
    DATA.runs.forEach(function (r, i) {
      var y = top + i * rowH;
      svg.appendChild(txt(r.label, 'rowlab', 0, y + 18));
      var x = left, w = W - left - right;
      var parts = [['applied', 'var(--s1)'], ['announced', 'var(--s2)'], ['rejected', 'var(--bad)']];
      var total = Math.max(1, r.entries);
      parts.forEach(function (p) {
        var v = r[p[0]];
        if (!v) return;
        var sw = (v / total) * (r.entries / maxE) * w;
        svg.appendChild(el('rect', { x: x, y: y + 6, width: Math.max(0.8, sw), height: 14, fill: p[1] }));
        x += sw;
      });
      svg.appendChild(txt(r.entries + ' 条 · faults ' + r.faults, 'tick', W - right, y + 18, 'end'));
    });
    host.appendChild(svg);
    var lg = document.createElement('p'); lg.className = 'note';
    lg.innerHTML = '<span class="legend"><span><i style="background:var(--s1)"></i>applied</span><span><i style="background:var(--s2)"></i>announced</span><span><i style="background:var(--bad)"></i>rejected</span></span>';
    host.appendChild(lg);
  })();

  // ---- determinism ----
  (function det() {
    var host = document.getElementById('determinism');
    if (!DATA.determinism) { host.textContent = '未执行复跑。'; return; }
    var d = DATA.determinism;
    var t = document.createElement('table');
    t.innerHTML = '<thead><tr><th>项</th><th>第一次</th><th>复跑</th><th>结果</th></tr></thead><tbody>' +
      '<tr><td>world_hash</td><td class="num">' + d.a.world + '</td><td class="num">' + d.b.world + '</td><td>' + (d.a.world === d.b.world ? '<span class="badge ok">逐位相同</span>' : '<span class="badge bad">不同</span>') + '</td></tr>' +
      '<tr><td>ledger_hash</td><td class="num">' + d.a.ledger + '</td><td class="num">' + d.b.ledger + '</td><td>' + (d.a.ledger === d.b.ledger ? '<span class="badge ok">逐位相同</span>' : '<span class="badge bad">不同</span>') + '</td></tr>' +
      '<tr><td>账本条数</td><td class="num">' + d.a.entries + '</td><td class="num">' + d.b.entries + '</td><td>' + (d.a.entries === d.b.entries ? '<span class="badge ok">相同</span>' : '<span class="badge bad">不同</span>') + '</td></tr>' +
      '</tbody>';
    host.appendChild(t);
  })();

  // ---- tick timeline (animation over real stage durations, log-scaled) ----
  var tlState = { idx: 0, t: 0, playing: true };
  (function timeline() {
    var host = document.getElementById('timelineRuns');
    DATA.runs.forEach(function (r, i) {
      var b = document.createElement('button');
      b.textContent = r.label; b.setAttribute('aria-pressed', i === 0 ? 'true' : 'false');
      b.addEventListener('click', function () {
        tlState.idx = i; tlState.t = 0;
        Array.prototype.forEach.call(host.querySelectorAll('button'), function (o) { o.setAttribute('aria-pressed', 'false'); });
        b.setAttribute('aria-pressed', 'true');
        drawTimeline();
      });
      host.appendChild(b);
    });
    var play = document.createElement('button');
    play.textContent = '暂停'; play.setAttribute('aria-pressed', 'true');
    play.addEventListener('click', function () {
      tlState.playing = !tlState.playing;
      play.textContent = tlState.playing ? '暂停' : '播放';
      play.setAttribute('aria-pressed', tlState.playing ? 'true' : 'false');
      if (tlState.playing) { last = 0; requestAnimationFrame(step); }
    });
    host.appendChild(play);
  })();

  function drawTimeline() {
    var host = document.getElementById('timeline');
    host.innerHTML = '';
    var r = DATA.runs[tlState.idx];
    // Use the run's own stage keys: a CV panel has no detect:rtdetr_v2_s, and
    // inventing a fixed list would draw stages this run never executed.
    var stages = Object.keys(r.stages).sort(function (a, b) { return stageRank(a) - stageRank(b) || a.localeCompare(b); });
    var W = 1000, H = 150, left = 90, right = 30, top = 40;
    var svg = el('svg', { viewBox: '0 0 ' + W + ' ' + H, 'class': 'chart', role: 'img', 'aria-label': '单 tick 时间轴' });
    var vals = stages.map(function (s) { return Math.max(0.001, r.stages[s].p50); });
    var total = vals.reduce(function (a, b) { return a + b; }, 0);
    var minLog = Math.log10(Math.min.apply(null, vals)), maxLog = Math.log10(Math.max.apply(null, vals));
    function wOf(v) { return 0.08 + 0.92 * (Math.log10(v) - minLog) / Math.max(0.0001, maxLog - minLog); }
    var sumW = vals.reduce(function (a, v) { return a + wOf(v); }, 0);
    var usable = W - left - right;
    var x = left;
    var segs = vals.map(function (v, i) {
      var w = (wOf(v) / sumW) * usable;
      var s = { x: x, w: w, v: v, name: shortKey(stages[i]) };
      x += w;
      return s;
    });
    segs.forEach(function (s, i) {
      var rect = el('rect', { x: s.x + 2, y: top, width: Math.max(2, s.w - 4), height: 38, rx: 5, fill: 'var(--s' + ((i % 4) + 1) + ')', opacity: 0.35 });
      rect.setAttribute('data-seg', String(i));
      svg.appendChild(rect);
      // Stagger labels: a real-model run has ~10 stage keys and they collide.
      svg.appendChild(txt(s.name, 'tick', s.x + s.w / 2, top - (i % 2 === 0 ? 8 : 22), 'middle'));
      svg.appendChild(txt(fmt(s.v), 'val', s.x + s.w / 2, top + 24, 'middle'));
    });
    var head = el('line', { x1: left, y1: top - 4, x2: left, y2: top + 46, stroke: 'var(--fg)', 'stroke-width': 2 });
    svg.appendChild(head);
    svg.appendChild(txt('时间轴按 log10(ms) 展宽，段宽不代表真实时间比例', 'tick', left, H - 8));
    svg.appendChild(txt('各段 p50 之和 ' + fmt(total) + '（近似，不等于整 tick 墙钟）', 'rowlab', W - right, top + 70, 'end'));
    host.appendChild(svg);
    tlState.svg = svg; tlState.head = head; tlState.segs = segs;
  }

  var last = 0;
  function step(now) {
    if (!tlState.playing) return;
    if (!last) last = now;
    var dt = Math.min(64, now - last) / 1000;
    last = now;
    tlState.t += dt * 0.35;
    if (tlState.t > 1) tlState.t -= 1;
    var segs = tlState.segs;
    if (segs && tlState.head) {
      var totalW = segs[segs.length - 1].x + segs[segs.length - 1].w - segs[0].x;
      var xpos = segs[0].x + totalW * tlState.t;
      tlState.head.setAttribute('x1', xpos); tlState.head.setAttribute('x2', xpos);
      var active = 0;
      segs.forEach(function (s, i) { if (xpos >= s.x) active = i; });
      Array.prototype.forEach.call(tlState.svg.querySelectorAll('rect[data-seg]'), function (r2) {
        r2.setAttribute('opacity', Number(r2.getAttribute('data-seg')) === active ? '0.95' : '0.35');
      });
    }
    requestAnimationFrame(step);
  }

  // theme toggle
  var themeBtn = document.getElementById('theme');
  var THEMES = ['auto', 'light', 'dark'], LABEL = { auto: '主题：跟随系统', light: '主题：浅色', dark: '主题：深色' };
  themeBtn.addEventListener('click', function () {
    var next = THEMES[(THEMES.indexOf(document.documentElement.getAttribute('data-theme')) + 1) % THEMES.length];
    document.documentElement.setAttribute('data-theme', next);
    themeBtn.textContent = LABEL[next];
  });

  document.getElementById('footer').innerHTML =
    '本页由 <code>node tools/build_dashboard.mjs</code> 从真实运行工件生成，未手工编辑任何数字。' +
    '重新生成即可反映代码变化。数字若与 <a href="../benchmarks/regression/">benchmarks/regression/</a> 的正式报告冲突，以后者为准（报告带完整环境元数据与偏差声明）。' +
    '机制说明请看 <a href="onboarding-dataflow.html">数据流转图</a>。';

  drawTimeline();
  if (window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
    tlState.playing = false;
  } else {
    requestAnimationFrame(step);
  }
})();
</script>
</body>
</html>
`;

// Run reports carry the command line verbatim, which includes the absolute
// path of the binary. The page is published, so the local layout must not
// travel with it: collapse the repo root to <repo>/ before embedding.
const repoPrefixes = [ROOT, ROOT.replaceAll('\\', '/'), ROOT.replaceAll('/', '\\')];
let serialized = JSON.stringify(DATA);
for (const prefix of repoPrefixes) {
  serialized = serialized.split(JSON.stringify(prefix).slice(1, -1)).join('<repo>');
}
writeFileSync(OUT, HTML.replace('__DATA__', serialized));
console.log('\nwrote ' + OUT);
console.log('runs: ' + results.length + ' | skipped: ' + skipped.length);
