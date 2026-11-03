'use strict';
// Read the reference engine's real loot tables without changing its files.
// Re-run after updating the reference; --check verifies committed data only.
const fs=require('fs'),path=require('path'),Module=require('module'),assert=require('assert/strict');
const root=path.resolve(__dirname,'..'),filename=path.resolve(root,'../florrclone/server/engine.js');
let source=fs.readFileSync(filename,'utf8');
assert.equal(source.split('slots:Ct,').length,2,'Reference export boundary changed');
source=source.replace('slots:Ct,','lootAudit:{tables:W,types:ir,fallback:Fi,floor:ya,magicForms:Ue},slots:Ct,');
const mod=new Module(filename);mod.filename=filename;mod.paths=Module._nodeModulePaths(path.dirname(filename));
mod._compile(source,filename);
const api=mod.exports.loadEngine(path.resolve(root,'../florrclone/florr-local-site'));
const mobs=JSON.parse(fs.readFileSync(path.join(root,'data/mobs.json')));
const petals=JSON.parse(fs.readFileSync(path.join(root,'data/petals.json')));
const file=path.join(root,'data/mob_drops.json'),before=JSON.parse(fs.readFileSync(file));
const after=structuredClone(before),aliases=require('./content-aliases').petalFlorrToCpp;
let baseline=before;
try{baseline=JSON.parse(require('child_process').execFileSync('git',['show','HEAD:data/mob_drops.json'],{cwd:root,encoding:'utf8',maxBuffer:8*1024*1024}));}catch{}
const norm=s=>String(s).toLowerCase().replace(/[^a-z0-9]+/g,'_').replace(/^_|_$/g,'');
const resolve=(name,items,overrides={})=>{
  const key=overrides[name]&&items[overrides[name]]?overrides[name]:
    Object.keys(items).find(id=>id===norm(name))||Object.keys(items).find(id=>norm(items[id].name)===norm(name));
  assert(key,'Unresolved reference name: '+name);return key;
};
const report={referenceMobs:[],changes:[],preservedCustomMobs:[],sources:[
  'D:/ashflor/florrclone/server/engine.js (grantLootRoll and W)',
  'https://florrio.wiki.gg/wiki/Desert','https://florrio.wiki.gg/wiki/Queen_Ant',
  'https://florrio.wiki.gg/wiki/Ant_Egg_%28Mob%29','https://florrio.wiki.gg/wiki/Garden'],
  notes:['Exact reference runtime tables take precedence over conflicting historical wiki percentages.',
    'Each row is an independent trial except rollGroup=1, the one-choice fallback.',
    'Percentages are per single trial; global 30 rolls and Super multiplier are unchanged.']};
