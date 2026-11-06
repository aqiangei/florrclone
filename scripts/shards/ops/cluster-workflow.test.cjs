'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const {deploy}=require('./cluster-workflow.cjs');
const phases=['preflight','lock','announce','closeGates','stopGames','stopCoordinator','backup','install','startCoordinator','recoverStoppedOwners','startGames','verifyPrivate','openGates','verifyPublic','recordSuccess','unlock'];
function fixture(fail){const events=[],ops={};for(const name of phases)ops[name]=async()=>{events.push(name);if(name===fail)throw Error('injected '+name);if(name==='backup')return {files:[{sha256:'a'.repeat(64)}]};};return {events,ops};}
test('paired deployment verifies both shutdowns and local backup before replacement, validates before opening',async()=>{
 const f=fixture();assert.equal((await deploy(f.ops)).status,'deployed');
 assert.deepEqual(f.events,['preflight','lock','preflight','announce','closeGates','stopGames','stopCoordinator','backup','install','startCoordinator','recoverStoppedOwners','startGames','verifyPrivate','openGates','verifyPublic','recordSuccess','unlock']);
});
test('failed announcement keeps live game running and a failed backup never replaces production',async()=>{
 for(const failure of ['announce','stopGames','stopCoordinator','backup']){
  const f=fixture(failure);await assert.rejects(deploy(f.ops));assert(!f.events.includes('install'));assert(!f.events.includes('openGates'));
  if(failure==='announce')assert(!f.events.includes('stopGames'));assert.equal(f.events.at(-1),'unlock');
 }
});
test('new backend validation failure and public failure preserve new data behind maintenance',async()=>{
 for(const failure of ['verifyPrivate','verifyPublic']){
  const f=fixture(failure);await assert.rejects(deploy(f.ops),error=>error.deployment.phase===failure);
  assert.equal(f.events.at(-2),'closeGates');assert(!f.events.includes('recordSuccess'));
  assert.equal(f.events.includes('openGates'),failure==='verifyPublic');
  assert(!f.events.some(name=>/rollback|restore/i.test(name)));
 }
});
test('lock cleanup failure preserves a completed deployment result without rolling back live progress',async()=>{
 const f=fixture('unlock');const result=await deploy(f.ops);
 assert.equal(result.status,'deployed');assert(result.warnings[0].includes('lock cleanup failed'));
 assert.equal(f.events.filter(name=>name==='closeGates').length,1);
});
