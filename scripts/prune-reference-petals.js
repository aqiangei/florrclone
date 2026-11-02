'use strict';
/**
 * Keep only petals that exist in florrclone (by name / content-aliases).
 * Drop every reference-only petal from data/petals.json and scrub drop rows
 * that name removed petal ids.
 */
const fs = require('node:fs');
const path = require('node:path');
const { loadEngine } = require('../../florrclone/server/engine');
const { petalFlorrToCpp } = require('./content-aliases');

const root = path.join(__dirname, '..');
const petalsPath = path.join(root, 'data/petals.json');
const dropsPath = path.join(root, 'data/mob_drops.json');

const api = loadEngine(path.join(root, '../florrclone/florr-local-site'));
const petals = JSON.parse(fs.readFileSync(petalsPath, 'utf8'));
const drops = JSON.parse(fs.readFileSync(dropsPath, 'utf8'));

const norm = (s) => String(s).toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, '');

const keep = new Set();
const florList = Object.keys(api.petals || {});
for (const florName of florList) {
  const aliased = petalFlorrToCpp[florName];
  if (aliased) keep.add(aliased);
  keep.add(norm(florName));
  // also keep if a cpp petal uses the display name
  for (const [id, row] of Object.entries(petals)) {
    if (norm(row.name || '') === norm(florName)) keep.add(id);
  }
}
// Always keep basic for loadout / starter kit.
keep.add('basic');

const removed = [];
const kept = {};
for (const [id, row] of Object.entries(petals)) {
  if (keep.has(id)) {
    kept[id] = row;
  } else {
    removed.push(id);
  }
}

const removedSet = new Set(removed);
let dropRowsScrubbed = 0;
for (const [mobId, table] of Object.entries(drops)) {
  if (!table || !Array.isArray(table.drops)) continue;
  const before = table.drops.length;
  table.drops = table.drops.filter((d) => {
    if (d.type !== 'petal') return true;
    const item = d.itemType || d.petal || d.id;
    if (!item) return true;
    return !removedSet.has(String(item));
  });
  dropRowsScrubbed += before - table.drops.length;
}

fs.writeFileSync(petalsPath, JSON.stringify(kept, null, 2) + '\n');
fs.writeFileSync(dropsPath, JSON.stringify(drops, null, 2) + '\n');

const report = {
  florPetals: florList.length,
  kept: Object.keys(kept).length,
  removedCount: removed.length,
  removed,
  dropRowsScrubbed,
  stillMissingFlor: florList.filter((n) => {
    const id = petalFlorrToCpp[n] || norm(n);
    return !kept[id] && !Object.values(kept).some((p) => norm(p.name || '') === norm(n));
  }),
};
fs.mkdirSync(path.join(root, 'docs'), { recursive: true });
fs.writeFileSync(path.join(root, 'docs/prune-reference-petals-report.json'), JSON.stringify(report, null, 2));
console.log(JSON.stringify({
  florPetals: report.florPetals,
  kept: report.kept,
  removedCount: report.removedCount,
  removed: report.removed,
  dropRowsScrubbed: report.dropRowsScrubbed,
  stillMissingFlor: report.stillMissingFlor,
}, null, 2));
