'use strict';
/**
 * Replace florrclone-cpp maps/*.tmj with geometry converted from florrclone
 * (client tile layers + engine biome/portal metadata). Extends tileset with
 * any missing florrclone world-tiles.
 */
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const site = path.join(root, '../florrclone/florr-local-site');
const florTilesDir = path.join(site, 'assets/world-tiles');
const mapsDir = path.join(root, 'maps');
const tilesDir = path.join(mapsDir, 'tiles');
const backupDir = path.join(root, 'maps_ref_backup');

const { loadEngine } = require('../../florrclone/server/engine');
const api = loadEngine(site);

const TILE = 256;
const FLIP_H = 0x80000000;
const FLIP_V = 0x40000000;
const FLIP_D = 0x20000000;
const FLIP_MASK = FLIP_H | FLIP_V | FLIP_D;

const nameToFile = {
  Garden: 'garden.tmj',
  Desert: 'desert.tmj',
  Ocean: 'ocean.tmj',
  Jungle: 'jungle.tmj',
  Hel: 'hel.tmj',
  'Ant Hell': 'ant_hell.tmj',
  Sewers: 'sewers.tmj',
  // File stays computer.tmj (mob group / legacy id); lobby label is Factory.
  Factory: 'computer.tmj',
  Pyramid: 'pyramid.tmj',
  Worm: 'worm.tmj',
  'dps test': 'dps_test.tmj',
  Swarm: 'swarm.tmj',
  Super: 'super.tmj',
};

const groupOf = {
  Garden: 'garden',
  Desert: 'desert',
  Ocean: 'ocean',
  Jungle: 'jungle',
  Hel: 'hel',
  'Ant Hell': 'ant_hell',
  Sewers: 'sewers',
  Factory: 'computer',
  Pyramid: 'pyramid',
  Worm: 'ant_hell',
  'dps test': 'garden',
  Swarm: 'garden',
  Super: 'garden',
};

const collideLayers = new Set([
  'dirt', 'castle', 'walls', 'grate', 'hut', 'bush', 'water',
]);

function normMob(s) {
  let id = String(s).toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, '');
  // florrclone client keys → cpp mobs.json ids
  const alias = {
    ant_worker: 'worker_ant', ant_baby: 'baby_ant', ant_soldier: 'soldier_ant', ant_queen: 'queen_ant',
    termite_baby: 'baby_termite', termite_worker: 'worker_termite', termite_soldier: 'soldier_termite',
    fire_ant_baby: 'baby_fire_ant', fire_ant_worker: 'worker_fire_ant', fire_ant_soldier: 'soldier_fire_ant', fire_ant_queen: 'queen_fire_ant',
    beetle_hel: 'hel_beetle', beetle_mummy: 'mummy_beetle',
    wasp_mecha: 'mecha_wasp',
    spider_mecha: 'mecha_spider',
    crab_mecha: 'mecha_crab',
  };
  return alias[id] || id;
}

function loadSpawnZones() {
  const p = path.join(root, 'docs/florrclone-spawn-zones.json');
  if (!fs.existsSync(p)) require('./extract-florrclone-spawn-zones.js');
  return JSON.parse(fs.readFileSync(p, 'utf8'));
}

/** Convert florrclone `a:1;b:2` mobs string into cpp-friendly weights. */
function normalizeMobsString(raw) {
  return String(raw || '')
    .replace(/\\n/g, ' ')
    .split(/[;\n]+/)
    .map((part) => part.trim())
    .filter(Boolean)
    .map((part) => {
      const m = part.match(/^([^:=\s]+)\s*[:=]?\s*([\d.]+)?/);
      if (!m) return null;
      const name = normMob(m[1]);
      const w = m[2] != null && m[2] !== '' ? Number(m[2]) : 1;
      if (!name || !(w > 0)) return null;
      return `${name}:${w}`;
    })
    .filter(Boolean)
    .join(' ');
}

function zonesForBiome(spawnZones, biomeName) {
  const by = (spawnZones && spawnZones.byBiome) || {};
  if (biomeName === 'Super') {
    // Super has no authored polygons; reuse Garden outlines at 3× density.
    return (by.Garden || []).map((z) => ({
      ...z,
      density: (z.density > 0 ? z.density : 1.4) * 3,
      mobs: mobDistribution('Super'),
    }));
  }
  if (biomeName === 'Swarm') {
    return (by.Garden || []).map((z) => ({
      ...z,
      density: (z.density > 0 ? z.density : 1.4) * 3,
      mobs: mobDistribution('Swarm'),
    }));
  }
  if (biomeName === 'dps test') {
    return [{
      points: [[0, 0], [1, 0], [1, 1], [0, 1]],
      difficulty: 0,
      density: 1,
      mobs: 'test_dummy:1',
    }];
  }
  return by[biomeName] || [];
}

