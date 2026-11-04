'use strict';
// Offline only: run against the SHA-verified stopped-save candidate, never live.
const fs=require('node:fs'),assert=require('node:assert/strict'),path=require('node:path');
function apply(db,actions){
  assert(Array.isArray(actions),'Invalid adjustment list');
  const report=[];
  for(const action of actions){
    assert(typeof action.id==='string'&&action.id&&Array.isArray(action.petals)&&action.petals.length,'Invalid adjustment');
    assert(action.refund===undefined||action.refund===0||action.refund===4,'Invalid refund amount');
    let matches=Object.values(db.users||{}).filter(u=>u.username===action.username);
    if(!matches.length)matches=Object.values(db.users||{}).filter(u=>u.username?.toLowerCase()===action.username.toLowerCase());
    assert.equal(matches.length,1,'Account must resolve uniquely: '+action.username);
    const user=matches[0],record=db.players?.[user.id];assert(record,'Missing progress');
    if(action.expectedUserId)assert.equal(user.id,action.expectedUserId,'Account identity changed: '+action.username);
    record.operatorAdjustments??=[];
    assert(Array.isArray(record.operatorAdjustments),'Invalid adjustment receipts');
    if(record.operatorAdjustments.includes(action.id)){report.push({username:user.username,alreadyApplied:true});continue;}
    const changed=[];
    let petals=action.petals;
    if(action.keepPetals){
      assert(Array.isArray(action.keepPetals)&&action.keepPetals.every(x=>typeof x==='string'),'Invalid keep list');
      const types=new Set();
      for(const [key,count] of Object.entries(record.inventory?.apex||{}))if(count>0)types.add(key.replace(/^petal_/,''));
      for(const item of record.loadout||[])if(item?.rarity==='apex')types.add(item.petalType);
      for(const [key,row] of Object.entries(db.hyperForges||{}))if(row.owner===user.id)types.add(key.split(':').at(-1));
      petals=[...types].filter(x=>!action.keepPetals.includes(x));
    }
    for(const petal of petals){
      const key='petal_'+petal,bag=record.inventory?.apex;
      const worn=(record.loadout||[]).filter(s=>s?.petalType===petal&&s.rarity==='apex').length;
      const stored=Math.max(0,bag?.[key]||0),included=!!record.loadoutInInventory;
      const spare=Math.max(0,stored-(included?worn:0));let removed=0;
      if(spare>0){bag[key]=stored-1;removed=1;}
      else {
        const index=(record.loadout||[]).findIndex(s=>s?.petalType===petal&&s.rarity==='apex');
        if(index>=0){record.loadout[index]=null;if(included&&bag)bag[key]=Math.max(0,stored-1);removed=1;}
      }
      if(bag?.[key]===0)delete bag[key];
      let remaining=Math.max(0,bag?.[key]||0);
      if(!included)remaining+=(record.loadout||[]).filter(s=>s?.petalType===petal&&s.rarity==='apex').length;
      if(remaining===0)for(const slots of Object.values(record.loadoutPresets||{}))if(Array.isArray(slots))
        for(let i=0;i<slots.length;++i)if(slots[i]?.petalType===petal&&slots[i].rarity==='apex')slots[i]=null;
      let released=0,refund=0;
      for(const [forgeKey,row]of Object.entries(db.hyperForges||{})){
        if(row.owner!==user.id||forgeKey.split(':').at(-1)!==petal)continue;
        if(action.refund===4){
          assert(petal!=="mjolnir" && (row.input||petal)===petal,"Unexpected refund material");
          record.inventory??={};record.inventory.unique??={};
          const balance=record.inventory.unique[key]??0;
          assert(Number.isSafeInteger(balance)&&balance>=0,"Invalid refund stack");
          record.inventory.unique[key]=balance+4;refund=4;
        }
        if(action.resetClaims)delete db.hyperForges[forgeKey];
        else {row.owner='';row.protectedUntil=0;row.releasedBy=action.id;}
        released++;break; // One copy of each specified petal, not every titan's copy.
      }
      if(petal==='mjolnir'&&record.temporaryHyperHammer){
        record.temporaryHyperHammer.active=false;record.temporaryHyperHammer.expiresAt=0;
      }
      changed.push({petal,removed,releasedClaims:released,refund});
    }
    record.operatorAdjustments.push(action.id);report.push({username:user.username,changed});
  }
  return report;
}
module.exports={apply};
if(require.main===module){
  const [input,output,requests]=process.argv.slice(2);
  assert(input&&output&&requests&&path.resolve(input)!==path.resolve(output));
  const db=JSON.parse(fs.readFileSync(input,'utf8'));
  const report=apply(db,JSON.parse(fs.readFileSync(requests,'utf8')).actions);
  fs.writeFileSync(output,JSON.stringify(db));console.log(JSON.stringify(report));
}
