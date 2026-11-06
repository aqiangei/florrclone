'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),crypto=require('node:crypto');
const {spawn}=require('node:child_process'),{once}=require('node:events'),net=require('node:net');
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function fixture(executable){
  const home=fs.mkdtempSync(path.join(os.tmpdir(),'shard-host-life-'));
  const key=crypto.randomBytes(32).toString('hex');
  const listener=net.createServer();listener.listen(0,'127.0.0.1');await once(listener,'listening');
  const port=listener.address().port;await new Promise(resolve=>listener.close(resolve));
  const config={executable,port,stateBase:path.join(home,'state'),bootstrap:path.join(home,'seed.json'),nodes:{one:{key}}};
  fs.writeFileSync(config.bootstrap,'{"users":{},"players":{}}');
  let host,closed,code,signal,errors='';
  function start(){
    const filename=path.join(home,'config.json');fs.writeFileSync(filename,JSON.stringify(config),{mode:0o600});
    host=spawn(process.execPath,[path.join(__dirname,'shard-coordinator-host.cjs')],{
      env:{...process.env,FLIX_SHARD_HOST_CONFIG:filename},stdio:['ignore','ignore','pipe']});
    host.stderr.on('data',data=>{errors+=data;});
    closed=once(host,'exit').then(result=>{[code,signal]=result;return result;});
    return host;
  }
  async function ready(){
    for(let i=0;i<100;++i){
      if(code!==undefined)throw Error('host exited: '+errors);
      try{if((await fetch(`http://127.0.0.1:${port}/rpc`)).status===404)return;}catch{}
      await sleep(30);
    }throw Error('host readiness timeout');
  }
  async function exit(){
    let timer;
    try{return await Promise.race([closed,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('host failed to exit')),8000);})]);}
    finally{clearTimeout(timer);}
  }
  async function request(){
    const nonce=crypto.randomBytes(16).toString('hex');
    const body=JSON.stringify({node:'one',boot:'test',nonce,at:Date.now(),request:{op:'snapshot'}});
    const response=await fetch(`http://127.0.0.1:${port}/rpc`,{method:'POST',body,
      headers:{'x-shard-signature':crypto.createHmac('sha256',key).update(body).digest('hex')},signal:AbortSignal.timeout(7000)});
    const text=await response.text();
    assert.equal(response.headers.get('x-shard-signature'),crypto.createHmac('sha256',key).update(text).digest('hex'));
    const value=JSON.parse(text);assert.equal(value.nonce,nonce);
    return {status:response.status,value};
  }
  async function cleanup(){
    if(host&&code===undefined){host.kill('SIGKILL');await closed;}
    assert.equal(path.dirname(home),os.tmpdir());assert(path.basename(home).startsWith('shard-host-life-'));
    fs.rmSync(home,{recursive:true});
  }
  return {home,config,start,ready,exit,request,cleanup};
}

test('killed native coordinator replies unknown to pending RPC and host exits nonzero',{timeout:15000},async()=>{
  assert(process.env.SHARD_COORDINATOR_BIN,'native coordinator required');
  const f=await fixture(process.env.SHARD_COORDINATOR_BIN);let nativePid;
  try{
    const host=f.start();await f.ready();assert.equal((await f.request()).status,200);
    const children=fs.readFileSync(`/proc/${host.pid}/task/${host.pid}/children`,'utf8').trim().split(/\s+/);
    assert.equal(children.length,1);nativePid=Number(children[0]);assert(nativePid>1);
    process.kill(nativePid,'SIGSTOP');
    const reply=f.request();await sleep(200);process.kill(nativePid,'SIGKILL');nativePid=undefined;
    const response=await reply;assert.equal(response.status,503);assert.equal(response.value.error,'commit_outcome_unknown');
    const [code,signal]=await f.exit();assert.equal(signal,null);assert.equal(code,1);
  }finally{if(nativePid){try{process.kill(nativePid,'SIGKILL');}catch{}}await f.cleanup();}
});

test('normal SIGTERM drains an admitted delayed reply before host exits zero',{timeout:15000},async()=>{
  const f=await fixture(process.execPath);
  try{
    // A delayed stdin peer makes the drain ordering deterministic.
    fs.writeFileSync(f.config.stateBase,`const fs=require('node:fs');let work=0,ended=false;
      const finish=()=>{if(ended&&!work)process.exit(0);};
      require('node:readline').createInterface({input:process.stdin}).on('line',line=>{
        ++work;fs.writeFileSync(process.argv[2]+'.admitted','yes');const input=JSON.parse(line);
        setTimeout(()=>{process.stdout.write(JSON.stringify({id:input.id,result:{ok:true}})+'\\n',()=>{--work;finish();});},300);
      }).on('close',()=>{ended=true;finish();});`);
    const host=f.start();await f.ready();const reply=f.request();
    for(let i=0;i<100&&!fs.existsSync(f.config.bootstrap+'.admitted');++i)await sleep(20);
    assert(fs.existsSync(f.config.bootstrap+'.admitted'));host.kill('SIGTERM');
    const response=await reply;assert.equal(response.status,200);assert(response.value.result.ok);
    assert.deepEqual(await f.exit(),[0,null]);
  }finally{await f.cleanup();}
});

test('coordinator spawn failure closes host with nonzero exit',{timeout:15000},async()=>{
  const f=await fixture('/nonexistent-shard-coordinator');
  try{f.start();assert.deepEqual(await f.exit(),[1,null]);}finally{await f.cleanup();}
});
