'use strict';
// The runtime already knows each polygon's biome. Never infer it from mobs:
// fire ants and termites are habitats inside Ant Hell as well as other maps.
const fs=require('fs'),path=require('path');
const root=path.resolve(__dirname,'..');
const api=require('../../florrclone/server/engine').loadEngine(path.resolve(root,'../florrclone/florr-local-site'));
const byBiome=api.spawnRegions,summary={};
for(const [biome,zones]of Object.entries(byBiome))summary[biome]={
  count:zones.length,difficulties:[...new Set(zones.map(z=>z.difficulty))],
  densities:[...new Set(zones.map(z=>z.density))]};
fs.writeFileSync(path.join(root,'docs/florrclone-spawn-zones.json'),
  JSON.stringify({source:'florrclone/server/engine.js spawnRegions',summary,byBiome},null,2)+'\n');
console.log(JSON.stringify({source:'engine spawnRegions',summary}));
