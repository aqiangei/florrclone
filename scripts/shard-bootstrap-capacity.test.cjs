'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto'),net=require('node:net');
const {spawn}=require('node:child_process'),{once}=require('node:events');
const {ShardRpc,AsyncShardRpc}=require('./shard-rpc.cjs');
test('startup restores snapshot above old 32 MiB limit without enlarging normal async bridge',{timeout:90000},async()=>{
  assert(process.env.SHARD_COORDINATOR_BIN,'native coordinator required');
  const home=fs.mkdtempSync(path.join(os.tmpdir(),'shard-large-bootstrap-'));
  const listener=net.createServer();listener.listen(0,'127.0.0.1');await once(listener,'listening');
  const port=listener.address().port;await new Promise(resolve=>listener.close(resolve));
  const key=crypto.randomBytes(32).toString('hex'),endpoint=`http://127.0.0.1:${port}/rpc`;
  const bootstrap=path.join(home,'seed.json'),stateBase=path.join(home,'state'),configPath=path.join(home,'host.json');
  const history='h'.repeat(33*1024*1024),digest=crypto.createHash('sha256').update(history).digest('hex');
  fs.writeFileSync(bootstrap,JSON.stringify({users:{alice:{id:'a',username:'alice'}},players:{a:{totalXP:10}},legacyFlorrcloneRoot:history}));
  fs.writeFileSync(configPath,JSON.stringify({executable:process.env.SHARD_COORDINATOR_BIN,bootstrap,stateBase,port,nodes:{one:{key}}}),{mode:0o600});
  let child,closed,rpc,asyncRpc;
  async function start(){
    child=spawn(process.execPath,[path.join(__dirname,'shard-coordinator-host.cjs')],{
      env:{...process.env,FLIX_SHARD_HOST_CONFIG:configPath},stdio:['ignore','ignore','ignore']});
    closed=once(child,'exit');
    for(let i=0;i<200;++i){
      assert.equal(child.exitCode,null,'host startup');
      try{if((await fetch(endpoint)).status===404)return;}catch{}
      await new Promise(resolve=>setTimeout(resolve,30));
    }throw Error('host readiness timeout');
  }
  async function stop(){
    if(!child||child.exitCode!==null)return;
    child.kill('SIGTERM');const timer=setTimeout(()=>child.kill('SIGKILL'),10000);
    try{assert.deepEqual(await closed,[0,null]);}finally{clearTimeout(timer);}
  }
  function verify(reply,xp){
    assert(reply.ok,reply.error);assert.equal(reply.state.accounts.a.record.totalXP,xp);
    const retained=reply.state.globals.legacyFlorrcloneRoot;
    assert(retained.length>32*1024*1024);
    assert.equal(crypto.createHash('sha256').update(retained).digest('hex'),digest);
  }
  try{
    await start();rpc=new ShardRpc({endpoint,node:'one',boot:'capacity',key});
    asyncRpc=new AsyncShardRpc({endpoint,node:'one',boot:'capacity',key});
    assert.equal(rpc.buffer.byteLength,128*1024*1024+8);
    assert.equal(asyncRpc.buffer.byteLength,32*1024*1024+8);
    verify(rpc.call({op:'snapshot'}),10);
    const lease=await asyncRpc.call({op:'acquire',requestId:'capacity-acquire',account:'a'});assert(lease.ok);
    const written=await asyncRpc.call({op:'checkpoint_batch',requestId:'capacity-write',writes:[{
      account:'a',epoch:lease.epoch,revision:lease.revision,record:{totalXP:99}}]});
    assert(written.ok);assert(JSON.stringify(written).length<512,'ordinary reply remains sparse');
    await stop();await start();verify(rpc.call({op:'snapshot'}),99);
    const narrow=await asyncRpc.call({op:'read_identity',username:'alice'});
    assert(narrow.ok&&narrow.found);assert.equal(narrow.record.totalXP,99);assert(JSON.stringify(narrow).length<1024);
  }finally{
    if(rpc)await rpc.close();if(asyncRpc)await asyncRpc.close();await stop();
    assert.equal(path.dirname(home),os.tmpdir());assert(path.basename(home).startsWith('shard-large-bootstrap-'));
    fs.rmSync(home,{recursive:true});
  }
});
