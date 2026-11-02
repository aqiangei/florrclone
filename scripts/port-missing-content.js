'use strict';
/**
 * Finish florrclone content-table port into data/:
 * - add missing petals (stats + placeholder SVG + mapped special fields)
 * - sync shared mob damage/health (not speed — units differ)
 * - map Rock-style drop petal names onto mob_drops where possible
 *
 * Presentation / feel stay reference. Rebuild dist via `npm run build:wsl`.
 */
const fs = require('node:fs');
const path = require('node:path');
const { loadEngine } = require('../../florrclone/server/engine');
const { petalFlorrToCpp } = require('./content-aliases');

const root = path.join(__dirname, '..');
const api = loadEngine(path.join(__dirname, '../../florrclone/florr-local-site'));
const petalsPath = path.join(root, 'data/petals.json');
const mobsPath = path.join(root, 'data/mobs.json');
const dropsPath = path.join(root, 'data/mob_drops.json');
const petals = JSON.parse(fs.readFileSync(petalsPath, 'utf8'));
const mobs = JSON.parse(fs.readFileSync(mobsPath, 'utf8'));
const drops = JSON.parse(fs.readFileSync(dropsPath, 'utf8'));

const norm = (s) => String(s).toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, '');

function placeholderSvg(name, color = '#90EE90') {
  const label = String(name).slice(0, 8).replace(/[<>&"]/g, '');
  return `<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32" fill="none"><circle cx="16" cy="16" r="13" fill="white" stroke="${color}" stroke-width="4"/><text x="16" y="20" text-anchor="middle" font-size="7" fill="#333">${label}</text></svg>`;
}

function mobIdFromHatch(hatch) {
  if (!hatch) return null;
  const n = norm(hatch);
  if (mobs[n]) return n;
  for (const [id, m] of Object.entries(mobs)) {
    if (norm(m.name || id) === n) return id;
  }
  return n; // leave id for later mob port
}

function resolveCppPetalKey(florName) {
  if (petalFlorrToCpp[florName] && petals[petalFlorrToCpp[florName]]) return petalFlorrToCpp[florName];
  const n = norm(florName);
  if (petals[n]) return n;
  for (const [id, p] of Object.entries(petals)) {
    if (norm(p.name || id) === n) return id;
  }
  return null;
}

const added = [];
const updated = [];
for (const [florName, flor] of Object.entries(api.petals)) {
  let key = resolveCppPetalKey(florName);
  if (!key) {
    key = norm(florName);
    const row = {
      name: flor.name || florName,
      zh: flor.zh || florName,
      damage: flor.damage ?? 0,
      health: flor.invulnerable || flor.hp == null ? null : flor.hp,
      size: 1,
      cooldown: Math.round(Number(flor.reload ?? 2.5) * 1000),
      price: 10,
      description: flor.description || florName,
      color: '#90EE90',
      count: 1,
      image: placeholderSvg(flor.zh || florName),
      florrclonePort: true,
    };
    if (flor.minRarity != null) row.minRarity = ['common', 'unusual', 'rare', 'epic', 'legendary', 'mythic', 'ultra', 'super', 'unique'][flor.minRarity] || 'common';
    if (flor.hatch) {
      row.petMobType = mobIdFromHatch(flor.hatch);
      row.petMobRarity = 'common';
      if (flor.summonCount) row.petCount = flor.summonCount;
    }
    if (flor.wearable || flor.noPhysics) row.noPhysics = true;
    if (flor.defendOnly) row.defendOnly = true;
    if (flor.healPerSecond) {
      row.passiveHeal = flor.healPerSecond;
    }
    petals[key] = row;
    added.push({ florName, key });
    continue;
  }
  const row = { ...petals[key] };
  let changed = false;
  if (flor.damage != null && Number(row.damage) !== Number(flor.damage)) {
    row.damage = flor.damage;
    changed = true;
  }
  if (flor.hp != null && !flor.invulnerable && Number(row.health) !== Number(flor.hp)) {
    row.health = flor.hp;
    changed = true;
  }
  if (flor.reload != null) {
    const ms = Math.round(Number(flor.reload) * 1000);
    if (Number(row.cooldown) !== ms) {
      row.cooldown = ms;
      changed = true;
    }
  }
  if (flor.zh && row.zh !== flor.zh) {
    row.zh = flor.zh;
    changed = true;
  }
  if (changed) {
    petals[key] = row;
    updated.push({ florName, key });
  }
}

// Mob damage/health sync for shared names (skip speed).
const mobIndex = new Map();
for (const [id, m] of Object.entries(mobs)) {
  mobIndex.set(norm(id), id);
  if (m.name) mobIndex.set(norm(m.name), id);
}
const mobUpdated = [];
for (const [florName, flor] of Object.entries(api.mobs)) {
  const id = mobIndex.get(norm(florName));
  if (!id) continue;
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
  if (changed) {
    mobs[id] = row;
    mobUpdated.push({ florName, id, damage: row.damage, health: row.health });
  }
}

// Drop itemType remap for petals that exist under aliased keys.
const dropRemap = {
  heavy: resolveCppPetalKey('Heavy') || 'heavy',
  moon_rock: resolveCppPetalKey('Moon Rock') || 'moon_rock',
  rock: 'rock',
};
let dropTouched = 0;
for (const [mobId, table] of Object.entries(drops)) {
  if (!table?.drops) continue;
  for (const d of table.drops) {
    if (d.type !== 'petal' || !d.itemType) continue;
    const n = norm(d.itemType);
    if (petals[d.itemType]) continue;
    if (petals[n]) {
      d.itemType = n;
      dropTouched++;
      continue;
    }
    // try flor display → cpp
    for (const [florName, flor] of Object.entries(api.petals)) {
      if (norm(florName) === n || norm(flor.name) === n) {
        const key = resolveCppPetalKey(florName) || norm(florName);
        if (petals[key]) {
          d.itemType = key;
          dropTouched++;
        }
        break;
      }
    }
  }
  // Ensure Rock-like flor drop names appear on matching mobs at low chance if missing.
  const florMob = api.mobs[Object.keys(api.mobs).find((k) => norm(k) === norm(mobId) || norm(api.mobs[k].asset || '') === norm(mobId))];
}

// Append missing drop petals listed on flor Rock for rock mob.
if (drops.rock && api.mobs.Rock?.drops) {
  const existing = new Set(drops.rock.drops.filter((d) => d.type === 'petal').map((d) => d.itemType));
  for (const name of api.mobs.Rock.drops) {
    const key = resolveCppPetalKey(name) || norm(name);
    if (!petals[key] || existing.has(key)) continue;
    drops.rock.drops.push({
      type: 'petal',
      itemType: key,
      rarity: 'common',
      probability: 0.05,
      minQuantity: 1,
      maxQuantity: 1,
    });
    existing.add(key);
    dropTouched++;
  }
}

fs.writeFileSync(petalsPath, JSON.stringify(petals, null, 2) + '\n');
fs.writeFileSync(mobsPath, JSON.stringify(mobs, null, 2) + '\n');
fs.writeFileSync(dropsPath, JSON.stringify(drops, null, 2) + '\n');

const report = {
  petalsAdded: added,
  petalsUpdated: updated.length,
  mobsUpdated: mobUpdated,
  dropTouched,
  petalCount: Object.keys(petals).length,
  unresolvedStill: Object.keys(api.petals).filter((n) => !resolveCppPetalKey(n) && !petals[norm(n)]),
};
fs.mkdirSync(path.join(root, 'docs'), { recursive: true });
fs.writeFileSync(path.join(root, 'docs/port-missing-content-report.json'), JSON.stringify(report, null, 2));
console.log(
  JSON.stringify(
    {
      added: added.length,
      petalsUpdated: updated.length,
      mobsUpdated: mobUpdated.length,
      dropTouched,
      petalCount: report.petalCount,
      sampleAdded: added.slice(0, 15),
    },
    null,
    2,
  ),
);
