'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto');
const {acceptance,load,create}=require('./cluster-deploy.cjs');
test('maintenance continuation requires confirmed announcements, matching backup and closed gate',async()=>{
  const root=fs.mkdtempSync(path.join(os.tmpdir(),'florr-maintenance-'));
  try{
    const prior=path.join(root,'prior');fs.mkdirSync(prior);const bytes=Buffer.from('{}');
    const sha=crypto.createHash('sha256').update(bytes).digest('hex');
    fs.writeFileSync(path.join(prior,'inventory.json'),bytes);
    fs.writeFileSync(path.join(prior,'backup-metadata.json'),JSON.stringify({timestamp:'20261106T090604Z',nextRelease:'/opt/florrclone/releases/hotfix',sha256:sha}));
    fs.writeFileSync(path.join(prior,'maintenance-bonus.json'),JSON.stringify({resumedAt:Date.now()-1000}));
    const log=path.join(root,'announcement.log');
    const config={mode:'initial',backupRoot:root,privateDirectory:root,targets:{one:{expectedRelease:'/opt/florrclone/releases/hotfix'}},maintenanceContinuation:{backupDirectory:prior,announcementLog:log}};
    let gated=true;
    const one={ssh(cmd){if(cmd.startsWith('sha256sum'))return sha+' inventory.json';assert(gated,'gate open');return '';}};
    fs.writeFileSync(log,'已确认全服广播：120秒后维护');
    await assert.rejects(create(config,{}, {one}).announce(),/Missing confirmed/);
    fs.writeFileSync(log,[120,60,30,10].map(n=>'已确认全服广播：'+n+'秒后维护').join('\n'));
    await create(config,{}, {one}).announce();
    gated=false;await assert.rejects(create(config,{}, {one}).announce(),/gate open/);
  }finally{fs.rmSync(root,{recursive:true,force:true});}
});
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
