'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {apply}=require('./maintenance-drop-bonus.cjs');
const begin=1700000000000;
const options={id:'update-test',stoppedAt:begin,resumedAt:begin+600000};
function fixture(){return {accounts:{private:'unchanged'},councilPolls:[{votes:{a:1}}],players:{
  active:{totalXP:100,inventory:[{type:1,count:2}],dailyDropBonus:{day:123,streak:4,multiplier:3,until:begin+120000}},
  expired:{dailyDropBonus:{day:123,streak:2,multiplier:2,until:begin}},
  absent:{totalXP:20}
}};}
test('update preserves remaining time even when downtime exceeds the bonus; unrelated progress is unchanged',()=>{
  const db=fixture(),before=structuredClone(db);const result=apply(db,options);
  assert.equal(result.extended,1);
  assert.equal(db.players.active.dailyDropBonus.until-options.resumedAt,120000);
  const {maintenanceCredit,...bonus}=db.players.active.dailyDropBonus;
  assert.deepEqual({...bonus,until:before.players.active.dailyDropBonus.until},before.players.active.dailyDropBonus);
  db.players.active.dailyDropBonus=before.players.active.dailyDropBonus;
  assert.deepEqual(db,before);
});
test('repeated update steps do not double credit and later retries add only additional downtime',()=>{
  const db=fixture();apply(db,options);const once=structuredClone(db);
  assert.equal(apply(db,options).extended,0);assert.deepEqual(db,once);
  apply(db,{...options,resumedAt:options.resumedAt+300000});
  assert.equal(db.players.active.dailyDropBonus.until,begin+120000+900000);
  apply(db,{id:'next-update',stoppedAt:begin+950000,resumedAt:begin+1000000});
  assert.equal(db.players.active.dailyDropBonus.until,begin+1070000);
});
test('bad timestamps fail without mutation and zero downtime does not change the save',()=>{
  const db=fixture(),before=structuredClone(db);
  assert.throws(()=>apply(db,{...options,resumedAt:begin-1}));
  assert.throws(()=>apply(db,{...options,stoppedAt:NaN}));
  assert.equal(apply(db,{...options,resumedAt:begin}).extended,0);
  assert.deepEqual(db,before);
});
test('deployment CLI atomically preserves file permissions and credits only the stopped interval',()=>{
  const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),cp=require('node:child_process');
  const directory=fs.mkdtempSync(path.join(os.tmpdir(),'florr-bonus-test-'));
  try{
    const file=path.join(directory,'inventory.json'),now=Date.now();
    const db={players:{a:{totalXP:17,dailyDropBonus:{day:55,streak:3,multiplier:2,until:now-5000}}}};
    fs.writeFileSync(file,JSON.stringify(db),{mode:0o600});
    const result=JSON.parse(cp.execFileSync(process.execPath,[require.resolve('./maintenance-drop-bonus.cjs'),file,'test-cli',String(now-10000)],{encoding:'utf8'}));
    const saved=JSON.parse(fs.readFileSync(file));
    assert.equal(saved.players.a.dailyDropBonus.until-result.resumedAt,5000);
    assert.equal(saved.players.a.totalXP,17);
    assert.equal(saved.players.a.dailyDropBonus.day,55);
    assert.equal(fs.existsSync(file+'.bonus-next'),false);
    if(process.platform!=='win32')assert.equal(fs.statSync(file).mode&0o777,0o600);
  }finally{
    // This path is the exact directory returned by mkdtemp, never user input.
    fs.rmSync(directory,{recursive:true});
  }
});
