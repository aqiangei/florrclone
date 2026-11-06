'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),cp=require('node:child_process');
test('stopped coordinator pauses bonuses durably, retries once, and refuses live ownership',()=>{
  const binary=process.env.SHARD_COORDINATOR_BIN;assert(binary);
  const home=fs.mkdtempSync(path.join(os.tmpdir(),'shard-maintenance-'));
  const base=path.join(home,'authority'),seed=path.join(home,'seed.json'),now=1700000000000;
  const original={users:{alice:{id:'a',username:'alice'},bob:{id:'b',username:'bob'}},players:{
    a:{totalXP:12,dailyDropBonus:{day:123,streak:4,multiplier:3,until:now+120000},inventory:{apex:{petal_rose:1}}},
    b:{totalXP:44,dailyDropBonus:{day:122,streak:2,multiplier:2,until:now-1}}
  }};
  const rpc=request=>JSON.parse(cp.execFileSync(binary,[base,seed],{input:JSON.stringify({id:'probe',node:'one',boot:'test',request})+'\n',encoding:'utf8'})).result;
  const pause=(id,time)=>cp.spawnSync(binary,[base,seed,'--pause-bonus',id,String(time)],{encoding:'utf8'});
  try{
    fs.writeFileSync(seed,JSON.stringify(original));
    const first=pause('update1',now);assert.equal(first.status,0,first.stderr);assert.equal(JSON.parse(first.stdout).paused,1);
    const state=rpc({op:'snapshot'}).state;
    assert.deepEqual(state.accounts.b.record,original.players.b);
    const saved=state.accounts.a.record;
    assert.equal(saved.dailyDropBonus.until,0);assert.equal(saved.dailyDropBonus.maintenancePause.remainingMillis,120000);
    assert.equal(saved.dailyDropBonus.day,123);assert.equal(saved.dailyDropBonus.streak,4);assert.equal(saved.dailyDropBonus.multiplier,3);
    assert.deepEqual(saved.inventory,original.players.a.inventory);assert.equal(saved.totalXP,12);
    assert.equal(pause('update1',now+600000).status,0);
    assert.deepEqual(rpc({op:'snapshot'}).state,state);
    // Existing authority never falls back to its stale bootstrap file.
    fs.unlinkSync(seed);assert.deepEqual(rpc({op:'snapshot'}).state,state);
    assert(rpc({op:'acquire',requestId:'claim',account:'a'}).ok);
    const active=rpc({op:'snapshot'}).state;
    assert.notEqual(pause('update2',now+100000).status,0);
    assert.deepEqual(rpc({op:'snapshot'}).state,active);
  }finally{
    assert.equal(path.dirname(path.resolve(home)),path.resolve(os.tmpdir()));
    assert(path.basename(home).startsWith('shard-maintenance-'));fs.rmSync(home,{recursive:true});
  }
});