function loadGeometry() {
  const p = path.join(root, 'docs/florrclone-maps-geometry.json');
  if (!fs.existsSync(p)) {
    require('./extract-florrclone-maps.js');
  }
  return JSON.parse(fs.readFileSync(path.join(root, 'docs/florrclone-maps-geometry.json'), 'utf8'));
}

function loadTileDefs() {
  const p = path.join(root, 'docs/florr-tile-id-map.json');
  if (!fs.existsSync(p)) require('./inspect-florr-map-tiles.js');
  return JSON.parse(fs.readFileSync(p, 'utf8')).tileDefs;
}

function fullTileCollision() {
  return {
    draworder: 'index',
    name: '',
    objects: [{
      height: 256, width: 256, id: 1, name: '', rotation: 0, type: '', visible: true, x: 0, y: 0,
    }],
    opacity: 1,
    type: 'objectgroup',
    visible: true,
    x: 0,
    y: 0,
  };
}

function ensureTileset(tileDefs) {
  fs.mkdirSync(tilesDir, { recursive: true });
  const tsjPath = path.join(mapsDir, 'tileset.tsj');
  const tsj = JSON.parse(fs.readFileSync(tsjPath, 'utf8'));
  const byFile = new Map();
  const byId = new Map();
  for (const t of tsj.tiles) {
    const file = String(t.image).replace(/^tiles[\\/]/, '').replace(/\\/g, '/');
    byFile.set(file, t);
    byId.set(t.id, t);
  }

  let added = 0;
  let copied = 0;
  for (const [gidStr, file] of Object.entries(tileDefs)) {
    const localId = Number(gidStr) - 1;
    const src = path.join(florTilesDir, file);
    const dst = path.join(tilesDir, file);
    if (fs.existsSync(src)) {
      fs.copyFileSync(src, dst);
      copied++;
    }
    if (byId.has(localId)) {
      // keep collision from existing entry; ensure image path
      const existing = byId.get(localId);
      existing.image = `tiles/${file}`;
      // Floors must never inherit wall-solid props (factory_floor_c_0 used to
      // match the `_c_0` wall rule and seal the whole Factory).
      if (/floor/i.test(file)) {
        delete existing.objectgroup;
        if (Array.isArray(existing.properties)) {
          existing.properties = existing.properties.filter((p) => p.name !== 'covers_everything');
          if (!existing.properties.length) delete existing.properties;
        }
      } else if (/wall_c_0\.svg$/i.test(file)) {
        // Centre wall tiles are full solids (dirt_c / castle_c / organic_wall_c).
        // An old factory_wall_c_0 entry only blocked a 130px strip, so Factory
        // rooms read as half-open corridors.
        existing.objectgroup = fullTileCollision();
        existing.properties = [{ name: 'covers_everything', type: 'bool', value: true }];
      }
      continue;
    }
    // Prefer reusing collision shapes from same filename if present under another id
    const twin = byFile.get(file);
    const tile = {
      id: localId,
      image: `tiles/${file}`,
      imageheight: 256,
      imagewidth: 256,
      type: 'tile',
    };
    if (twin) {
      if (twin.properties) tile.properties = JSON.parse(JSON.stringify(twin.properties));
      if (twin.objectgroup) tile.objectgroup = JSON.parse(JSON.stringify(twin.objectgroup));
    } else if (!/floor|entrance|exit/i.test(file) &&
               /wall|dirt_l|dirt_tl|dirt_tri|castle|grate_l|grate_t|organic|cliff|hut_[^c]|foliage|scliff|bush_[^c]|coral_[^c]/i.test(file)) {
      tile.objectgroup = fullTileCollision();
      if (/wall_c|_c_0\.svg$/i.test(file)) {
        tile.properties = [{ name: 'covers_everything', type: 'bool', value: true }];
      }
    } else if (/water_/i.test(file)) {
      tile.properties = [{ name: 'water', type: 'bool', value: true }];
    }
    tsj.tiles.push(tile);
    byId.set(localId, tile);
    byFile.set(file, tile);
    added++;
  }
  // Factory wall silhouettes come from florrclone world tiles. Do not reuse
  // the reference project's same-named tile collision geometry.
  const factoryShapes = {
    'factory_wall_l_0.svg': [[0,0],[128,0],[128,256],[0,256]],
    'factory_wall_tl_0.svg': [[0,0],[128,0],[128,96],[96,128],[0,128]],
    'factory_wall_tli.svg': [[0,0],[256,0],[256,128],[160,128],[128,160],[128,256],[0,256]],
  };
  for (const tile of tsj.tiles) {
    const points = factoryShapes[path.basename(tile.image)];
    if (!points) continue;
    tile.objectgroup = fullTileCollision();
    tile.objectgroup.objects[0] = {id:1, x:0, y:0, width:0, height:0,
      rotation:0, visible:true, polygon:points.map(([x,y]) => ({x,y}))};
    tile.properties = (tile.properties || []).filter(p => p.name !== 'covers_everything');
  }
  tsj.tiles.sort((a, b) => a.id - b.id);
  tsj.tilecount = tsj.tiles.length;
  fs.writeFileSync(tsjPath, JSON.stringify(tsj, null, 1) + '\n');
  return { added, copied, tilecount: tsj.tilecount };
}

