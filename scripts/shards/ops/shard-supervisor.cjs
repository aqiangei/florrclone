'use strict';
// Root-only systemd pre-start recovery. A network timeout is never evidence that
// an old game stopped. Only an empty service cgroup (or this exact pre-start
// process alone) permits recovering leases belonging to this shard.
const fs=require('node:fs'),cp=require('node:child_process'),crypto=require('node:crypto'),assert=require('node:assert/strict');
function proveStopped(properties,pids,self){
  assert.equal(properties.LoadState,'loaded','Game service is not loaded');
  assert.equal(properties.MainPID,'0','A game process is still running');
  const inactive=properties.ActiveState==='inactive'&&properties.SubState==='dead'&&properties.ControlPID==='0'&&pids.length===0;
  const prestart=properties.ActiveState==='activating'&&properties.SubState==='start-pre'&&properties.ControlPID===String(self)&&
    pids.length===1&&pids[0]===self;
  assert(inactive||prestart,'Old game termination is not proven');
  return true;
}
function localProof(unit='florrclone.service'){
  assert(/^[a-zA-Z0-9_-]+[.]service$/.test(unit),'Invalid systemd unit');
  const raw=cp.execFileSync('systemctl',['show',unit,'-p','LoadState','-p','MainPID','-p','ControlPID','-p','ActiveState','-p','SubState','-p','ControlGroup'],{encoding:'utf8'});
  const fields=Object.fromEntries(raw.trim().split('\n').map(line=>{const at=line.indexOf('=');return [line.slice(0,at),line.slice(at+1)];}));
  let pids=[];
  if(fields.ControlGroup){
    assert.equal(fields.ControlGroup,'/system.slice/'+unit,'Unexpected service cgroup');
    const file='/sys/fs/cgroup'+fields.ControlGroup+'/cgroup.procs';
    try{pids=fs.readFileSync(file,'utf8').trim().split(/\s+/).filter(Boolean).map(Number);}
    catch(error){if(error.code!=='ENOENT')throw error;}
  }
  return proveStopped(fields,pids,process.pid);
}
async function recover(rpc,shard,proven){
  assert(proven===true,'Termination proof required');assert(['one','two'].includes(shard));
  const first=await rpc({op:'snapshot'});assert(first?.ok&&first.state?.accounts,'Authority snapshot unavailable');
  const targets=Object.entries(first.state.accounts).filter(([,a])=>a.shard===shard&&['active','prepared'].includes(a.phase));
  let serial=0,recovered=0,completedElsewhere=0;
  for(const [account,old]of targets){
    const n=++serial;
    const reply=await rpc({op:'recover_stopped',requestId:'recovery-'+n,serial:n,ackThrough:n-1,account,
      stoppedShard:shard,stoppedBoot:old.boot,epoch:old.epoch});
    if(reply?.ok){recovered++;continue;}
    if(reply?.error==='recovery_conflict'){
      // The other shard may have claimed an already-prepared transfer while
      // this service was down. Its new ownership must never be reclaimed.
      const latest=await rpc({op:'snapshot'});assert(latest?.ok,'Recovery conflict cannot be inspected');
      const current=latest.state?.accounts?.[account];
      if(current&&(current.phase==='offline'||current.shard!==shard||current.boot!==old.boot)){completedElsewhere++;continue;}
    }
    throw Error('Lease recovery did not commit: '+(reply?.error||'unknown outcome'));
  }
  return {shard,recovered,completedElsewhere};
}
async function main(probe=false){
  assert(process.getuid&&process.getuid()===0,'Supervisor must run as root');
  const file='/etc/florrclone/shard-supervisor.json',stat=fs.statSync(file);
  assert(stat.uid===0&&(stat.mode&0o077)===0,'Supervisor credential must be root-only');
  const config=JSON.parse(fs.readFileSync(file,'utf8')),game=JSON.parse(fs.readFileSync('/etc/florrclone/shard-game.json','utf8'));
  assert(['one','two'].includes(config.shard)&&config.shard===game.node,'Supervisor shard identity mismatch');
  assert.equal(config.node,'supervisor-'+config.shard);assert(typeof config.key==='string'&&config.key.length>=32);
  const url=new URL(config.endpoint);
  assert(url.protocol==='http:'&&url.hostname==='127.0.0.1'&&url.pathname==='/rpc'&&!url.search&&!url.hash&&!url.username&&!url.password,'Local coordinator endpoint required');
  const proof=probe?false:localProof(),boot=crypto.randomBytes(16).toString('hex');
  const sign=text=>crypto.createHmac('sha256',config.key).update(text).digest('hex');
  const rpc=async request=>{
    const nonce=crypto.randomBytes(16).toString('hex');
    const body=JSON.stringify({node:config.node,boot,nonce,at:Date.now(),request});
    const response=await fetch(config.endpoint,{method:'POST',headers:{'Content-Type':'application/json','X-Shard-Signature':sign(body)},body,signal:AbortSignal.timeout(request.op==='snapshot'?120000:15000)});
    const text=await response.text(),signature=response.headers.get('x-shard-signature');
    assert(signature&&/^[a-f0-9]{64}$/.test(signature)&&crypto.timingSafeEqual(Buffer.from(signature,'hex'),Buffer.from(sign(text),'hex')),'Unverified supervisor response');
    const decoded=JSON.parse(text);assert.equal(decoded.nonce,nonce);assert(response.ok,'Supervisor request rejected');return decoded.result;
  };
  if(probe){
    const reply=await rpc({op:'snapshot'});assert(reply?.ok&&reply.state?.accounts,'Coordinator not ready');
    console.log(JSON.stringify({event:'coordinator-ready',shard:config.shard,sequence:reply.state.sequence}));
  }else console.log(JSON.stringify({event:'stopped-shard-recovered',...await recover(rpc,config.shard,proof)}));
}
if(require.main===module){
  assert(process.argv.length===2||(process.argv.length===3&&process.argv[2]==='--probe'),'Unsupported supervisor argument');
  main(process.argv[2]==='--probe').catch(error=>{console.error('Game pre-start recovery failed: '+error.message);process.exitCode=1;});
}
module.exports={proveStopped,localProof,recover};
