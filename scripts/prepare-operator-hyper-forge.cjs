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
const fragment=s=>s?.petalType==='fragment'&&s.rarity==='unique';
const worn=(p.loadout||[]).filter(fragment).length;
const count=(p?.inventory?.unique?.petal_fragment||0)-(p.loadoutInInventory?worn:0);
assert(count>=5,'Account needs five unequipped Eternal fragments; timed hammers cannot be converted');
assert(!process.env.FLORR_OPERATOR_HAMMER_EVIDENCE,'Expired timed hammers cannot be restored as Hyper forge materials');
assert(db.operatorHyperForgeRequest?.status!=='pending'||db.operatorHyperForgeRequest.userId===user.id,'Another account operator request is pending');
db.operatorHyperForgeRequest={id,userId:user.id,input:'fragment',status:'pending',announced:false,maintenanceGate:gate,readyFile:ready};
const compare=structuredClone(db);if(Object.hasOwn(original,'operatorHyperForgeRequest'))compare.operatorHyperForgeRequest=original.operatorHyperForgeRequest;else delete compare.operatorHyperForgeRequest;
assert.deepEqual(compare,original);fs.writeFileSync(output,JSON.stringify(db));
console.log(JSON.stringify({id,userId:user.id,username,input:'fragment',rarity:'unique',quantity:5,readyFile:ready}));
