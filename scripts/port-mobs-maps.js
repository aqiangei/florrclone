'use strict';
/**
 * Port missing florrclone mobs into data/mobs.json and ensure map zone
 * distributions include the biome type lists from florrclone.
 */
const fs = require('node:fs');
const path = require('node:path');
const { loadEngine } = require('../../florrclone/server/engine');

const root = path.join(__dirname, '..');
const api = loadEngine(path.join(__dirname, '../../florrclone/florr-local-site'));
const mobsPath = path.join(root, 'data/mobs.json');
const dropsPath = path.join(root, 'data/mob_drops.json');
const mobs = JSON.parse(fs.readFileSync(mobsPath, 'utf8'));
const drops = JSON.parse(fs.readFileSync(dropsPath, 'utf8'));

const norm = (s) => String(s).toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, '');

/** florrclone biome → cpp map file + default group token used in zones */
const biomeToMap = {
  Garden: { file: 'garden.tmj', group: 'garden' },
  Desert: { file: 'desert.tmj', group: 'desert' },
  Ocean: { file: 'ocean.tmj', group: 'ocean' },
  Jungle: { file: 'jungle.tmj', group: 'jungle' },
  Hel: { file: 'hel.tmj', group: 'hel' },
  'Ant Hell': { file: 'ant_hell.tmj', group: 'ant_hell' },
  Sewers: { file: 'sewers.tmj', group: 'sewers' },
  Factory: { file: 'computer.tmj', group: 'computer' },
  Pyramid: { file: 'pyramid.tmj', group: 'pyramid' },
};

function resolveMobId(florName) {
  const n = norm(florName);
  if (mobs[n]) return n;
  for (const [id, m] of Object.entries(mobs)) {
    if (norm(m.name || '') === n) return id;
  }
  // common aliases
  const aliases = {
    ant_egg: 'ant_egg',
    baby_ant: 'baby_ant',
    hel_beetle: 'hel_beetle',
  };
  return aliases[n] || n;
}

function ensureMob(florName, groups) {
  const id = resolveMobId(florName);
  const flor = api.mobs[florName];
  if (!flor) return { id, status: 'no-flor-def' };
  if (mobs[id]) {
    const row = { ...mobs[id] };
    let changed = false;
    if (flor.damage != null && Number(row.damage) !== Number(flor.damage)) {
      row.damage = flor.damage;
      changed = true;
    }
    if (flor.hp != null && Number(row.health) !== Number(flor.hp)) {
      row.health = flor.hp;
      changed = true;
    }
    // merge groups
    const g = row.groups;
    if (Array.isArray(g)) {
      const set = new Set(g);
      for (const x of groups) if (!set.has(x)) {
        set.add(x);
        changed = true;
      }
      row.groups = [...set];
    } else if (g && typeof g === 'object') {
      for (const x of groups) if (g[x] == null) {
        g[x] = 1;
        changed = true;
      }
      row.groups = g;
    } else {
      row.groups = groups;
      changed = true;
    }
    if (changed) {
      mobs[id] = row;
      return { id, status: 'updated' };
    }
    return { id, status: 'exists' };
  }

  const aggro = !!flor.aggro;
  const row = {
    name: florName,
    damage: flor.damage ?? 10,
    health: flor.hp ?? 50,
    size: Math.max(0.5, (flor.radius ?? 25) / 30),
    speed: 0.1,
    chase_speed: aggro ? 0.6 : 0.1,
    cooldown: 2000,
    xp: {
      common: flor.xp ?? 1,
      uncommon: (flor.xp ?? 1) * 3,
      rare: (flor.xp ?? 1) * 30,
      epic: (flor.xp ?? 1) * 270,
      legendary: (flor.xp ?? 1) * 1400,
      mythic: (flor.xp ?? 1) * 32000,
      ultra: (flor.xp ?? 1) * 150000,
      super: (flor.xp ?? 1) * 1200000,
      unique: (flor.xp ?? 1) * 6800000,
    },
    description: florName,
    color: '#888888',
    image: '$' + id.replace(/_/g, ''),
    ai_type: aggro ? 'hostile' : 'passive',
    range: 100,
    groups,
    florrclonePort: true,
  };
  // segment-ish / special hints
  if (/centipede/i.test(florName)) {
    row.ai_type = 'hostile';
    row.segments = 10;
  }
  if (/hole|burrow|mound|tomb|spawner|egg/i.test(florName)) {
    row.speed = 0;
    row.chase_speed = 0;
    row.ai_type = 'stationary';
  }
  if (/queen|overmind|pharaoh|gambler/i.test(florName)) {
    row.ai_type = 'hostile';
    row.health = Math.max(row.health, 200);
  }
  mobs[id] = row;

  // minimal drop table from flor.drops
  if (Array.isArray(flor.drops) && flor.drops.length && !drops[id]) {
    const petals = JSON.parse(fs.readFileSync(path.join(root, 'data/petals.json'), 'utf8'));
    const dropList = [];
    for (const name of flor.drops) {
      const key = norm(name);
      const itemType = petals[key] ? key : Object.keys(petals).find((k) => norm(petals[k].name || '') === key);
      if (!itemType) continue;
      dropList.push({
        type: 'petal',
        itemType,
        rarity: 'common',
        probability: 0.08,
        minQuantity: 1,
        maxQuantity: 1,
      });
    }
    if (dropList.length) drops[id] = { guaranteed: false, drops: dropList };
  }
  return { id, status: 'added' };
}

