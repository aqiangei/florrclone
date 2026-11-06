'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {merge}=require('./shard-global-merge.cjs');
test('first simultaneous map writes preserve both independent shard clocks',()=>{
  const a={globals:{shardWorlds:{one:{bossClocks:{h:123}}}}};
  const b={globals:{shardWorlds:{two:{bossClocks:{h:456}}}}};
  const original={globals:{shardWorlds:null}};
  assert.deepEqual(merge(original,a,b),{globals:{shardWorlds:{two:{bossClocks:{h:456}},one:{bossClocks:{h:123}}}}});
  assert.equal(original.globals.shardWorlds,null);
});
test('concurrent changes to the same value fail rather than overwrite',()=>{
  assert.throws(()=>merge(undefined,{one:1},{one:2}),/concurrently/);
  assert.throws(()=>merge({one:1},{},{one:2}),/concurrently/);
  assert.deepEqual(merge({one:1,two:2},{one:3,two:2},{one:1,two:4}),{one:3,two:4});
});
