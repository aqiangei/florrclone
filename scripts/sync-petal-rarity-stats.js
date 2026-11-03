'use strict';
// Read the reference engine without changing it. Store typed per-tier numeric
// exceptions in petals.json so the client, server, and tooltip share values.
const fs=require('node:fs'),path=require('node:path'),Module=require('node:module'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),file=path.resolve(root,'../florrclone/server/engine.js');
const mod=new Module(file);mod.filename=file;mod.paths=Module._nodeModulePaths(path.dirname(file));
const source=fs.readFileSync(file,'utf8');assert.equal(source.split('slots:Ct,').length,2);
mod._compile(source.replace('slots:Ct,','petalStats:(type,rarity)=>R({type,rarity}),slots:Ct,'),file);
const api=mod.exports.loadEngine(path.resolve(root,'../florrclone/florr-local-site'));
const {petalFlorrToCpp}=require('./content-aliases');
const output=path.join(root,'data/petals.json'),data=JSON.parse(fs.readFileSync(output));
const norm=s=>s.toLowerCase().replace(/[^a-z0-9]+/g,'_').replace(/^_|_$/g,'');
const mapping={damage:'damage',hp:'health',shield:'shield',poison:'poisonPerSecond',bodyDamage:'bodyDamage',armorDebuff:'armorReduction',armorStack:'armorPerStack',armor:'petalArmor',healOnHit:'lifesteal',overhealShield:'overhealShield',healBonus:'healBonus',bodyResist:'collisionResist',undeadResist:'undeadBodyDamageCut',flowerHealth:'flowerHealthBonus',healOnKill:'totemHealPerSecond'};
const rows=[];let combinations=0;
for(const [name,base] of Object.entries(api.petals)){
 const id=petalFlorrToCpp[name]||Object.keys(data).find(k=>norm(k)===norm(name)||norm(data[k].name||'')===norm(name));
 assert(id&&data[id],name+' missing from current content');const tiers={};
 for(let t=0;t<10;t++){
  const ref=api.petalStats(name,t),row={};
  for(const [from,to]of Object.entries(mapping))if(base[from]!==undefined||['damage','hp'].includes(from))row[to]=ref[from]||0;
  row.reloadMillis=ref.reload*1000;
  if(Number.isInteger(ref.count))row.count=ref.count;
  if(id!=='leaf'&&(base.heal||base.healPerSecond)){
   const target=id==='starfish'?'conditionalHealPerSecond':base.burstHeal?'heal':'passiveHealPerSecond';
   row[target]=ref.healPerSecond||ref.heal||0;
  }
  if(base.manaBase||base.manaCapacity)row.maxMana=(ref.manaBase||0)+(ref.manaCapacity||0);
  if(base.manaBurst)row.mana=ref.manaBurst;
  if(base.manaRegen)row.passiveManaPerSecond=ref.manaRegen;
  if(base.undeadTime)row.undeadMillis=ref.undeadTime*1000;
  if(base.luck)row.luck=ref.luck;
  if(id==='magnet')row.magnetism=ref.pickup;
  if(id==='battery')row.lightningDamagePerHit=ref.onHurtLightning;
  if(id==='lightning')row.lightningDamagePerHit=ref.lightningDamage;
  // Knockback is a physics-engine decision. Keep the chosen movement engine
  // defaults; only explicit combat knockback petals use reference values.
  if(['jelly','corn','heavy'].includes(id))row.knockback=ref.knockback;
  for(const [key,value]of Object.entries(row))assert(Number.isFinite(value)&&value>=0,id+' '+t+' '+key);
  tiers[t]=row;++combinations;
 }
 if(process.argv.includes('--check'))assert.deepEqual(data[id].rarityStats,tiers,id+' tier stats differ');
 else data[id].rarityStats=tiers;
 rows.push({id,reference:name,tiers:10,attributes:[...new Set(Object.values(tiers).flatMap(Object.keys))]});
}
const report={reference:'florrclone engine R(petal)',referencePetals:rows.length,combinations,
 preserved:['Leaf healing and server 3x multiplier','stackable Powder and requested player speed','movement engine knockback defaults','Devine/Admin custom scaling'],petals:rows};
if(!process.argv.includes('--check')){fs.writeFileSync(output,JSON.stringify(data,null,2)+'\n');fs.writeFileSync(path.join(root,'docs/petal-rarity-stat-audit.json'),JSON.stringify(report,null,2)+'\n');}
console.log(JSON.stringify({referencePetals:rows.length,combinations,mode:process.argv.includes('--check')?'verified':'synced'}));