const added = [];
const updated = [];
const biomeMobs = {};

for (const [biome, meta] of Object.entries(biomeToMap)) {
  const types = [...new Set(api.maps[biome]?.types || [])];
  biomeMobs[biome] = types;
  for (const t of types) {
    const r = ensureMob(t, [meta.group]);
    if (r.status === 'added') added.push({ flor: t, id: r.id, group: meta.group });
    if (r.status === 'updated') updated.push({ flor: t, id: r.id });
  }
}

// Also ensure every flor mob exists somewhere (Swarm/Super catch-all → garden weight low)
for (const florName of Object.keys(api.mobs)) {
  const id = resolveMobId(florName);
  if (mobs[id]) continue;
  const r = ensureMob(florName, ['garden']);
  if (r.status === 'added') added.push({ flor: florName, id: r.id, group: 'garden' });
}

/**
 * Rewrite each map's zone `mobs` property to:
 *   <group> 70% + each biome-unique type equal share of 30%
 * keeping difficulty bands intact. Zones that already mention the group keep structure.
 */
function patchMap(file, group, florTypes) {
  const p = path.join(root, 'maps', file);
  if (!fs.existsSync(p)) return { file, status: 'missing-file' };
  const raw = fs.readFileSync(p, 'utf8');
  const unique = [...new Set(florTypes)];
  // Build distribution string: group 50% + each type equal among remaining 50%
  const typeWeight = unique.length ? (50 / unique.length) : 0;
  const parts = [`${group} 50%`];
  for (const t of unique) {
    const id = resolveMobId(t);
    if (!mobs[id]) continue;
    // Prefer using mob id in distribution (content registry id)
    parts.push(`${id} ${typeWeight.toFixed(2)}%`);
  }
  const dist = parts.join(' ');

  // Tiled props are pretty-printed across lines:
  //   "name":"mobs",
  //   "type":"string",
  //   "value":"garden"
  let count = 0;
  const next = raw.replace(
    /("name"\s*:\s*"mobs"\s*,\s*"type"\s*:\s*"string"\s*,\s*"value"\s*:\s*")([^"]*)(")/g,
    (_, a, _old, c) => {
      count++;
      return `${a}${dist}${c}`;
    },
  );
  if (count === 0) {
    return { file, status: 'no-mobs-props', count: 0 };
  }
  fs.writeFileSync(p, next);
  return { file, status: 'patched', count, dist };
}

const mapPatches = [];
for (const [biome, meta] of Object.entries(biomeToMap)) {
  mapPatches.push(patchMap(meta.file, meta.group, biomeMobs[biome] || []));
}

fs.writeFileSync(mobsPath, JSON.stringify(mobs, null, 2) + '\n');
fs.writeFileSync(dropsPath, JSON.stringify(drops, null, 2) + '\n');

const report = {
  added,
  updatedCount: updated.length,
  mobCount: Object.keys(mobs).length,
  mapPatches,
  stillMissingFlor: Object.keys(api.mobs).filter((n) => !mobs[resolveMobId(n)]),
};
fs.mkdirSync(path.join(root, 'docs'), { recursive: true });
fs.writeFileSync(path.join(root, 'docs/port-mobs-maps-report.json'), JSON.stringify(report, null, 2));
console.log(JSON.stringify({
  added: added.length,
  updated: updated.length,
  mobCount: report.mobCount,
  mapPatches: mapPatches.map((p) => ({ file: p.file, status: p.status, count: p.count })),
  stillMissing: report.stillMissingFlor,
  sampleAdded: added.slice(0, 25),
}, null, 2));
