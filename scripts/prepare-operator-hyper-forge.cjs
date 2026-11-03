'use strict';
// Offline operator staging only. No inventory/auth data is changed here.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const [input,output,username,id,gate,ready]=process.argv.slice(2);
assert(input&&output&&username&&id&&gate&&ready&&path.resolve(input)!==path.resolve(output));
const original=JSON.parse(fs.readFileSync(input)),db=structuredClone(original);
const matches=Object.values(db.users||{}).filter(u=>u.username===username);
assert.equal(matches.length,1,'Exact account not found');const user=matches[0],p=db.players[user.id];
const count=p?.inventory?.unique?.petal_timed_mjolnir||0;
const worn=p.loadoutInInventory?(p.loadout||[]).filter(s=>s?.petalType==='timed_mjolnir'&&s.rarity==='unique').length:0;
assert(count-worn>=5,'Account needs five unequipped Eternal hammers');
assert(db.operatorHyperForgeRequest?.status!=='pending','Another operator request is pending');
db.operatorHyperForgeRequest={id,userId:user.id,input:'timed_mjolnir',status:'pending',announced:false,maintenanceGate:gate,readyFile:ready};
const compare=structuredClone(db);if(Object.hasOwn(original,'operatorHyperForgeRequest'))compare.operatorHyperForgeRequest=original.operatorHyperForgeRequest;else delete compare.operatorHyperForgeRequest;
assert.deepEqual(compare,original);fs.writeFileSync(output,JSON.stringify(db));
console.log(JSON.stringify({id,userId:user.id,username,quantity:5,readyFile:ready}));
