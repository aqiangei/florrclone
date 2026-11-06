'use strict';
// Runs behind maintenance, using the dedicated operator's existing account.
// Does not forge real H, publish an issue, reset progress, or create accounts.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const {Worker}=require('node:worker_threads');
const {once}=require('node:events');const {ShardRpc}=require('../../shard-rpc.cjs');
const {transport}=require('./cluster-targets.cjs');
const wire=require('../../smoke-websocket.cjs');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const str=s=>{const b=Buffer.from(s),n=Buffer.alloc(2);n.writeUInt16LE(b.length);return Buffer.concat([n,b]);};
function textAt(b,at){const n=b.readUInt16LE(at);return {text:b.toString('utf8',at+2,at+2+n),next:at+2+n};}

// Authority queries can block this thread while waiting for an RPC response.
// Keep the byte relay on a separate thread so those responses can still arrive.
async function openSshRelay(target,remotePort,localPort=0){
  const worker=new Worker(path.join(__dirname,'ssh-relay-worker.cjs'),{
    workerData:{target,remotePort,localPort}
  });
  const [ready]=await once(worker,'message');
  if(!ready.port)throw Error(ready.error||'SSH relay failed to start');
  return {port:ready.port,async close(){
    if(worker.threadId===-1)return;
    const exited=once(worker,'exit');
    worker.postMessage('close');
    await exited;
  }};
}