function decodeLayer(layer) {
  const bytes = Buffer.from(layer.cells, 'base64');
  const w = layer.w;
  const h = layer.h;
  const data = new Array(w * h).fill(0);
  const palette = layer.palette || [];
  for (let i = 0; i < bytes.length && i < data.length; i++) {
    const idx = bytes[i];
    const gid = palette[idx] || 0;
    data[i] = gid >>> 0; // keep flip flags
  }
  return data;
}

function mobDistribution(biomeName) {
  const group = groupOf[biomeName] || 'garden';
  const types = (api.maps[biomeName] && api.maps[biomeName].types) || [];
  if (!types.length) return group;
  const counts = new Map();
  for (const type of types) counts.set(normMob(type), (counts.get(normMob(type)) || 0) + 1);
  return [...counts].map(([id, count]) => `${id}:${count}`).join(' ');
}

function biomeDifficulty(biomeName) {
  const m = api.maps[biomeName] || {};
  if (typeof m.difficulty === 'number') return m.difficulty;
  if (biomeName === 'Hel') return 21;
  if (biomeName === 'Swarm') return 30;
  if (biomeName === 'Super') return 60;
  if (biomeName === 'Pyramid') return 25;
  if (biomeName === 'dps test') return 0;
  return 10;
}

function defaultFloorGid(biomeName) {
  const floors = {
    Garden: 6,
    Desert: 1,
    Ocean: 10,
    Jungle: 66,
    Hel: 61,
    'Ant Hell': 28,
    Sewers: 54,
    Factory: 95,
    Pyramid: 1,
    Worm: 88,
    'dps test': 6,
    Swarm: 6,
    Super: 6,
  };
  return floors[biomeName] || 6;
}

function makeFlatGeometry(biomeName, cols = 60, rows = 60) {
  const gid = defaultFloorGid(biomeName);
  const cells = Buffer.alloc(cols * rows, 1); // palette index 1 -> gid
  // use index 0 for gid directly via palette[0]=gid and all zeros
  cells.fill(0);
  return {
    slug: normMob(biomeName),
    columns: cols,
    rows: rows,
    layers: [{
      order: 0,
      name: 'bg',
      w: cols,
      h: rows,
      x: 0,
      y: 0,
      palette: [gid],
      cells: cells.toString('base64'),
    }],
    objects: [],
  };
}

function portalTargetMap(toBiome) {
  const file = nameToFile[toBiome];
  if (!file) return normMob(toBiome);
  return file.replace(/\.tmj$/, '');
}

/// florrclone `Nx` (preferred) then `nS` fallback — fractions of map width/height.
const spawnFracOf = {
  Garden: [0.3347107438016529, 0.3925619834710744],
  'Ant Hell': [0.40645160280175757, 0.34975992183836263],
  Desert: [0.5247933884297521, 0.7727272727272727],
  Jungle: [0.32257231404958675, 0.2233987603305785],
  Ocean: [0.5, 0.47520661157024796],
  Sewers: [0.2730034722222222, 0.9352957518567455],
  Factory: [0.17942257967856606, 0.9679261564848013],
  Hel: [0.5048611111111111, 0.42916666666666664],
  Worm: [0.41693115234375, 0.4970703125],
  Pyramid: [0.499, 0.905],
  // overlays / aliases that share a parent biome's ground
  Swarm: [0.3347107438016529, 0.3925619834710744],
  Super: [0.3347107438016529, 0.3925619834710744],
  'dps test': [0.3347107438016529, 0.3925619834710744],
};

