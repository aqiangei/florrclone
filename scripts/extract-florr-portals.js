'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { loadEngine } = require('../../florrclone/server/engine');

const site = path.join(__dirname, '../../florrclone/florr-local-site');
const api = loadEngine(site);

const biomes = Object.keys(api.maps);
const results = {};
for (const biome of biomes) {
  try {
    const size = api.mapSize(biome);
    const list = api.portals(biome, size.width, size.height);
    results[biome] = { size, portals: list };
  } catch (e) {
    results[biome] = { error: e.message };
  }
}
console.log(JSON.stringify(results, null, 2));
fs.writeFileSync(path.join(__dirname, '../docs/florrclone-portals.json'), JSON.stringify(results, null, 2));
