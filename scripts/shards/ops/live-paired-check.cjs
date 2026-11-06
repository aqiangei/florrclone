'use strict';
// Runs behind maintenance, using the dedicated operator's existing account.
// Does not forge real H, publish an issue, reset progress, or create accounts.
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const {once}=require('node:events');const {ShardRpc}=require('../../shard-rpc.cjs');
const {transport}=require('./cluster-targets.cjs');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const str=s=>{const b=Buffer.from(s),n=Buffer.alloc(2);n.writeUInt16LE(b.length);return Buffer.concat([n,b]);};
function textAt(b,at){const n=b.readUInt16LE(at);return {text:b.toString('utf8',at+2,at+2+n),next:at+2+n};}
async function socket(port,version,hash){
  const ws=new WebSocket('ws://127.0.0.1:'+port+'/ws','binary');ws.binaryType='arraybuffer';
  const inbox=[],waiters=[];let buffer=Buffer.alloc(0);
  ws.addEventListener('message',event=>{
    buffer=Buffer.concat([buffer,Buffer.from(event.data)]);
    while(buffer.length>=4){const n=buffer.readUInt32LE();assert(n<=1048576);if(buffer.length<4+n)break;
      const b=Buffer.from(buffer.subarray(4,4+n));buffer=buffer.subarray(4+n);
      const at=waiters.findIndex(w=>w.type===b[0]);if(at>=0){const w=waiters.splice(at,1)[0];clearTimeout(w.timer);w.resolve(b);}else if(inbox.length<512)inbox.push(b);
    }
  });
  const wait=type=>new Promise((resolve,reject)=>{const at=inbox.findIndex(b=>b[0]===type);if(at>=0){resolve(inbox.splice(at,1)[0]);return;}const w={type,resolve};w.timer=setTimeout(()=>{const at=waiters.indexOf(w);if(at>=0)waiters.splice(at,1);reject(Error('Protocol response timeout: '+type));},15000);waiters.push(w);});
  const send=b=>{const n=Buffer.alloc(4);n.writeUInt32LE(b.length);ws.send(Buffer.concat([n,b]));};
  await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('WebSocket connect timeout')),10000);ws.addEventListener('open',()=>{clearTimeout(t);resolve();},{once:true});ws.addEventListener('error',event=>{clearTimeout(t);reject(Error('WebSocket failed at '+port+': '+(event.error?.message||event.message||'connection error')));},{once:true});});
  const hello=Buffer.alloc(7);hello[0]=1;hello.writeUInt16LE(version,1);hello.writeUInt32LE(hash,3);send(hello);assert.equal((await wait(1))[3],1);
  return {ws,send,wait,close:()=>ws.close()};
}
async function main(file){
  const config=JSON.parse(fs.readFileSync(file,'utf8'));
  const evidence=JSON.parse(fs.readFileSync(config.pairedEvidence,'utf8'));
  assert.equal(evidence.ok,true);assert.equal(evidence.logSha256,sha(fs.readFileSync(evidence.log)));
  assert.match(fs.readFileSync(evidence.log,'utf8'),/# pass 1\b/);
  assert.equal(evidence.testSha256,sha(fs.readFileSync(evidence.test)));
  const manifest=JSON.parse(fs.readFileSync(config.manifest,'utf8'));assert.deepEqual(evidence.files,manifest.files);
  const tunnels=[],sockets=[];let rpc;
  try{
    for(const [id,port]of [['one',43881],['two',43882]]){
      const target=config.targets[id],remote=transport(target);
      assert.equal(remote.ssh('cat /etc/machine-id').trim(),target.machineId);assert.equal(evidence.machineIds[id],target.machineId);
      assert.equal(remote.ssh('readlink -f /opt/florrclone/current').trim(),config.release);
      assert.equal(remote.ssh('systemctl is-active florrclone').trim(),'active');
      const args=['-N','-i',target.key,'-p',String(target.sshPort),'-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ExitOnForwardFailure=yes','-L',port+':127.0.0.1:8788'];
      if(id==='one')args.push('-L','43890:127.0.0.1:19090');args.push('root@'+target.host);
      tunnels.push(cp.spawn('ssh',args,{stdio:'ignore',windowsHide:true}));
    }
    for(const port of [43881,43882]){let ready=false;for(let i=0;i<60;i++){try{if((await fetch('http://127.0.0.1:'+port+'/api/health')).ok){ready=true;break;}}catch{}await sleep(100);}assert(ready,'Private listener unavailable');}
    const keyConfig=JSON.parse(fs.readFileSync(path.join(config.privateDirectory,'shard-one.json')));
    rpc=new ShardRpc({...keyConfig,endpoint:'http://127.0.0.1:43890/rpc',boot:'release-check-'+crypto.randomBytes(8).toString('hex')});
    if(config.councilManagerIds?.length){
      const snapshot=rpc.call({op:'snapshot'});assert(snapshot.ok);
      for(const id of config.councilManagerIds){
        assert.equal(snapshot.state.globals.councilManagers?.[id],true,'Council delegation missing');
        const accounts=Object.values(snapshot.state.globals.users).filter(a=>a.id===id);
        assert.equal(accounts.length,1);assert(!accounts[0].guest&&!accounts[0].adminBanned);
      }
    }
    const admin=JSON.parse(fs.readFileSync(config.adminFiles.one,'utf8'));
    const connect=async port=>{const s=await socket(port,config.protocolVersion,config.contentHash);sockets.push(s);return s;};
    const authenticate=async(s,kind,...fields)=>{s.send(Buffer.concat([Buffer.from([kind]),...fields.map(str)]));const b=await s.wait(2);return {status:b[1],token:textAt(b,2).text};};
    const identity=()=>rpc.call({op:'read_identity',username:admin.username});
    const offline=async()=>{for(let i=0;i<100;i++){if(identity().ownership?.phase==='offline')return;await sleep(100);}throw Error('Source did not save and release');};
    const first=await connect(43881),login=await authenticate(first,3,admin.username,admin.password);assert.equal(login.status,0);await first.wait(3);
    const start=identity();assert(start.ok&&start.found);
    const projection=r=>Object.fromEntries(['inventory','loadout','loadoutPresets','totalXP','stars','skills','mazeTotalXP'].map(k=>[k,r[k]]));
    const expected=projection(start.record);
    const second=await connect(43882),busy=await authenticate(second,3,admin.username,admin.password);assert.notEqual(busy.status,0,'duplicate cross-shard login admitted');
    first.close();await offline();
    const moved=await authenticate(second,4,login.token);assert.equal(moved.status,0);await second.wait(3);assert.deepEqual(projection(identity().record),expected);
    const directoryOne=await (await fetch('http://127.0.0.1:43881/api/shards')).json(),directoryTwo=await (await fetch('http://127.0.0.1:43882/api/shards')).json();
    assert.equal(directoryOne.current,'one');assert.equal(directoryTwo.current,'two');assert.deepEqual(directoryOne.choices,directoryTwo.choices);
    const views=[];
    for(const port of [43881,43882]){const r=await fetch('http://127.0.0.1:'+port+'/api/council',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+login.token},body:'{"action":"view"}'});assert(r.ok);views.push(await r.json());}
    assert(!views[0].error&&!views[1].error);assert.deepEqual(views[0].players,views[1].players);assert.deepEqual(views[0].polls,views[1].polls);
    if(views[0].players.length)assert.equal(views[0].players.reduce((s,r)=>s+r.units,0),1000000);
    second.close();await offline();
    const back=await connect(43881);assert.equal((await authenticate(back,4,login.token)).status,0);await back.wait(3);assert.deepEqual(projection(identity().record),expected);back.close();await offline();
    // Actual cross-host synthetic H/independent-world evidence is bound to the
    // exact 25 installed artifacts; no real player's H is touched at cutover.
    assert.equal(evidence.checks.hyperUnique,true);assert.equal(evidence.checks.independentWorlds,true);
    const result={ok:true,checks:{roundTrip:true,progress:true,exclusiveLogin:true,hyperUnique:true,council:true,independentWorlds:true},
      evidence:config.pairedEvidence,protocol:config.protocolVersion,contentHash:config.contentHash,councilPlayers:views[0].players.length};
    return result;
  }finally{for(const s of sockets)s.close();if(rpc)await rpc.close();for(const child of tunnels){const done=once(child,'exit');child.kill();await Promise.race([done,sleep(3000)]);}}
}
if(require.main===module)main(process.argv[2]).then(r=>console.log(JSON.stringify(r))).catch(e=>{console.error(e.message);process.exitCode=1;});
module.exports={main};
