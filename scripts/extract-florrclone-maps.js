'use strict';
/**
 * Extract florrclone client map geometry (tile layers) from the page chunk
 * and summarise biome spawn metadata from the engine.
 */
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const site = path.join(root, '../florrclone/florr-local-site');
const chunks = path.join(site, '_next/static/chunks');
const outDir = path.join(root, 'docs');

const { loadEngine } = require('../../florrclone/server/engine');
const api = loadEngine(site);

const files = fs.readdirSync(chunks).filter((f) => f.startsWith('page-') && f.endsWith('.js'));
let maps = null;
let sourceFile = null;
for (const f of files) {
  const s = fs.readFileSync(path.join(chunks, f), 'utf8');
  if (!s.includes('"Garden":{"slug":"garden"') && !s.includes('"Garden":{"slug":"garden"')) {
    // also tolerate minified without escapes
    if (!s.includes('slug":"garden"') || !s.includes('"Garden"')) continue;
  }
  const start = s.indexOf('JSON.parse(`');
  if (start < 0) continue;
  const from = start + 'JSON.parse(`'.length;
  const end = s.indexOf('`)', from);
  if (end < 0) continue;
  const raw = s.slice(from, end);
  try {
    const data = JSON.parse(raw);
    if (data.Garden && data.Garden.slug === 'garden') {
      maps = data;
      sourceFile = f;
      break;
    }
  } catch (e) {
    console.error('parse fail', f, e.message);
  }
}

if (!maps) {
  // fallback: locate Garden object start
  for (const f of files) {
    const s = fs.readFileSync(path.join(chunks, f), 'utf8');
    const marker = '{"Garden":{"slug":"garden"';
    const i = s.indexOf(marker);
    if (i < 0) continue;
    // walk back to JSON.parse(`
    const back = s.lastIndexOf('JSON.parse(`', i);
    if (back < 0) continue;
    const from = back + 'JSON.parse(`'.length;
    const end = s.indexOf('`)', from);
    maps = JSON.parse(s.slice(from, end));
    sourceFile = f;
    break;
  }
}

if (!maps) {
  console.error('Could not find embedded map geometry in page chunks');
  process.exit(1);
}

const summary = {};
for (const [name, m] of Object.entries(maps)) {
  summary[name] = {
    slug: m.slug,
    columns: m.columns,
    rows: m.rows,
    layers: (m.layers || []).map((l) => ({
      name: l.name,
      order: l.order,
      w: l.w,
      h: l.h,
      x: l.x,
      y: l.y,
      palette: l.palette,
      cellsBytes: Buffer.from(l.cells || '', 'base64').length,
    })),
    otherKeys: Object.keys(m).filter((k) => !['slug', 'columns', 'rows', 'layers'].includes(k)),
  };
}

const biomes = {};
for (const [name, m] of Object.entries(api.maps || {})) {
  biomes[name] = {
    zh: m.zh,
    bg: m.bg,
    dark: m.dark,
    light: m.light,
    types: m.types,
    difficulty: m.difficulty,
    population: m.population,
    spawnInterval: m.spawnInterval,
    spawnBatch: m.spawnBatch,
    maxMobRarity: m.maxMobRarity,
    test: m.test,
  };
}

fs.mkdirSync(outDir, { recursive: true });
fs.writeFileSync(path.join(outDir, 'florrclone-maps-geometry.json'), JSON.stringify(maps));
fs.writeFileSync(
  path.join(outDir, 'florrclone-maps-summary.json'),
  JSON.stringify({ sourceFile, mapNames: Object.keys(maps), summary, biomes }, null, 2),
);

console.log(JSON.stringify({
  sourceFile,
  mapNames: Object.keys(maps),
  sizes: Object.fromEntries(
    Object.entries(maps).map(([k, v]) => [k, `${v.columns}x${v.rows} L${(v.layers || []).length}`]),
  ),
  biomeNames: Object.keys(biomes),
  geometryOnly: Object.keys(maps).filter((k) => !biomes[k]),
  biomeOnly: Object.keys(biomes).filter((k) => !maps[k]),
}, null, 2));
