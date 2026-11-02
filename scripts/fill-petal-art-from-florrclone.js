'use strict';
/**
 * Replace text-circle placeholder petal SVGs with florrclone wiki PNG cutouts
 * embedded as <svg><image href="data:image/png;base64,..."/></svg>.
 */
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const ASH = path.resolve(ROOT, '..');
const petalsPath = path.join(ROOT, 'data', 'petals.json');
const assets = path.join(ASH, 'florrclone', 'florr-local-site', 'assets');
const manifest = JSON.parse(fs.readFileSync(path.join(assets, 'manifest.json'), 'utf8'));

function isPlaceholder(img) {
  if (!img || typeof img !== 'string') return true;
  return img.includes('font-size') && img.includes('<text');
}

function resolveFile(src) {
  if (!src) return null;
  const rel = String(src).replace(/^\//, '').replace(/^assets\//, '');
  const full = path.join(assets, rel);
  return fs.existsSync(full) ? full : null;
}

function manifestEntry(id, name) {
  const aliases = [name, id, id.replace(/_/g, ' ')];
  if (id === 'mysterious_stick') aliases.push('Mysterious Stick', 'Stick');
  if (id === 'dark_mark') aliases.push('Mark', 'Dark Mark');
  if (id === 'pharaoh_s_crown') aliases.push("Pharaoh's Crown", 'Pharaohs Crown');
  if (id === 'egg') aliases.push('Ant Egg');
  for (const a of aliases) {
    if (manifest[a]) return { key: a, entry: manifest[a] };
  }
  const lower = String(name || '').toLowerCase();
  for (const k of Object.keys(manifest)) {
    if (k.toLowerCase() === lower) return { key: k, entry: manifest[k] };
  }
  return null;
}

function pngToSvg(filePath, entry) {
  const buf = fs.readFileSync(filePath);
  const b64 = buf.toString('base64');
  const w = entry.width || 290;
  const h = entry.height || 290;
  // Use a square viewBox so inventory tiles frame consistently.
  const side = Math.max(w, h);
  const x = (side - w) / 2;
  const y = (side - h) / 2;
  return (
    `<svg xmlns="http://www.w3.org/2000/svg" width="${side}" height="${side}" viewBox="0 0 ${side} ${side}" fill="none">` +
    `<image href="data:image/png;base64,${b64}" x="${x}" y="${y}" width="${w}" height="${h}" preserveAspectRatio="xMidYMid meet"/>` +
    `</svg>`
  );
}

const petals = JSON.parse(fs.readFileSync(petalsPath, 'utf8'));
const filled = [];
const missing = [];
let bytes = 0;

for (const [id, petal] of Object.entries(petals)) {
  if (!isPlaceholder(petal.image)) continue;
  const hit = manifestEntry(id, petal.name);
  if (!hit) {
    missing.push(id + ' (no manifest)');
    continue;
  }
  const file = resolveFile(hit.entry.src);
  if (!file) {
    missing.push(id + ' (missing ' + hit.entry.src + ')');
    continue;
  }
  const svg = pngToSvg(file, hit.entry);
  petal.image = svg;
  bytes += svg.length;
  filled.push(id);
}

fs.writeFileSync(petalsPath, JSON.stringify(petals, null, 2) + '\n');
console.log(JSON.stringify({
  filled: filled.length,
  filled,
  missing,
  approxImageBytesMB: +(bytes / 1024 / 1024).toFixed(2),
}, null, 2));
