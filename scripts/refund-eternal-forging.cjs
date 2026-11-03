'use strict';
// Offline migration: use only the freshly stopped, verified deployment snapshot.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const marker='eternalPriceRefund20261103';
function refund(db,catalog){
  const result=structuredClone(db),credits=[];
  const names=new Map();
  for(const [id,p]of Object.entries(catalog)){
    if(names.has(p.name))throw Error('Ambiguous petal name: '+p.name);
    names.set(p.name,id);
  }
  const owed=new Map(),seen=new Map(),secondSeen=new Set();
  for(const n of db.notifications||[]){
    if(n.type!=='unique_craft')continue;
    const m=/^The Eternal (.+) has been forged by @(.+) \[.*\] \(forged x1, consumed x(25|55)\)$/.exec(n.message||'');
    if(!m)continue;
    const material=names.get(m[1]);
    if(!material)throw Error('Unknown forged material: '+m[1]);
    if(material==='fragment'||material==='timed_mjolnir')continue;
    const user=db.users?.[m[2]],id=user?.id;
    if(!id||!db.players?.[id])throw Error('Cannot resolve forge owner: '+m[2]);
    assert(n.id,'Refund evidence must have a notification ID');
    const key=id+'\0'+material,ids=seen.get(key)||new Set();
    if(ids.has(n.id))continue;
    ids.add(n.id);seen.set(key,ids);
    if(Number(m[3])===25)secondSeen.add(key);
    owed.set(key,(owed.get(key)||0)+Number(m[3])-15);
  }
  // Historical counters were capped at two. They prove a second forge but do
  // not prove an exact total; later forges are recovered from their actual fees.
  for(const [id,p]of Object.entries(db.players||{}))for(const [material,count]of Object.entries(p.titanForgeCounts||{})){
    if(!Number.isInteger(count)||count<2||material==='fragment'||material==='timed_mjolnir')continue;
    assert(catalog[material],'Unknown forge counter material');
    const key=id+'\0'+material;
    const observed=owed.get(key)||0;
    owed.set(key,secondSeen.has(key)?Math.max(10,observed):observed+10);
  }
  for(const [key,total]of owed){
    const [id,material]=key.split('\0'),p=result.players[id];
    const prior=p[marker]?.refundedByMaterial?.[material]||0;
    assert(Number.isSafeInteger(prior)&&prior>=0,'Invalid refund marker');
    const quantity=Math.max(0,total-prior);if(!quantity)continue;
    const inv=p.inventory||(p.inventory={}),tier=inv.super||(inv.super={}),item='petal_'+material;
    const old=tier[item]||0;
    assert(Number.isSafeInteger(old)&&old>=0&&old+quantity<=2147483647,'Refund inventory capacity exceeded');
    tier[item]=old+quantity;
    const state=p[marker]||(p[marker]={version:1,refundedByMaterial:{}});
    assert.equal(state.version,1);
    state.refundedByMaterial[material]=total;
    credits.push({accountId:id,material,quantity,evidenceIds:[...(seen.get(key)||[])]});
  }
  // Independently restrict the migration to precisely the credited inventory
  // cells and per-account receipt. Passwords, talents and all other progress stay.
  const checked=structuredClone(result);
  for(const id of new Set(credits.map(c=>c.accountId))){
    const p=checked.players[id],old=db.players[id];
    for(const {material}of credits.filter(c=>c.accountId===id)){
      const item='petal_'+material;
      if(Object.hasOwn(old.inventory?.super||{},item))p.inventory.super[item]=old.inventory.super[item];
      else delete p.inventory.super[item];
    }
    if(!Object.hasOwn(old.inventory||{},'super'))delete p.inventory.super;
    if(!Object.hasOwn(old,'inventory'))delete p.inventory;
    if(Object.hasOwn(old,marker))p[marker]=structuredClone(old[marker]);else delete p[marker];
  }
  assert.deepEqual(checked,db);
  return {database:result,report:{accountsRefunded:new Set(credits.map(c=>c.accountId)).size,
    superMaterialsRefunded:credits.reduce((a,c)=>a+c.quantity,0),credits}};
}
module.exports={refund};
if(require.main===module){
  const [input,output]=process.argv.slice(2);
  assert(input&&output&&path.resolve(input)!==path.resolve(output),'Usage: refund-eternal-forging.cjs INPUT NEW_OUTPUT');
  const {database,report}=refund(JSON.parse(fs.readFileSync(input)),require('../data/petals.json'));
  fs.writeFileSync(output,JSON.stringify(database));console.log(JSON.stringify(report));
}
