'use strict';
/**
 * Pull florrclone client spawn-zone polygons (difficulty / density / mobs)
 * out of the page chunk into docs/florrclone-spawn-zones.json.
 */
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const chunks = path.join(root, '../florrclone/florr-local-site/_next/static/chunks');
const outPath = path.join(root, 'docs/florrclone-spawn-zones.json');

const files = fs.readdirSync(chunks).filter((f) => f.startsWith('page-') && f.endsWith('.js'));
let source = null;
let fileName = null;
for (const f of files) {
  const s = fs.readFileSync(path.join(chunks, f), 'utf8');
  if (!s.includes('"mobs":"garden"') || !s.includes('forceRarity')) continue;
  source = s;
  fileName = f;
  break;
}
if (!source) throw new Error('page chunk with spawn zones not found');

// Zones are authored as JSON-ish objects embedded in the minified bundle.
const re =
  /\{"id":(\d+),"points":(\[[\s\S]*?\]),"point":(true|false),"properties":\{[^}]*?\},"mobs":"([^"]*)","difficulty":(null|-?[\d.]+),"forceRarity":(null|-?[\d.]+),"density":(null|-?[\d.]+),"extraSpawnDelay":(null|-?[\d.]+),"team":(null|"[^"]*"|[\d.]+)\}/g;

const zones = [];
let m;
while ((m = re.exec(source))) {
  let points;
  try {
    points = JSON.parse(m[2]);
  } catch {
    continue;
  }
  if (!Array.isArray(points) || points.length < 3) continue;
  const mobs = m[4].replace(/\\n/g, ' ').trim();
  zones.push({
    id: Number(m[1]),
    points, // fractional [x,y] in map space 0..1
    point: m[3] === 'true',
    mobs,
    difficulty: m[5] === 'null' ? null : Number(m[5]),
    forceRarity: m[6] === 'null' ? null : Number(m[6]),
    density: m[7] === 'null' ? null : Number(m[7]),
    extraSpawnDelay: m[8] === 'null' ? null : Number(m[8]),
    team: m[9] === 'null' ? null : String(m[9]).replace(/^"|"$/g, ''),
  });
}

/** Guess which biome a zone belongs to from its mobs string. */
function biomeOf(z) {
  const s = z.mobs;
  if (/mecha_|barrel|assembler|gambler|chip/i.test(s)) return 'Factory';
  if (/^garden\b|\bgarden\b/i.test(s) && !/hel|fire_ant|termite|sewer|ocean|desert|jungle|mecha/i.test(s))
    return 'Garden';
  if (/\bdesert\b|sandstorm|cactus|beetle(?!_hel|_mummy)|fire_ant_burrow/i.test(s) && !/mecha/i.test(s))
    return 'Desert';
  if (/\bocean\b|jellyfish|starfish|leech|sponge|shell|bubble|crab\b/i.test(s) && !/mecha|hel/i.test(s))
    return 'Ocean';
  if (/\bjungle\b|termite|leafbug|firefly|mantis|wasp\b|bush\b/i.test(s) && !/hel|mecha|sewer/i.test(s))
    return 'Jungle';
  if (/\bhel\b|beetle_hel|wasp_hel|spider_hel|centipede_hel/i.test(s)) return 'Hel';
  if (/\bsewers?\b|roach|garbage|silverfish|moth\b/i.test(s)) return 'Sewers';
  if (/ant_hole|ant_queen|ant_baby|ant_worker|ant_soldier|ant_egg/i.test(s) && !/fire_ant|termite/i.test(s))
    return 'Ant Hell';
  if (/fire_ant|termite_egg|pyramid|beetle_mummy/i.test(s)) return 'Pyramid';
  if (/worm\b/i.test(s) && /queen|overmind/i.test(s)) return 'Worm';
  // density-3 ant hell-ish bands that appear on Super overlays
  if (z.density === 3 && /ant_|fire_ant|termite|worm/i.test(s)) return 'Super';
  if (/\bgarden\b/i.test(s)) return 'Garden';
  if (/\bdesert\b/i.test(s)) return 'Desert';
  return 'Unknown';
}

const byBiome = {};
for (const z of zones) {
  const b = biomeOf(z);
  (byBiome[b] ||= []).push(z);
}

const summary = {};
for (const [b, list] of Object.entries(byBiome)) {
  const diffs = [...new Set(list.map((z) => z.difficulty))].sort((a, b) => (a ?? -999) - (b ?? -999));
  const dens = [...new Set(list.map((z) => z.density))];
  summary[b] = { count: list.length, difficulties: diffs, densities: dens };
}

fs.writeFileSync(outPath, JSON.stringify({ source: fileName, summary, byBiome }, null, 2));
console.log(JSON.stringify({ source: fileName, total: zones.length, summary }, null, 2));
