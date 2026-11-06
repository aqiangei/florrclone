'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),path=require('node:path');
const {validate,inspect}=require('./cluster-targets.cjs');
function config(){return {mode:'initial',release:'/opt/florrclone/releases/candidate',targets:Object.fromEntries(['one','two'].map((id,n)=>[id,{id,host:'host'+n,expectedHost:'host'+n,sshPort:22,publicPort:8088,backendPort:8788,machineId:String(n+1).repeat(32),key:path.resolve('test-key'),expectedRelease:n===0?'/opt/florrclone/releases/live':null}]))};}
test('explicit target checks reject switched identities and old NAT ports',()=>{
 assert(validate(config()));
 for(const mutate of [c=>c.targets.two.host=c.targets.one.host,c=>c.targets.two.machineId=c.targets.one.machineId,c=>c.targets.two.publicPort=36577,c=>c.targets.two.id='one',c=>delete c.targets.two.sshPort]){const c=config();mutate(c);assert.throws(()=>validate(c));}
});
test('council delegation validates immutable ID list before stopping production',()=>{
 const c=config();c.councilManagerIds=['account-id'];assert(validate(c));
 for(const ids of ['account-id',[null],[''],[' account-id'],['account-id','account-id']]){
   assert.throws(()=>validate({...c,councilManagerIds:ids}));
 }
 assert.throws(()=>validate({...c,mode:'upgrade'}),/initial mode/);
});
test('preflight rejects a changed remote machine before any release operation',()=>{
 const c=config(),files=Object.fromEntries(['server.js','server.wasm','bundle.js','bundle.wasm','bundle.js.bin','bundle.wasm.bin','index.html','council.html','deploy-runtime.cjs','shard-game-authority.cjs'].map(n=>[n,'a'.repeat(64)]));
 const commands=[],one={ssh(command){commands.push(command);return 'f'.repeat(32);}};
 assert.throws(()=>inspect(c,{one,two:one},{files}),/machine identity changed/);
 assert.deepEqual(commands,['cat /etc/machine-id']);
});
