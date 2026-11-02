'use strict';
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const maps = JSON.parse(fs.readFileSync(path.join(root, 'docs/florrclone-maps-geometry.json'), 'utf8'));
const site = path.join(root, '../florrclone/florr-local-site');

// Find tile id -> filename map in client patches / page chunks
const patch = fs.readFileSync(path.join(root, '../florrclone/server/client-patches.js'), 'utf8');
const chunksDir = path.join(site, '_next/static/chunks');
let tileDefs = null;
for (const f of fs.readdirSync(chunksDir)) {
  if (!f.startsWith('page-') || !f.endsWith('.js')) continue;
  const s = fs.readFileSync(path.join(chunksDir, f), 'utf8');
  // look for grass_c_0.svg style definitions near numeric keys
  if (!s.includes('grass_c_0.svg')) continue;
  // common pattern: {1:"hel_c_0.svg",6:"grass_c_0.svg",...} or similar
  const idx = s.indexOf('grass_c_0.svg');
  // expand window around
  let lo = Math.max(0, idx - 4000);
  let hi = Math.min(s.length, idx + 4000);
  const window = s.slice(lo, hi);
  // try to extract object literal containing the svg names
  const m = window.match(/\{(?:\d+:`[^`]+`(?:,)?)+\}/);
  const m2 = window.match(/\{(?:\d+:"[^"]+"(?:,)?)+\}/);
  const candidate = m || m2;
  if (candidate) {
    try {
      tileDefs = Function(`"use strict"; return (${candidate[0].replace(/`/g, '"')});`)();
      console.log('tileDefs from', f, 'count', Object.keys(tileDefs).length);
      break;
    } catch (_) {}
  }
  // broader: find all id:filename pairs nearby
  const pairs = [...window.matchAll(/(\d+):[`"]([^`"']+\.svg)[`"]/g)];
  if (pairs.length > 20) {
    tileDefs = Object.fromEntries(pairs.map((p) => [p[1], p[2]]));
    console.log('tileDefs pairs from', f, 'count', Object.keys(tileDefs).length);
    break;
  }
}

if (!tileDefs) {
  // search whole page for world-tiles mapping
  for (const f of fs.readdirSync(chunksDir)) {
    if (!f.startsWith('page-') || !f.endsWith('.js')) continue;
    const s = fs.readFileSync(path.join(chunksDir, f), 'utf8');
    if (!s.includes('world-tiles/')) continue;
    const pairs = [...s.matchAll(/(\d+):[`"]([a-z0-9_]+\.svg)[`"]/gi)];
    if (pairs.length > 40) {
      tileDefs = Object.fromEntries(pairs.map((p) => [p[1], p[2]]));
      console.log('global pairs', f, Object.keys(tileDefs).length);
      break;
    }
  }
}

// Decode one layer to see value range
function decodeCells(layer) {
  const buf = Buffer.from(layer.cells, 'base64');
  // cells are palette indices (bytes); 0 = empty?
  const out = new Array(layer.w * layer.h);
  // encoding might be raw bytes of indices into palette (0 = empty, 1..n = palette[i-1])
  // Inspect histogram
  const hist = new Map();
  for (let i = 0; i < buf.length; i++) {
    const v = buf[i];
    hist.set(v, (hist.get(v) || 0) + 1);
  }
  return { bytes: buf.length, expected: layer.w * layer.h, hist: [...hist.entries()].sort((a, b) => a[0] - b[0]), palette: layer.palette };
}

const garden = maps.Garden;
console.log('Garden layers:');
for (const l of garden.layers) {
  const d = decodeCells(l);
  console.log(' ', l.name, 'order', l.order, d);
}

// cpp tileset image names -> id
const tsj = JSON.parse(fs.readFileSync(path.join(root, 'maps/tileset.tsj'), 'utf8'));
const cppByImage = {};
for (const t of tsj.tiles) {
  const name = String(t.image || '').replace(/^tiles\//, '').replace(/\\/g, '/');
  cppByImage[name] = t.id;
}
console.log('cpp tiles', tsj.tiles.length, 'sample', Object.entries(cppByImage).slice(0, 8));

if (tileDefs) {
  const mapping = {};
  const missing = [];
  for (const [id, file] of Object.entries(tileDefs)) {
    const cppId = cppByImage[file];
    mapping[id] = { file, cppId: cppId == null ? null : cppId };
    if (cppId == null) missing.push({ id, file });
  }
  fs.writeFileSync(path.join(root, 'docs/florr-tile-id-map.json'), JSON.stringify({ tileDefs, mapping, missing }, null, 2));
  console.log('missing in cpp tileset', missing.length, missing.slice(0, 20));
  console.log('mapped', Object.keys(mapping).length);
} else {
  console.log('NO tileDefs found');
  // list world-tiles
  const tiles = fs.readdirSync(path.join(site, 'assets/world-tiles')).filter((f) => f.endsWith('.svg'));
  console.log('world-tiles count', tiles.length);
  const missingCpp = tiles.filter((f) => cppByImage[f] == null);
  const missingFlor = Object.keys(cppByImage).filter((f) => !tiles.includes(f));
  console.log('in florr not cpp', missingCpp.slice(0, 30), 'count', missingCpp.length);
  console.log('in cpp not florr', missingFlor.slice(0, 30), 'count', missingFlor.length);
}
