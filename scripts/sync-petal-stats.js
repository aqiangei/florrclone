'use strict';
/**
 * Copy florrclone petal damage / hp / reload into florrclone-cpp data/petals.json
 * for entries that resolve through normalization or content-aliases.
 * Does not touch size, art, or C++ special behaviours. Dry-run: --dry
 */
const fs = require('node:fs');
const path = require('node:path');
const { loadEngine } = require('../../florrclone/server/engine');
const { petalFlorrToCpp } = require('./content-aliases');

const dry = process.argv.includes('--dry');
const root = path.join(__dirname, '..');
const petalsPath = path.join(root, 'data/petals.json');
const api = loadEngine(path.join(__dirname, '../../florrclone/florr-local-site'));
const cpp = JSON.parse(fs.readFileSync(petalsPath, 'utf8'));

const norm = (s) => String(s).toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, '');
const byNorm = new Map();
for (const [key, v] of Object.entries(cpp)) {
  byNorm.set(norm(key), key);
  if (v.name) byNorm.set(norm(v.name), key);
}

const changes = [];
const unresolved = [];
for (const [florName, flor] of Object.entries(api.petals)) {
  let cppKey = petalFlorrToCpp[florName];
  if (!cppKey) cppKey = byNorm.get(norm(florName));
  if (!cppKey || !cpp[cppKey]) {
    unresolved.push(florName);
    continue;
  }
  const row = cpp[cppKey];
  const next = { ...row };
  let changed = false;
  if (flor.damage != null && Number(row.damage) !== Number(flor.damage)) {
    next.damage = flor.damage;
    changed = true;
  }
  if (flor.hp != null && Number(row.health) !== Number(flor.hp)) {
    next.health = flor.hp;
    changed = true;
  }
  if (flor.reload != null) {
    const ms = Math.round(Number(flor.reload) * 1000);
    if (Number(row.cooldown) !== ms) {
      next.cooldown = ms;
      changed = true;
    }
  }
  if (flor.zh && row.name && !row.zh) {
    // optional: store zh alongside without removing English name used by UI
    next.zh = flor.zh;
    changed = true;
  } else if (flor.zh && row.zh !== flor.zh) {
    next.zh = flor.zh;
    changed = true;
  }
  if (!changed) continue;
  changes.push({
    florName,
    cppKey,
    before: { damage: row.damage, health: row.health, cooldown: row.cooldown },
    after: { damage: next.damage, health: next.health, cooldown: next.cooldown, zh: next.zh },
  });
  cpp[cppKey] = next;
}

const report = { dry, updated: changes.length, unresolvedCount: unresolved.length, changes, unresolved };
fs.mkdirSync(path.join(root, 'docs'), { recursive: true });
fs.writeFileSync(path.join(root, 'docs/sync-petal-stats-report.json'), JSON.stringify(report, null, 2));
if (!dry && changes.length) {
  fs.writeFileSync(petalsPath, JSON.stringify(cpp, null, 2) + '\n');
}
console.log(JSON.stringify({ dry, updated: changes.length, unresolvedCount: unresolved.length, sample: changes.slice(0, 8) }, null, 2));
