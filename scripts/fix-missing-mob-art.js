'use strict';
/**
 * For every mob whose `image` is a `$marker` with no matching painter in
 * mob_art.cpp, embed the florrclone SVG. Also retarget a few known wrong
 * markers onto existing painters when no SVG is available.
 */
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const ASH = path.resolve(ROOT, '..');
const mobsPath = path.join(ROOT, 'data', 'mobs.json');
const artCpp = fs.readFileSync(path.join(ROOT, 'cpp', 'client', 'render', 'mob_art.cpp'), 'utf8');
const svgDir = path.join(ASH, 'florrclone', 'florr-local-site', 'assets', 'svg');
const srcDir = path.join(ASH, 'florrclone', 'florr-local-site', 'assets', 'mob-sources');

const painters = new Set();
for (const m of artCpp.matchAll(/if \(name == "([^"]+)"\)/g)) painters.add(m[1]);
for (const m of artCpp.matchAll(/\{\s*"([^"]+)"\s*,\s*MobArt::/g)) painters.add(m[1]);

/** Wrong glued markers → existing painter name. */
const MARKER_ALIAS = {
  testdummy: 'dummy',
  fireantburrow: 'fire_ant_burrow',
  desertladybug: 'ladybug', // fallback tint wrong; SVG preferred
  jungleladybug: 'ladybug',
  queenfireant: 'fire_ant_soldier', // closest; SVG preferred
};

function indexSvgs(dir) {
  const map = new Map();
  if (!fs.existsSync(dir)) return map;
  for (const f of fs.readdirSync(dir)) {
    if (!f.endsWith('.svg')) continue;
    let base = f.replace(/\.svg$/i, '').replace(/\.[0-9a-f]{8,}$/i, '');
    map.set(base.toLowerCase(), path.join(dir, f));
  }
  return map;
}

const fromSvg = indexSvgs(svgDir);
const fromSrc = indexSvgs(srcDir);

function findSvg(mobId, marker) {
  const variants = [
    'mob-' + mobId.replace(/_/g, '-'),
    'mob-' + marker.replace(/_/g, '-'),
    mobId.replace(/_/g, '-'),
    // official names that diverge from id
    mobId === 'beetle_mummy' ? 'mummy-beetle' : null,
    mobId === 'pharaoh_beetle' ? 'mob-pharaoh-beetle' : null,
  ].filter(Boolean).map((s) => s.toLowerCase());

  for (const c of variants) {
    if (fromSvg.has(c)) return fromSvg.get(c);
    if (fromSrc.has(c)) return fromSrc.get(c);
  }
  const needle = mobId.replace(/_/g, '-').toLowerCase();
  for (const map of [fromSvg, fromSrc]) {
    for (const [k, p] of map) {
      if (k === 'mob-' + needle || k === needle) return p;
    }
  }
  return null;
}

const mobs = JSON.parse(fs.readFileSync(mobsPath, 'utf8'));
const report = { toSvg: [], aliased: [], alreadyOk: [], unresolved: [] };

for (const [id, mob] of Object.entries(mobs)) {
  let img = mob.image || '';
  if (!img.startsWith('$')) {
    if (img.startsWith('<svg')) report.alreadyOk.push(id + ' (svg)');
    continue;
  }
  if (img.startsWith('$petal:') || img.startsWith('$sponge:')) {
    report.alreadyOk.push(id + ' (special)');
    continue;
  }

  let marker = img.slice(1).split(':')[0];
  if (painters.has(marker)) {
    report.alreadyOk.push(id + ':' + img);
    continue;
  }

  const file = findSvg(id, marker);
  if (file) {
    mob.image = fs.readFileSync(file, 'utf8');
    report.toSvg.push(id + ' ← ' + path.basename(file));
    continue;
  }

  const alias = MARKER_ALIAS[marker];
  if (alias && painters.has(alias)) {
    mob.image = '$' + alias;
    report.aliased.push(id + ': $' + marker + ' → $' + alias);
    continue;
  }

  report.unresolved.push(id + ':' + img);
}

fs.writeFileSync(mobsPath, JSON.stringify(mobs, null, 2) + '\n');
console.log(JSON.stringify({
  toSvg: report.toSvg,
  aliased: report.aliased,
  unresolved: report.unresolved,
  alreadyOk: report.alreadyOk.length,
}, null, 2));
