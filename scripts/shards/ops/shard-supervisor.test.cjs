'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const {proveStopped,recover}=require('./shard-supervisor.cjs');
const stopped={LoadState:'loaded',MainPID:'0',ControlPID:'0',ActiveState:'inactive',SubState:'dead'};
test('supervisor rejects a live main process or leftover child despite missing heartbeat',()=>{
 assert(proveStopped(stopped,[],10));
 assert.throws(()=>proveStopped({...stopped,MainPID:'88'},[88],10),/still running/);
 assert.throws(()=>proveStopped(stopped,[88],10),/not proven/);
 assert(proveStopped({...stopped,ActiveState:'activating',SubState:'start-pre',ControlPID:'10'},[10],10));
 assert.throws(()=>proveStopped({...stopped,ActiveState:'activating',SubState:'start-pre',ControlPID:'10'},[10,88],10),/not proven/);
});
test('supervisor restores only its stopped shard and preserves other shard ownership',async()=>{
 const accounts={a:{phase:'active',shard:'two',boot:'old-two',epoch:4},b:{phase:'active',shard:'one',boot:'live-one',epoch:8},c:{phase:'offline',shard:'',boot:'',epoch:9}},requests=[];
 const rpc=async r=>{requests.push(r);if(r.op==='snapshot')return {ok:true,state:{accounts}};return {ok:true};};
 const result=await recover(rpc,'two',true);assert.equal(result.recovered,1);
 assert.equal(requests[1].account,'a');assert.equal(requests[1].stoppedBoot,'old-two');assert.equal(requests[1].epoch,4);assert.equal(requests.length,2);
});
test('unknown recovery outcome stops further mutations and a claimed transfer is not stolen',async()=>{
 const old={a:{phase:'prepared',shard:'one',boot:'old-one',epoch:3},b:{phase:'active',shard:'one',boot:'old-one',epoch:4}};
 let calls=0;await assert.rejects(recover(async r=>{calls++;return r.op==='snapshot'?{ok:true,state:{accounts:old}}:{ok:false,error:'commit_unknown'};},'one',true),/commit_unknown/);assert.equal(calls,2);
 let snapshots=0;
 const result=await recover(async r=>r.op==='snapshot'?{ok:true,state:{accounts:++snapshots===1?{a:old.a}:{a:{phase:'active',shard:'two',boot:'new-two',epoch:4}}}}:{ok:false,error:'recovery_conflict'},'one',true);
 assert.equal(result.completedElsewhere,1);assert.equal(result.recovered,0);
});
