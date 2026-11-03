'use strict';
// Only replace spawn annotations. Terrain, art, portals and service NPCs stay
// in their existing layers. The engine owns each polygon's biome membership.
const fs=require('fs'),path=require('path'),assert=require('assert/strict'),Module=require('module');
const root=path.resolve(__dirname,'..');
const filename=path.resolve(root,'../florrclone/server/engine.js');
const mod=new Module(filename);mod.filename=filename;mod.paths=Module._nodeModulePaths(path.dirname(filename));
const source=fs.readFileSync(filename,'utf8');assert.equal(source.split('slots:Ct,').length,2);
mod._compile(source.replace('slots:Ct,','spawnRows:r=>No(r),slots:Ct,'),filename);
const api=mod.exports.loadEngine(path.resolve(root,'../florrclone/florr-local-site'));
const mobs=JSON.parse(fs.readFileSync(path.join(root,'data/mobs.json')));
const files={Garden:'garden',Desert:'desert',Ocean:'ocean',Jungle:'jungle',Hel:'hel',
  'Ant Hell':'ant_hell',Sewers:'sewers',Factory:'computer',Pyramid:'pyramid'};
const norm=s=>String(s).toLowerCase().replace(/[^a-z0-9]+/g,'_').replace(/^_|_$/g,'');
function roster(raw,biome,sourceTypes){
  const rows=(sourceTypes?sourceTypes.map(type=>({type,weight:1})):api.spawnRows({mobs:raw})).map(row=>{
    let id=Object.keys(mobs).find(id=>id===norm(row.type))||Object.keys(mobs).find(id=>norm(mobs[id].name)===norm(row.type));
    if(biome==='Factory'&&id==='gambler')id='mecha_flower';
    assert(mobs[id],'Unknown spawn type '+row.type+' in '+biome);
    return id+':'+row.weight;
  });
  assert(rows.length,'Empty roster in '+biome);return rows.join(' ');
}
const report=[],plans=[];
for(const [biome,id]of Object.entries(files)){
  const file=path.join(root,'maps',id+'.tmj'),map=JSON.parse(fs.readFileSync(file));
  map.properties=(map.properties||[]).filter(p=>p.name!=='spawnCurve');
  map.properties.push({name:'spawnCurve',type:'string',value:'florrclone'});
  const layer=map.layers.find(l=>l.name==='spawns');assert(layer,biome+' has no spawns layer');
  const previous=layer.objects;
  const regions=api.spawnRegions[biome]||(biome==='Pyramid'?[{
    points:[[0,0],[1,0],[1,1],[0,1]],difficulty:25,density:1,
    sourceTypes:api.maps.Pyramid.types
  }]:null);assert(Array.isArray(regions),biome+' has no reference regions');
  const objects=[];let npcAreas=0;
  for(const z of regions){
    if(!Array.isArray(z.points)||z.points.length<3)continue;
    // Service areas are not combat populations. Preserve the separately
    // authored NPC layer rather than turning a service into a killable mob.
    if(/^\s*trader:1;?\s*$/.test(z.mobs))continue;
    if(/^\s*assembler:1;?\s*$/.test(z.mobs)){++npcAreas;continue;}
    const distribution=roster(z.mobs,biome,z.sourceTypes);
    if(distribution==='assembler:1'){++npcAreas;continue;}
    const points=z.points.map(([x,y])=>[x*map.width*map.tilewidth,y*map.height*map.tileheight]);
    const [x,y]=points[0],properties=[{name:'mobs',type:'string',value:distribution}];
    if(z.difficulty!=null)properties.push({name:'difficulty',type:'float',value:z.difficulty});
    if(z.forceRarity!=null)properties.push({name:'forceRarity',type:'int',value:z.forceRarity});
    if(z.density!=null)properties.push({name:'density',type:'float',value:z.density});
    if(z.difficulty!=null&&z.density===0)properties.push({name:'singular',type:'bool',value:true});
    if(z.extraSpawnDelay!=null)properties.push({name:'sourceExtraSpawnDelay',type:'float',value:z.extraSpawnDelay});
    objects.push({height:0,id:0,name:'',polygon:points.map(([px,py])=>({x:px-x,y:py-y})),properties,
      rotation:0,type:'',visible:true,width:0,x,y});
  }
  if(npcAreas){
    const services=map.layers.filter(l=>l.name==='npcs').flatMap(l=>l.objects||[])
      .filter(o=>(o.properties||[]).some(p=>p.name==='npc'&&p.value==='assembler'));
    assert.equal(services.length,npcAreas,'Preserve exactly the existing factory service areas');
  }
  const comparable=xs=>xs.map(({id,...rest})=>rest);
  if(process.argv.includes('--check'))assert(require('util').isDeepStrictEqual(comparable(previous),JSON.parse(JSON.stringify(comparable(objects)))),biome+' spawn annotations differ from reference');
  else{
    const remaining=map.layers.filter(l=>l!==layer).flatMap(l=>l.objects||[]).map(o=>o.id);
    let next=Math.max(0,...remaining)+1;for(const o of objects)o.id=next++;
    layer.objects=objects;map.nextobjectid=next;
    plans.push({file,map});
  }
  report.push({biome,map:id,before:previous.length,reference:regions.length,spawnAreas:objects.length,npcAreas,
    bands:objects.filter(o=>o.properties.some(p=>p.name==='difficulty')).length});
}
if(!process.argv.includes('--check')){
  for(const {file,map}of plans)fs.writeFileSync(file,JSON.stringify(map));
  fs.writeFileSync(path.join(root,'docs/spawn-biome-repair.json'),JSON.stringify(report,null,2)+'\n');
}
console.log(JSON.stringify(report));
