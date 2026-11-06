'use strict';
// Runs as a one-shot SSH command on the authority host. Prints hashes and paths,
// never credentials or account contents. Does not stop or modify the game.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
function bundle(state){
  assert(state&&state.schema===1&&Number.isSafeInteger(state.sequence)&&state.sequence>=0,'Invalid authority sequence');
  assert(state.accounts&&state.globals&&state.globals.users&&state.requests,'Incomplete authority snapshot');
  const inventory=structuredClone(state.globals);inventory.players={};
  for(const [id,account]of Object.entries(state.accounts)){
    assert(account.record&&typeof account.record==='object'&&!Array.isArray(account.record),'Invalid account record');
    inventory.players[id]=account.record;
  }
  // A full durable checkpoint at one sequence supersedes its preceding journal.
  // Retain ownership, epochs, pending transfers and dedup metadata in the full
  // snapshot; restoring requires fencing any old game processes first.
  return {'authority.snapshot':JSON.stringify(state),'authority.journal':'',
    'authority.snapshot.lock':'','bootstrap.json':JSON.stringify(inventory)};
}
async function main(){
  const config=JSON.parse(fs.readFileSync(process.env.FLIX_SHARD_BACKUP_CONFIG||'/etc/florrclone/shard-backup.json','utf8'));
  assert.equal(config.node,'backup');
  const endpoint=new URL(config.endpoint);
  assert(endpoint.protocol==='http:'&&endpoint.hostname==='127.0.0.1'&&endpoint.pathname==='/rpc'&&
    !endpoint.username&&!endpoint.password&&!endpoint.search&&!endpoint.hash,'Backup endpoint must be the local authority');
  assert(typeof config.key==='string'&&config.key.length>=32,'Missing backup credential');
  const nonce=crypto.randomBytes(16).toString('hex');
  const body=JSON.stringify({node:config.node,boot:crypto.randomBytes(16).toString('hex'),at:Date.now(),nonce,request:{op:'snapshot'}});
  const sign=text=>crypto.createHmac('sha256',config.key).update(text).digest('hex');
  const response=await fetch(config.endpoint,{method:'POST',headers:{'Content-Type':'application/json','X-Shard-Signature':sign(body)},body,signal:AbortSignal.timeout(20000)});
  const raw=await response.text(),signature=response.headers.get('x-shard-signature');
  assert(signature&&/^[a-f0-9]{64}$/.test(signature)&&crypto.timingSafeEqual(Buffer.from(signature,'hex'),Buffer.from(sign(raw),'hex')),'Unauthenticated backup response');
  assert(response.ok,'Authority snapshot unavailable');
  const reply=JSON.parse(raw);assert(reply.nonce===nonce&&reply.result?.ok,'Authority snapshot rejected');
  const state=reply.result.state,files=bundle(state);
  const directory=fs.mkdtempSync('/var/tmp/florrclone-authority-backup-');fs.chmodSync(directory,0o700);
  const metadata=[];
  try{
    for(const [name,text]of Object.entries(files)){
      const file=path.join(directory,name),bytes=Buffer.from(text),fd=fs.openSync(file,'wx',0o600);
      try{fs.writeFileSync(fd,bytes);fs.fsyncSync(fd);}finally{fs.closeSync(fd);}
      metadata.push({name,sha256:crypto.createHash('sha256').update(bytes).digest('hex'),bytes:bytes.length});
    }
    console.log(JSON.stringify({directory,files:metadata,sequence:state.sequence,accounts:Object.keys(state.accounts).length,kind:'online-authority-checkpoint',capturedAt:new Date().toISOString()}));
  }catch(error){
    const resolved=path.resolve(directory);assert.equal(path.dirname(resolved),path.resolve('/var/tmp'));
    assert(path.basename(resolved).startsWith('florrclone-authority-backup-'));
    fs.rmSync(resolved,{recursive:true,force:true});throw error;
  }
}
if(require.main===module)main().catch(error=>{console.error('Authority backup failed: '+error.message);process.exitCode=1;});
module.exports={bundle};
