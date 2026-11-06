'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto');
const {acceptance,load}=require('./cluster-deploy.cjs');
test('deployment requires actual checks for both simulations, progress, exclusivity and global rules',()=>{
  const checks={roundTrip:true,progress:true,exclusiveLogin:true,hyperUnique:true,council:true,independentWorlds:true};
  assert(acceptance({ok:true,checks}));
  for(const name of Object.keys(checks)){assert.throws(()=>acceptance({ok:true,checks:{...checks,[name]:false}}),new RegExp(name));}
  assert.throws(()=>acceptance({ok:true}));assert.throws(()=>acceptance({ok:false,checks}));
});
test('paired release requires a reviewed acceptance script and every coordinator runtime dependency',()=>{
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'florr-cluster-config-'));
  try{
    const script=path.join(dir,'accept.cjs');fs.writeFileSync(script,'// immutable reviewed acceptance fixture\n');
    const required=['shard_coordinator','shard-hyper-client.cjs','shard-global-merge.cjs','shard-coordinator-host.cjs','shard-rpc.cjs','shard-rpc-worker.cjs','shard-request-sequence.cjs','shard-directory.cjs','shard-supervisor.cjs','release-check.cjs','florrclone-sharded.service','florr-shard-coordinator.service','nginx-game.conf'];
    const manifest=path.join(dir,'manifest.json'),files=Object.fromEntries(required.map(name=>[name,'a'.repeat(64)]));fs.writeFileSync(manifest,JSON.stringify({files}));
    const c={mode:'initial',release:'/opt/florrclone/releases/fixture',manifest,privateDirectory:dir,acceptanceScript:script,
      acceptanceSha256:crypto.createHash('sha256').update(fs.readFileSync(script)).digest('hex'),protocolVersion:96,contentHash:123,
      targets:Object.fromEntries(['one','two'].map((id,index)=>[id,{id,host:'host'+index,expectedHost:'host'+index,sshPort:22,publicPort:8088,backendPort:8788,
        machineId:String(index+1).repeat(32),key:path.join(dir,'key'),expectedRelease:index===0?'/opt/florrclone/releases/old':null}]))};
    const file=path.join(dir,'config.json');fs.writeFileSync(file,JSON.stringify(c));assert(load(file));
    delete files['shard-supervisor.cjs'];fs.writeFileSync(manifest,JSON.stringify({files}));assert.throws(()=>load(file),/shard-supervisor/);
    fs.appendFileSync(script,'// changed');assert.throws(()=>load(file),/Unreviewed/);
  }finally{
    const resolved=path.resolve(dir);assert.equal(path.dirname(resolved),path.resolve(os.tmpdir()));assert(path.basename(resolved).startsWith('florr-cluster-config-'));fs.rmSync(resolved,{recursive:true,force:true});
  }
});