const mapped=new Set();
for(const [name,mob]of Object.entries(api.mobs)){
  const id=resolve(name,mobs);mapped.add(id);
  const raw=api.lootAudit.tables[name]||{};
  const rows=new Map();
  const ensure=(petal,group=0)=>{
    const key=resolve(petal,petals,aliases),tag=key+':'+group;
    assert(!petals[key].isAdminPetal,'Reference drop resolved to admin petal: '+petal+' -> '+key);
    if(!rows.has(tag))rows.set(tag,{type:'petal',itemType:key,rarity:'common',probability:1,
      minQuantity:1,maxQuantity:1,rollGroup:group,rarityChances:Array.from({length:10},()=>[])});
    if(api.lootAudit.magicForms[petal])rows.get(tag).magicItemType=resolve(api.lootAudit.magicForms[petal],petals,aliases);
    return rows.get(tag);
  };
  for(let tier=0;tier<10;tier++){
    const types=api.lootAudit.types(name,tier)??api.lootAudit.fallback(name)??mob.drops;
    // The W table is authoritative for normal mobs. Conditional overrides
    // (queen's Basil, shell's higher-tier items) are already empty below the gate.
    for(const [petal,table]of Object.entries(raw)){
      if(petal==='Magic Leaf'&&name==='Golden Leafbug')continue;
      const row=ensure(petal),rates=table[Math.min(tier,table.length-1)]||[];
      row.rarityChances[tier]=rates.map(pair=>Array.from(pair));
      if(name==='Golden Leafbug'&&petal==='Golden Leaf'){
        row.magicItemType=resolve('Magic Leaf',petals,aliases);
        row.magicRarityChances??=Array.from({length:10},()=>[]);
        const magic=raw['Magic Leaf'];
        row.magicRarityChances[tier]=(magic[Math.min(tier,magic.length-1)]||[]).map(pair=>Array.from(pair));
      }
    }
    if(name==='Square'||name==='Test Dummy'){
      ensure('Square').rarityChances[tier]=[[0,100]];continue;
    }
    for(const petal of types||[]){
      if(Object.hasOwn(raw,petal))continue;
      const min=Math.max(petals[resolve(petal,petals,aliases)].minRarity||0,api.lootAudit.floor(name,petal),+(name==='Ghost'));
      if(tier<min)continue;
      const row=ensure(petal,1),rates=new Map(),chance=80/types.length;
      for(const [r,p]of [[Math.max(min,tier-1),.65],[Math.max(min,tier),.35]])rates.set(r,(rates.get(r)||0)+chance*p);
      row.rarityChances[tier]=[...rates];
    }
  }
  after[id]={referenceMob:name,autoEggDrops:false,drops:[...rows.values()]};
  report.referenceMobs.push({id,name,items:[...new Set([...rows.values()].map(r=>r.itemType))]});
  if(JSON.stringify(after[id])!==JSON.stringify(baseline[id]))report.changes.push({id,
    before:[...new Set((baseline[id]?.drops||[]).map(r=>r.itemType))],after:report.referenceMobs.at(-1).items});
}
// The old Fire Ant Hole name is still a spawnable alias of Fire Ant Burrow.
after.fire_ant_hole=structuredClone(after.fire_ant_burrow);mapped.add('fire_ant_hole');
// Keep explicitly authored server-only sources through a reference refresh.
for(const [id,table]of Object.entries(before))for(const row of table.drops||[])
  if(row.itemType==='splitter'&&after[id]&&!after[id].drops.some(r=>r.itemType==='splitter'))
    after[id].drops.push(structuredClone(row));
for(const [body,head]of [['centipede_body','centipede'],['desert_centipede_body','desert_centipede']]){
  after[body]=structuredClone(after[head]);mapped.add(body);
}
require('./restore-high-tier-drops').restoreHighTierDrops(after);
report.notes.push('Server override: Super restores a 0.5% promotion of its Ultra mass; Eternal uses 1.5%, without removing lower-tier or no-drop probability.');
for(const id of Object.keys(mobs))if(!mapped.has(id))report.preservedCustomMobs.push(id);
// A legacy table with no mob cannot ever pay out; keep it out of the active file.
for(const id of Object.keys(after))if(!mobs[id])delete after[id];
report.totalMobConfigs=Object.keys(mobs).length;
report.totalDropTables=Object.keys(after).length;
report.withoutAuthoredTable=Object.keys(mobs).filter(id=>!after[id]);
report.unresolvedItems=[];
for(const [id,table]of Object.entries(after))for(const row of table.drops||[])
  if(row.itemType!=='random'&&!petals[row.itemType])report.unresolvedItems.push({mob:id,item:row.itemType});
assert.equal(report.unresolvedItems.length,0,'Drop table contains unresolved items');
if(process.argv.includes('--check'))assert(require('util').isDeepStrictEqual(before,JSON.parse(JSON.stringify(after))),'Run node scripts/sync-mob-drops.js');
else{
  fs.writeFileSync(file,JSON.stringify(after,null,2)+'\n');
  fs.writeFileSync(path.join(root,'docs/mob-drop-audit.json'),JSON.stringify(report,null,2)+'\n');
}
console.log(JSON.stringify({referenceMobs:report.referenceMobs.length,changed:report.changes.length,
  preservedCustomMobs:report.preservedCustomMobs.length}));