function spawnFraction(biomeName) {
  return spawnFracOf[biomeName] || spawnFracOf.Garden;
}

function buildTmj(biomeName, geom, portals, spawnZones) {
  const cols = geom.columns;
  const rows = geom.rows;
  const layers = [];
  let nextId = 1;

  const sorted = [...(geom.layers || [])].sort((a, b) => (a.order || 0) - (b.order || 0));
  for (const layer of sorted) {
    const collide = collideLayers.has(layer.name) || /wall|dirt|castle|grate|hut|cliff/i.test(layer.name);
    // Vents / foliage shortcuts punch holes through walls below (same role as
    // a bridge over water). Without negation, Factory vents sit on solid
    // wall_c cells and the rooms stay sealed.
    const negate = /shortcut/i.test(layer.name);
    const properties = [{ name: 'has_collision', type: 'bool', value: collide && !negate }];
    if (negate) properties.push({ name: 'negate_collision', type: 'bool', value: true });
    layers.push({
      data: decodeLayer(layer),
      height: layer.h,
      id: nextId++,
      name: layer.name,
      opacity: 1,
      properties,
      type: 'tilelayer',
      visible: true,
      width: layer.w,
      x: 0,
      y: 0,
    });
  }

  const pxW = cols * TILE;
  const pxH = rows * TILE;
  const objects = [];

  // florrclone engine `nS` / `Nx`: fractional spawn of the map (not centre).
  // Place a tight rect centred on that point so defaultSpawn lands there.
  // Dungeon / test maps hang off in-world pads — not the title-screen row —
  // so their doors stay unpickable and the lobby stays a single biome row.
  const lobbyPickable = !['Worm', 'Swarm', 'Super', 'dps test'].includes(biomeName);
  const [fx, fy] = spawnFraction(biomeName);
  const spawnW = Math.min(pxW * 0.08, 800);
  const spawnH = Math.min(pxH * 0.08, 800);
  const spawnX = Math.max(0, Math.min(pxW - spawnW, pxW * fx - spawnW * 0.5));
  const spawnY = Math.max(0, Math.min(pxH - spawnH, pxH * fy - spawnH * 0.5));
  objects.push({
    height: spawnH,
    id: nextId++,
    name: 'spawn',
    properties: [
      { name: 'biome', type: 'string', value: groupOf[biomeName] || geom.slug },
      { name: 'label', type: 'string', value: biomeName },
      { name: 'pickable', type: 'bool', value: lobbyPickable },
      { name: 'order', type: 'float', value: 0 },
    ],
    rotation: 0,
    type: '',
    visible: true,
    width: spawnW,
    x: spawnX,
    y: spawnY,
  });

  // florrclone polygon bands (difficulty / density / mobs). Fallback: one
  // full-map band so empty overlays still grow something.
  const zoneList = zonesForBiome(spawnZones, biomeName);
  let bandCount = 0;
  for (const z of zoneList) {
    const hasDifficulty = z.difficulty != null && Number.isFinite(Number(z.difficulty));
    const pts = Array.isArray(z.points) ? z.points : [];
    if (pts.length < 3) continue;
    const abs = pts.map(([fx, fy]) => [fx * pxW, fy * pxH]);
    const ox = abs[0][0];
    const oy = abs[0][1];
    const polygon = abs.map(([x, y]) => ({ x: x - ox, y: y - oy }));
    // florrclone authoring uses `a:1;b:2;`. cpp mobDistribution already emits
    // `group 50% id n%` — only the semicolon form needs rewriting.
    const rawMobs = z.mobs == null ? '' : String(z.mobs);
    let mobs = /;/.test(rawMobs)
      ? (normalizeMobsString(rawMobs) || mobDistribution(biomeName))
      : (rawMobs.trim() || mobDistribution(biomeName));
    if (mobs === (groupOf[biomeName] || 'garden')) mobs = mobDistribution(biomeName);
    if (biomeName === 'Factory') mobs = mobs.replace(/\bgambler\b/g, 'mecha_flower');
    const props = [{ name: 'mobs', type: 'string', value: mobs }];
    if (hasDifficulty) props.push({ name: 'difficulty', type: 'float', value: Number(z.difficulty) });
    if (z.forceRarity != null) props.push({ name: 'forceRarity', type: 'int', value: Number(z.forceRarity) });
    if (z.density != null && Number.isFinite(Number(z.density))) {
      props.push({ name: 'density', type: 'float', value: Number(z.density) });
    }
    objects.push({
      height: 0,
      id: nextId++,
      name: '',
      polygon,
      properties: props,
      rotation: 0,
      type: '',
      visible: true,
      width: 0,
      x: ox,
      y: oy,
    });
    if (hasDifficulty) bandCount++;
  }
  if (bandCount === 0) {
    const dens = biomeName === 'Super' || biomeName === 'Swarm' ? 3 : 1;
    const props = [
      { name: 'difficulty', type: 'float', value: biomeDifficulty(biomeName) },
      { name: 'mobs', type: 'string', value: mobDistribution(biomeName) },
      { name: 'density', type: 'float', value: dens },
    ];
    objects.push({
      height: pxH,
      id: nextId++,
      name: '',
      properties: props,
      rotation: 0,
      type: '',
      visible: true,
      width: pxW,
      x: 0,
      y: 0,
    });
  }

  // teleporters from engine portal list (preferred) + decorative objects
  const seen = new Set();
  for (const p of portals || []) {
    const key = `${p.to}:${Math.round(p.x)}:${Math.round(p.y)}`;
    if (seen.has(key)) continue;
    seen.add(key);
    objects.push({
      height: 0,
      id: nextId++,
      name: '',
      point: true,
      properties: [
        { name: 'targetMap', type: 'string', value: portalTargetMap(p.to) },
        { name: 'radius', type: 'float', value: 120 },
      ],
      rotation: 0,
      type: '',
      visible: true,
      width: 0,
      x: p.x,
      y: p.y,
    });
  }

  // decorative object gids (entrances) as tile objects if present
  for (const o of geom.objects || []) {
    objects.push({
      gid: o.gid >>> 0,
      height: (o.h || 0) * pxH,
      id: nextId++,
      name: '',
      rotation: o.rotation || 0,
      type: '',
      visible: true,
      width: (o.w || 0) * pxW,
      x: (o.x || 0) * pxW,
      y: (o.y || 0) * pxH,
    });
  }

  layers.push({
    draworder: 'topdown',
    id: nextId++,
    name: 'player_spawns',
    objects: objects.filter((o) => o.name === 'spawn'),
    opacity: 1,
    type: 'objectgroup',
    visible: true,
    x: 0,
    y: 0,
  });
  // Layer MUST be named `spawns` (plural): tiled_map.cpp only reads that name.
  layers.push({
    draworder: 'topdown',
    id: nextId++,
    name: 'spawns',
    objects: objects.filter((o) => (o.properties || []).some((p) => p.name === 'difficulty')),
    opacity: 1,
    type: 'objectgroup',
    visible: true,
    x: 0,
    y: 0,
  });
  layers.push({
    draworder: 'topdown',
    id: nextId++,
    name: 'teleporters',
    objects: objects.filter((o) => (o.properties || []).some((p) => p.name === 'targetMap')),
    opacity: 1,
    type: 'objectgroup',
    visible: true,
    x: 0,
    y: 0,
  });
  layers.push({
    draworder: 'topdown',
    id: nextId++,
    name: 'decor',
    objects: objects.filter((o) => o.gid != null),
    opacity: 1,
    type: 'objectgroup',
    visible: true,
    x: 0,
    y: 0,
  });

  const bg = (api.maps[biomeName] && api.maps[biomeName].bg) || '#1ea660';
  return {
    compressionlevel: -1,
    height: rows,
    infinite: false,
    layers,
    nextlayerid: nextId + 1,
    nextobjectid: nextId + 50,
    orientation: 'orthogonal',
    properties: [
      { name: 'biome', type: 'string', value: groupOf[biomeName] || geom.slug },
      { name: 'displayName', type: 'string', value: biomeName },
      { name: 'backgroundColor', type: 'color', value: bg },
      { name: 'defaultMobGroup', type: 'string', value: groupOf[biomeName] || 'garden' },
    ],
    renderorder: 'right-down',
    tiledversion: '1.10.1',
    tileheight: TILE,
    tilesets: [{ firstgid: 1, source: 'tileset.tsj' }],
    tilewidth: TILE,
    type: 'map',
    width: cols,
    florrcloneSource: true,
  };
}

