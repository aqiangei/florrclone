'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {RequestSequence}=require('./shard-request-sequence.cjs');
test('interleaved sync and async responses acknowledge only a completed prefix',()=>{
  const seq=new RequestSequence(),first=seq.request('global_commit'),second=seq.request('acquire');
  seq.complete(second,{ok:true});assert.equal(seq.ackThrough,0);
  const third=seq.request('release');assert.equal(third.ackThrough,0);
  seq.complete(first,{ok:true});assert.equal(seq.ackThrough,2);
  seq.complete(third,{ok:false,error:'account_conflict'});assert.equal(seq.ackThrough,3);
  assert.equal(seq.request('checkpoint').ackThrough,3);
});
test('unknown outcome is retained until the same immutable request resolves',()=>{
  for(const reply of [null,{}, {ok:false,error:'transport_unknown'},{ok:false,error:'commit_unknown'},
    {ok:false,error:'authority_frozen'},{ok:false,error:'commit_outcome_unknown'},
    {ok:false,error:'coordinator_unavailable'},{ok:false,error:'future_error'}]){
    const seq=new RequestSequence(),first=seq.request('prepare',{record:{totalXP:42}}),bytes=JSON.stringify(first);
    seq.complete(first,reply);seq.complete(seq.request('checkpoint'),{ok:true});
    assert.equal(seq.ackThrough,0);assert.equal(JSON.stringify(first),bytes);
    assert.throws(()=>{first.record.totalXP=0;},TypeError);
    seq.complete(first,{ok:true,replayed:true});assert.equal(seq.ackThrough,2);
  }
});
test('request numbering cannot be overridden and acknowledgements are actor scoped',()=>{
  const a=new RequestSequence(),b=new RequestSequence();
  const request=a.request('release',{serial:99,ackThrough:98,requestId:'fake',op:'acquire'});
  assert.deepEqual(request,{serial:1,ackThrough:0,requestId:'r1',op:'release'});
  assert.throws(()=>b.complete(request,{ok:true}),/another actor/);
  a.complete(request,{ok:false,error:'not_sent'});assert.equal(a.ackThrough,1);
  a.complete(request,{ok:true});assert.equal(a.completed.size,0);
});
