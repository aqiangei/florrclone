'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const {bundle}=require('./online-snapshot.cjs');
test('one checkpoint preserves progress, ownership, pending transfers and all global data',()=>{
  const state={schema:1,sequence:73,globalRevision:9,
    accounts:{alice:{record:{xp:300,inventory:{hyper:{egg:1}}},epoch:7,revision:12,phase:'prepared',shard:'one',boot:'old-process',pending:{target:'two',id:'move123'}}},
    globals:{users:{Alice:{id:'alice'}},sessions:{sessionHash:{userId:'alice'}},hyperForges:{egg:{owner:'alice'}},councilPolls:{one:{votes:{alice:'yes'},weights:{alice:1000000}}},shardWorlds:{one:{bossClocks:{h:25}},two:{bossClocks:{h:50}}}},
    requests:{dedup:{digest:'digest',result:{ok:true}}},acknowledged:{'game/one/old-process':42}};
  const before=JSON.stringify(state),files=bundle(state);
  assert.deepEqual(JSON.parse(files['authority.snapshot']),state);
  const bootstrap=JSON.parse(files['bootstrap.json']);
  assert.deepEqual(bootstrap.players.alice,state.accounts.alice.record);
  for(const key of Object.keys(state.globals))assert.deepEqual(bootstrap[key],state.globals[key]);
  assert.equal(files['authority.journal'],'');assert.equal(files['authority.snapshot.lock'],'');
  assert.equal(JSON.stringify(state),before);
});
test('partial authority responses cannot become valid backup bundles',()=>{
  assert.throws(()=>bundle({schema:1,sequence:2,accounts:{},globals:{}}),/Incomplete/);
  assert.throws(()=>bundle({schema:1,sequence:NaN}),/sequence/);
});