function scalePortals(portals, geom) {
  // Engine portals assume 28800 for Bx biomes; geometry may differ.
  const eng = 28800;
  const pxW = geom.columns * TILE;
  const pxH = geom.rows * TILE;
  return (portals || []).map((p) => ({
    to: p.to,
    x: (p.x / eng) * pxW,
    y: (p.y / eng) * pxH,
  }));
}

// --- main ---
const geometry = loadGeometry();
const tileDefs = loadTileDefs();

// backup reference maps once
if (!fs.existsSync(backupDir)) {
  fs.mkdirSync(backupDir, { recursive: true });
  for (const f of fs.readdirSync(mapsDir)) {
    if (f.endsWith('.tmj') || f === 'maps.json') {
      fs.copyFileSync(path.join(mapsDir, f), path.join(backupDir, f));
    }
  }
}

const tilesetInfo = ensureTileset(tileDefs);
const spawnZones = loadSpawnZones();

const portalDoc = JSON.parse(fs.readFileSync(path.join(root, 'docs/florrclone-portals.json'), 'utf8'));
const written = [];

// Geometry maps from florrclone client
for (const [biomeName, geom] of Object.entries(geometry)) {
  const file = nameToFile[biomeName];
  if (!file) continue;
  const portals = scalePortals(portalDoc[biomeName]?.portals, geom);
  const tmj = buildTmj(biomeName, geom, portals, spawnZones);
  fs.writeFileSync(path.join(mapsDir, file), JSON.stringify(tmj));
  written.push({ biome: biomeName, file, cols: geom.columns, rows: geom.rows, layers: geom.layers.length });
}

