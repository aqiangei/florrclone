'use strict';
// Offline operator staging after the stopped-save backup has been verified.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const [input,output,username,id,gate,ready]=process.argv.slice(2);
assert(input&&output&&username&&id&&gate&&ready&&path.resolve(input)!==path.resolve(output));
const original=JSON.parse(fs.readFileSync(input)),db=structuredClone(original);
const matches=Object.values(db.users||{}).filter(u=>u.username===username);
assert.equal(matches.length,1,'Exact account not found');const user=matches[0],p=db.players[user.id];
if(process.env.FLORR_OPERATOR_TEMP_HAMMER==='1'){
  assert(!(p.inventory?.apex?.petal_mjolnir>0),'Account already owns a Hyper hammer');
  db.operatorHyperForgeRequest={id,userId:user.id,input:'timed_mjolnir',mode:'temporary15h',status:'pending',announced:false,maintenanceGate:gate,readyFile:ready};
  const compare=structuredClone(db);if(Object.hasOwn(original,'operatorHyperForgeRequest'))compare.operatorHyperForgeRequest=original.operatorHyperForgeRequest;else delete compare.operatorHyperForgeRequest;
  assert.deepEqual(compare,original);fs.writeFileSync(output,JSON.stringify(db));
  console.log(JSON.stringify({id,userId:user.id,username,mode:'temporary15h',quantity:1,consumed:0,readyFile:ready}));process.exit(0);
}
const hammer=s=>s?.petalType==='timed_mjolnir'&&s.rarity==='unique';
let count=p?.inventory?.unique?.petal_timed_mjolnir||0;
const worn=(p.loadout||[]).filter(hammer).length;
if(!p.loadoutInInventory)count+=worn;
const recovered=[];
if(process.env.FLORR_OPERATOR_HAMMER_EVIDENCE){
  const evidence=JSON.parse(fs.readFileSync(process.env.FLORR_OPERATOR_HAMMER_EVIDENCE));
  assert.equal(Object.values(evidence.users||{}).find(u=>u.id===user.id)?.username,username,'Evidence account mismatch');
  const prior=evidence.players[user.id];
  assert.equal(prior.inventory?.unique?.petal_timed_mjolnir,5,'Evidence must prove the original five hammers');
  const now=Date.now();
  for(const lease of prior.titanHammerLeases||[]){
    if(count>=5)break;
    if(lease.expiresAt>now)continue;
    if((p.titanHammerLeases||[]).some(l=>l.expiresAt===lease.expiresAt))continue;
    recovered.push(lease.expiresAt);++count;
    (p.titanHammerLeases??=[]).push({expiresAt:now+3600000,slot:-1});
  }
}
assert(count>=5,'Account needs five Eternal hammers; only proven expired copies may be restored');
p.inventory.unique.petal_timed_mjolnir=count;
p.loadout=(p.loadout||[]).map(s=>hammer(s)?null:s);
for(const lease of p.titanHammerLeases||[])lease.slot=-1;
assert(db.operatorHyperForgeRequest?.status!=='pending'||db.operatorHyperForgeRequest.userId===user.id,'Another account operator request is pending');
db.operatorHyperForgeRequest={id,userId:user.id,input:'timed_mjolnir',status:'pending',announced:false,maintenanceGate:gate,readyFile:ready};
const compare=structuredClone(db);compare.players[user.id]=structuredClone(original.players[user.id]);if(Object.hasOwn(original,'operatorHyperForgeRequest'))compare.operatorHyperForgeRequest=original.operatorHyperForgeRequest;else delete compare.operatorHyperForgeRequest;
assert.deepEqual(compare,original);fs.writeFileSync(output,JSON.stringify(db));
console.log(JSON.stringify({id,userId:user.id,username,quantity:5,readyFile:ready,unequipped:worn,recoveredExpiredLeases:recovered}));
