'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),crypto=require('node:crypto');
const {spawn}=require('node:child_process'),{once}=require('node:events');
const {ShardRpc,AsyncShardRpc}=require('./shard-rpc.cjs');
test('async RPC keeps the event loop running while an acknowledgement is delayed',async()=>{
  const key=crypto.randomBytes(32).toString('hex');
  const server=require('node:http').createServer((req,res)=>{
    let text='';req.on('data',chunk=>{text+=chunk;});req.on('end',()=>{
      const input=JSON.parse(text);
      setTimeout(()=>{
        const body=JSON.stringify({nonce:input.nonce,result:{ok:true}});
        res.writeHead(200,{'X-Shard-Signature':crypto.createHmac('sha256',key).update(body).digest('hex')});res.end(body);
      },150);
    });
  });
  server.listen(0,'127.0.0.1');await once(server,'listening');
  const rpc=new AsyncShardRpc({endpoint:`http://127.0.0.1:${server.address().port}/rpc`,node:'one',boot:'boot',key});
  let ticks=0;const timer=setInterval(()=>{++ticks;},5);
  try {assert((await rpc.call({op:'snapshot'})).ok);assert(ticks>=10);}
  finally{clearInterval(timer);await rpc.close();await new Promise(r=>server.close(r));}
});
test('authenticated RPC preserves fences and replays a committed transfer after restart',async()=>{
  assert(process.env.SHARD_COORDINATOR_BIN,'build the native coordinator first');
  const directory=fs.mkdtempSync(path.join(os.tmpdir(),'shards-rpc-test-'));
  const oneKey=crypto.randomBytes(32).toString('hex'),twoKey=crypto.randomBytes(32).toString('hex'),backupKey=crypto.randomBytes(32).toString('hex');
  const bootstrap=path.join(directory,'seed.json'),configPath=path.join(directory,'host.json');
  const supervisorKey=crypto.randomBytes(32).toString('hex');
  const councilToken=crypto.randomBytes(32).toString('hex');
  const tokenHash=crypto.createHash('sha256').update(councilToken).digest('hex');
  fs.writeFileSync(bootstrap,JSON.stringify({users:{alice:{id:'alice',username:'alice'}},players:{alice:{totalXP:21}},
    sessions:{[tokenHash]:{userId:'alice',username:'alice',expiresAt:Date.now()+3600000}}}));
  const port=19091,endpoint=`http://127.0.0.1:${port}/rpc`;
  fs.writeFileSync(configPath,JSON.stringify({executable:process.env.SHARD_COORDINATOR_BIN,
    bootstrap,stateBase:path.join(directory,'state'),port,nodes:{one:{key:oneKey},two:{key:twoKey},backup:{key:backupKey,role:'backup'},
      supervisor:{key:supervisorKey,role:'supervisor',scopeShard:'one'}}}),{mode:0o600});
  let child;const clients=[];
  async function start(){
    child=spawn(process.execPath,[path.join(__dirname,'shard-coordinator-host.cjs')],{
      env:{...process.env,FLIX_SHARD_HOST_CONFIG:configPath},stdio:['ignore','ignore','pipe']});
    let errors='';child.stderr.on('data',chunk=>{errors+=chunk;});
    for(let i=0;i<100;++i){
      if(child.exitCode!==null)throw Error(errors);
      try{const r=await fetch(endpoint);if(r.status===404)return;}catch{}
      await new Promise(r=>setTimeout(r,30));
    }
    throw Error('host startup timeout');
  }
  async function stop(){if(child&&child.exitCode===null){const done=once(child,'exit');child.kill('SIGTERM');await done;}}
  try {
    await start();
    const one=new ShardRpc({endpoint,node:'one',boot:'boot-one',key:oneKey});clients.push(one);
    const two=new ShardRpc({endpoint,node:'two',boot:'boot-two',key:twoKey});clients.push(two);
    const backup=new ShardRpc({endpoint,node:'backup',boot:'backup-job',key:backupKey});clients.push(backup);
    const supervisor=new ShardRpc({endpoint,node:'supervisor',boot:'supervisor-job',key:supervisorKey});clients.push(supervisor);
    assert(supervisor.call({op:'snapshot'}).ok);
    assert.equal(supervisor.call({op:'acquire',requestId:'bad-supervisor',account:'alice'}).error,'supervisor_scope');
    assert.equal(supervisor.call({op:'recover_stopped',requestId:'wrong-shard',account:'alice',stoppedShard:'two',scopeShard:'two'}).error,'supervisor_scope');
    assert(backup.call({op:'snapshot'}).ok);
    assert.equal(backup.call({op:'acquire',requestId:'forged-game',account:'alice',role:'game'}).error,'snapshot_only');
    assert.equal(backup.call({op:'recover_stopped',requestId:'forged-supervisor',account:'alice',supervisor:true}).error,'snapshot_only');
    assert.equal(one.call({op:'snapshot'}).state.accounts.alice.record.totalXP,21);
    const acquire={op:'acquire',requestId:'login',account:'alice'};
    const lease=one.call(acquire);assert(lease.ok);
    assert.equal(two.call({...acquire,requestId:'steal'}).error,'account_busy');
    assert.equal(two.call({op:'recover_stopped',requestId:'escalate',account:'alice',
      stoppedShard:'one',stoppedBoot:'boot-one',epoch:lease.epoch,role:'supervisor',supervisor:true}).error,'supervisor_required');
    const prepare={op:'prepare',requestId:'prepare',account:'alice',target:'two',transfer:'transfer1',
      epoch:lease.epoch,revision:lease.revision,record:{totalXP:99}};
    assert(one.call(prepare).ok);
    const claim={op:'claim',requestId:'claim',account:'alice',transfer:'transfer1'};
    assert.equal(two.call(claim).record.totalXP,99);
    await stop();await start();
    const replay=two.call(claim);assert(replay.ok&&replay.replayed&&replay.currentOwnership);
    assert.equal(replay.record.totalXP,99);
    const create={op:'council',requestId:'council-create',serial:1,ackThrough:0,token:councilToken,
      input:{action:'create',title:'Shared proposal'}};
    const created=one.call(create);assert(created.ok);assert(!created.body.error,created.body.error);
    assert.equal(created.body.polls.length,1);
    const pollId=created.body.polls[0].id;
    const vote={op:'council',requestId:'council-vote',serial:1,ackThrough:0,token:councilToken,
      input:{action:'vote',id:pollId,choice:'yes'}};
    const voted=two.call(vote);assert(voted.ok);assert.equal(voted.body.polls[0].yes,1000000);
    assert.equal(one.call({op:'council',input:{action:'view'}}).body.polls[0].yes,1000000);
    await stop();await start();
    assert(one.call(create).replayed,'original publication is deduplicated after restart');
    assert.equal(one.call(create).body.polls.length,1);
    const changed={...vote,requestId:'council-change',serial:2,ackThrough:0,input:{...vote.input,choice:'no'}};
    assert.equal(two.call(changed).body.polls[0].no,1000000);
    assert(two.call(vote).replayed);
    assert.equal(two.call(vote).body.polls[0].no,1000000,'replayed old vote cannot overwrite later vote');
    assert.equal(two.call({...vote,input:{...vote.input,choice:'abstain'}}).error,'request_id_reused');
    assert(one.call({op:'council',requestId:'anonymous',serial:2,ackThrough:1,input:{action:'create',title:'denied'}}).body.error);
    const denied=await fetch(endpoint,{method:'POST',body:JSON.stringify({node:'one'}),headers:{'x-shard-signature':'0'.repeat(64)}});
    assert.equal(denied.status,401);
    const nonce=crypto.randomBytes(16).toString('hex');
    const body=JSON.stringify({node:'one',boot:'boot-one',nonce,at:Date.now(),request:{op:'snapshot'}});
    const headers={'x-shard-signature':crypto.createHmac('sha256',oneKey).update(body).digest('hex')};
    assert.equal((await fetch(endpoint,{method:'POST',body,headers})).status,200);
    assert.equal((await fetch(endpoint,{method:'POST',body,headers})).status,409);
    assert.equal((await fetch(endpoint,{method:'POST',body:'null'})).status,400);
  } finally {
    for(const client of clients)await client.close();
    await stop();
    for(const name of fs.readdirSync(directory))fs.unlinkSync(path.join(directory,name));
    fs.rmdirSync(directory);
  }
});