// Biome-only maps (no client geometry): flat floors
for (const biomeName of ['Pyramid', 'dps test', 'Swarm', 'Super']) {
  const file = nameToFile[biomeName];
  const size = biomeName === 'Swarm' || biomeName === 'Super' ? 80 : 50;
  const geom = makeFlatGeometry(biomeName, size, size);
  const portals = scalePortals(portalDoc[biomeName]?.portals, geom);
  const tmj = buildTmj(biomeName, geom, portals, spawnZones);
  fs.writeFileSync(path.join(mapsDir, file), JSON.stringify(tmj));
  written.push({ biome: biomeName, file, cols: size, rows: size, layers: 1, flat: true });
}

// Drop reference-only maps we no longer want as active content
for (const stale of []) {
  const p = path.join(mapsDir, stale);
  if (fs.existsSync(p)) {
    fs.renameSync(p, path.join(backupDir, stale));
  }
}

const mapsJson = {
  _comment: [
    'Maps converted from florrclone client geometry + engine biome/portal tables.',
    'Reference florr_clone-web maps were moved to maps_ref_backup/.',
  ],
  maps: [
    { file: 'garden.tmj', prestige: true },
    { file: 'desert.tmj', prestige: true },
    { file: 'computer.tmj', prestige: true },
    { file: 'ocean.tmj', prestige: true },
    { file: 'jungle.tmj', prestige: true },
    { file: 'hel.tmj', prestige: true },
    { file: 'ant_hell.tmj', prestige: true },
    { file: 'sewers.tmj', prestige: true },
    { file: 'pyramid.tmj', prestige: true },
    { file: 'worm.tmj', copies: 4 },
    { file: 'swarm.tmj' },
    { file: 'super.tmj' },
    { file: 'dps_test.tmj' },
    { file: 'termite_mound.tmj', copies: 8 },
  ],
};
fs.writeFileSync(path.join(mapsDir, 'maps.json'), JSON.stringify(mapsJson, null, 2) + '\n');

const report = { tilesetInfo, written, mapsJson: mapsJson.maps };
fs.writeFileSync(path.join(root, 'docs/port-florrclone-maps-report.json'), JSON.stringify(report, null, 2));
console.log(JSON.stringify(report, null, 2));

// Rebuild authoritative collision after map art and annotations are imported.
require("node:child_process").execFileSync(process.env.PYTHON || "python",
  [path.join(__dirname, "sync-map-collision.py")], {cwd: root, stdio: "inherit"});

// Fixed practice targets are NPCs, never part of ambient mob distribution.
require('./restore-jungle-dps');
