'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {apply}=require('./grant-authorized-petals.cjs');
test('authorized grant preserves progress, verifies identity and applies once',()=>{
  const db={users:{u:{id:'u',username:'player'}},players:{u:{xp:123,inventory:{unique:{petal_claw:6}}},other:{xp:456}}};
  const request={id:'one-time-request',accountId:'u',username:'player',petal:'claw',rarity:'unique',count:4};
  assert.throws(()=>apply(db,{...request,accountId:'other'}));
  assert.equal(apply(db,request).after,10);
  const once=structuredClone(db);
  assert.equal(apply(db,request).alreadyApplied,true);
  assert.deepEqual(db,once);
  assert.equal(db.players.u.xp,123);
  assert.deepEqual(db.players.other,{xp:456});
});
