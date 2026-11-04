const a=require('node:assert/strict'),{apply}=require('./release-selected-hypers.cjs');
const item=p=>({petalType:p,rarity:'apex'});
function fixture(included){return {users:{u:{id:'u',username:'test',password:'untouched'},v:{id:'v',username:'other'}},players:{u:{stars:777,xp:123,loadoutInInventory:included,inventory:{unique:{petal_fragment:20},apex:{petal_dahlia:included?2:1,petal_yggdrasil:1,petal_leaf:1}},loadout:[item('dahlia')],loadoutPresets:{K:[item('dahlia'),item('yggdrasil'),item('leaf')]}},v:{inventory:{apex:{petal_dahlia:8}}}},hyperForges:{dahlia:{owner:'u',claims:3,protectedUntil:99},'garden:dahlia':{owner:'v',claims:2},yggdrasil:{owner:'u',claims:1,protectedUntil:99}}};}
for(const included of [false,true]){
 const db=fixture(included),users=structuredClone(db.users),other=structuredClone(db.players.v),currency=structuredClone(db.players.u.inventory.unique);
 const action={id:'once',username:'test',petals:['dahlia','yggdrasil'],resetClaims:false};
 const r=apply(db,[action]);a.equal(r[0].changed.reduce((s,x)=>s+x.removed,0),2);a.equal(db.players.u.loadout[0].petalType,'dahlia');a.equal(db.players.u.loadoutPresets.K[0].petalType,'dahlia');a.equal(db.players.u.loadoutPresets.K[1],null);a.equal(db.hyperForges.dahlia.owner,'');a.equal(db.hyperForges.dahlia.claims,3);a.equal(db.hyperForges['garden:dahlia'].owner,'v');a.deepEqual(db.users,users);a.deepEqual(db.players.v,other);a.deepEqual(db.players.u.inventory.unique,currency);a.equal(db.players.u.stars,777);a.equal(db.players.u.xp,123);
 const once=JSON.stringify(db);apply(db,[action]);a.equal(JSON.stringify(db),once);
}
for(const included of [false,true]){
 const db=fixture(included);db.players.u.inventory.apex={...(included?{petal_mjolnir:1}:{}),petal_leaf:1};db.players.u.loadout=[item('mjolnir')];db.players.u.loadoutPresets={K:[item('mjolnir'),item('leaf')]};db.players.u.temporaryHyperHammer={active:true,expiresAt:999,request:'old'};db.hyperForges={mjolnir:{owner:'u',claims:1,paid:5},'garden:mjolnir':{owner:'v'}};
 apply(db,[{id:'hammer',username:'test',petals:['mjolnir'],resetClaims:true}]);a.equal(db.players.u.loadout[0],null);a.equal(db.players.u.loadoutPresets.K[0],null);a.equal(db.players.u.inventory.apex.petal_mjolnir,undefined);a.equal(db.players.u.temporaryHyperHammer.active,false);a.equal(db.hyperForges.mjolnir,undefined);a.equal(db.hyperForges['garden:mjolnir'].owner,'v');a.equal(db.players.u.inventory.unique.petal_fragment,20);
}
a.throws(()=>apply(fixture(false),[{id:'bad',username:'absent',petals:['dahlia']}]),/uniquely/);
console.log('PASS: separate/inclusive bag storage, one-copy removal, worn/presets, claims, no refunds, unrelated data, idempotency');

for(const included of [false,true]){
 const db=fixture(included),old=structuredClone(db.players.u.inventory.unique);
 const report=apply(db,[{id:'refund-four',username:'test',expectedUserId:'u',petals:['dahlia','yggdrasil'],resetClaims:false,refund:4}]);
 a.equal(db.players.u.inventory.unique.petal_dahlia,4);a.equal(db.players.u.inventory.unique.petal_yggdrasil,4);
 a.equal(db.players.u.inventory.unique.petal_fragment,old.petal_fragment);a.equal(report[0].changed.reduce((s,x)=>s+x.refund,0),8);
 const once=JSON.stringify(db);apply(db,[{id:'refund-four',username:'test',petals:['dahlia','yggdrasil'],refund:4}]);a.equal(JSON.stringify(db),once);
}
console.log('PASS: four matching Eternal materials per released claim, idempotent receipts');