async function socket(port,version,hash){
  const ws=await wire.wsConnect(port).catch(error=>{
    throw Error('WebSocket upgrade failed at '+port+': '+error.message);
  });
  const inbox=ws.smokeReader.queue;
  let pending;
  ws.smokeReader.listeners.add(frame=>{
    if(pending?.type===frame[0]){
      const {resolve,timer}=pending;
      pending=null;
      clearTimeout(timer);
      resolve(frame);
    }else if(inbox.length<512)inbox.push(frame);
  });
  ws.on('close',()=>{
    if(!pending)return;
    const {reject,timer}=pending;
    pending=null;
    clearTimeout(timer);
    reject(Error('WebSocket closed at '+port));
  });
  const wait=type=>new Promise((resolve,reject)=>{
    const at=inbox.findIndex(frame=>frame[0]===type);
    if(at>=0){resolve(inbox.splice(at,1)[0]);return;}
    if(ws.destroyed){reject(Error('WebSocket closed at '+port));return;}
    assert(!pending,'Concurrent protocol wait');
    const timer=setTimeout(()=>{
      pending=null;
      reject(Error('Protocol response timeout at '+port+': '+type));
    },15000);
    pending={type,resolve,reject,timer};
  });
  const send=frame=>wire.send(ws,frame);
  try{
    const hello=Buffer.alloc(7);
    hello[0]=1;
    hello.writeUInt16LE(version,1);
    hello.writeUInt32LE(hash,3);
    send(hello);
    assert.equal((await wait(1))[3],1,'Protocol/content rejected at '+port);
    return {send,wait,close:()=>ws.destroy()};
  }catch(error){
    ws.destroy();
    throw error;
  }
}
async function checkPair(config,admin,rpc,ports){
  const sockets=[];
  try{
    const connect=async port=>{const s=await socket(port,config.protocolVersion,config.contentHash);sockets.push(s);return s;};
    const authenticate=async(s,kind,...fields)=>{s.send(Buffer.concat([Buffer.from([kind]),...fields.map(str)]));const b=await s.wait(2);return {status:b[1],token:textAt(b,2).text};};
    const identity=()=>rpc.call({op:'read_identity',username:admin.username});
    const offline=async()=>{for(let i=0;i<100;i++){if(identity().ownership?.phase==='offline')return;await sleep(100);}throw Error('Source did not save and release');};
    const first=await connect(ports.one),login=await authenticate(first,3,admin.username,admin.password);assert.equal(login.status,0);await first.wait(3);
    const start=identity();assert(start.ok&&start.found);
    assert.equal(start.ownership?.shard,'one');
    const projection=r=>Object.fromEntries(['inventory','loadout','loadoutPresets','totalXP','stars','skills','mazeTotalXP'].map(k=>[k,r[k]]));
    const expected=projection(start.record);
    const second=await connect(ports.two),busy=await authenticate(second,3,admin.username,admin.password);assert.notEqual(busy.status,0,'duplicate cross-shard login admitted');
    second.close();
    first.close();await offline();
    const destination=await connect(ports.two);
    const moved=await authenticate(destination,4,login.token);assert.equal(moved.status,0);await destination.wait(3);assert.deepEqual(projection(identity().record),expected);
    const directoryOne=await (await fetch('http://127.0.0.1:'+ports.one+'/api/shards')).json(),directoryTwo=await (await fetch('http://127.0.0.1:'+ports.two+'/api/shards')).json();
    assert.equal(directoryOne.current,'one');assert.equal(directoryTwo.current,'two');assert.deepEqual(directoryOne.choices,directoryTwo.choices);
    const views=[];
    for(const port of [ports.one,ports.two]){const r=await fetch('http://127.0.0.1:'+port+'/api/council',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+login.token},body:'{"action":"view"}'});assert(r.ok);views.push(await r.json());}
    assert(views.every(view=>!view.error&&view.me===start.account),'Council session not authenticated on both shards');
    assert.deepEqual(views[0].players,views[1].players);assert.deepEqual(views[0].polls,views[1].polls);
    if(views[0].players.length)assert.equal(views[0].players.reduce((s,r)=>s+r.units,0),1000000);
    destination.close();await offline();
    const back=await connect(ports.one);assert.equal((await authenticate(back,4,login.token)).status,0);await back.wait(3);assert.deepEqual(projection(identity().record),expected);back.close();await offline();
    return views[0].players.length;
  }finally{
    for(const socket of sockets)socket.close();
  }
}
async function main(file){
  const config=JSON.parse(fs.readFileSync(file,'utf8'));
  const evidence=JSON.parse(fs.readFileSync(config.pairedEvidence,'utf8'));
  assert.equal(evidence.ok,true);assert.equal(evidence.logSha256,sha(fs.readFileSync(evidence.log)));
  assert.match(fs.readFileSync(evidence.log,'utf8'),/# pass 1\b/);
  assert.equal(evidence.testSha256,sha(fs.readFileSync(evidence.test)));
  const manifest=JSON.parse(fs.readFileSync(config.manifest,'utf8'));assert.deepEqual(evidence.files,manifest.files);
  const relays=[];const ports={};let rpc;
  try{
    for(const id of ['one','two']){
      const target=config.targets[id],remote=transport(target);
      assert.equal(remote.ssh('cat /etc/machine-id').trim(),target.machineId);assert.equal(evidence.machineIds[id],target.machineId);
      assert.equal(remote.ssh('readlink -f /opt/florrclone/current').trim(),config.release);
      assert.equal(remote.ssh('systemctl is-active florrclone').trim(),'active');
      const relay=await openSshRelay(target,8788);
      relays.push(relay);
      ports[id]=relay.port;
      const health=await fetch('http://127.0.0.1:'+relay.port+'/api/health',
        {signal:AbortSignal.timeout(15000)});
      assert(health.ok&&(await health.json()).ok,'Private listener unavailable on '+id);
    }
    const authority=await openSshRelay(config.targets.one,19090);
    relays.push(authority);
    const keyConfig=JSON.parse(fs.readFileSync(path.join(config.privateDirectory,'shard-one.json')));
    rpc=new ShardRpc({...keyConfig,endpoint:'http://127.0.0.1:'+authority.port+'/rpc',boot:'release-check-'+crypto.randomBytes(8).toString('hex')});
    if(config.councilManagerIds?.length){
      const snapshot=rpc.call({op:'snapshot'});assert(snapshot.ok);
      for(const id of config.councilManagerIds){
        assert.equal(snapshot.state.globals.councilManagers?.[id],true,'Council delegation missing');
        const accounts=Object.values(snapshot.state.globals.users).filter(a=>a.id===id);
        assert.equal(accounts.length,1);assert(!accounts[0].guest&&!accounts[0].adminBanned);
      }
    }
    const admin=JSON.parse(fs.readFileSync(config.adminFiles.one,'utf8'));
    const councilPlayers=await checkPair(config,admin,rpc,ports);
    // Actual cross-host synthetic H/independent-world evidence is bound to the
    // exact 25 installed artifacts; no real player's H is touched at cutover.
    assert.equal(evidence.checks.hyperUnique,true);assert.equal(evidence.checks.independentWorlds,true);
    const result={ok:true,checks:{roundTrip:true,progress:true,exclusiveLogin:true,hyperUnique:true,council:true,independentWorlds:true},
      evidence:config.pairedEvidence,protocol:config.protocolVersion,contentHash:config.contentHash,councilPlayers};
    return result;
  }finally{if(rpc)await rpc.close();await Promise.all(relays.map(relay=>relay.close()));}
}
if(require.main===module)main(process.argv[2]).then(r=>console.log(JSON.stringify(r))).catch(e=>{console.error(e.message);process.exitCode=1;});
module.exports={main,checkPair,openSshRelay};
