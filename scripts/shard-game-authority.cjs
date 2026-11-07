'use strict';
// Only initial bootstrap blocks. Authentication, commits and lease release are
// polled async jobs; ordinary saves never copy the full globals dictionary.
// Do not enable in production until transfer/H/council integration is accepted.
const fs=require('node:fs'),crypto=require('node:crypto');
const {isDeepStrictEqual:equal}=require('node:util');
const {ShardRpc,AsyncShardRpc}=require('./shard-rpc.cjs');
const {RequestSequence}=require('./shard-request-sequence.cjs');
const {directory}=require('./shard-directory.cjs');
const config=JSON.parse(fs.readFileSync(process.env.FLIX_SHARD_GAME_CONFIG,'utf8'));
if(!['one','two'].includes(config.node))throw Error('explicit shard identity required');
config.boot=config.boot||crypto.randomBytes(16).toString('hex');
const publicDirectory=directory(config);
const sync=new ShardRpc(config),asyncRpc=new AsyncShardRpc(config);
let state=null,pending=null,failed=false;
let presencePending=false,presenceRows=[],presenceAt=0;
const outgoingNotices=[],incomingNotices=[],seenNotices=new Map();
const sequence=new RequestSequence();
const leases=new Map();
const authJobs=new Map(),reserved=new Map();
let quiescing=false;
let mutationTail=Promise.resolve(),mutationCount=0,releaseJob=null,releaseDesired=new Set();
function mutate(run){
  ++mutationCount;
  const job=mutationTail.then(run);
  mutationTail=job.catch(()=>{}).finally(()=>{--mutationCount;});return job;
}
const clone=value=>value===undefined?undefined:structuredClone(value);
const object=value=>value!==null&&typeof value==='object'&&!Array.isArray(value);
const worldTables=new Set(['bossClocks','sacrificeSpawn']);
function request(op,fields={}){return sequence.request(op,fields);}
function callSync(command){return sequence.complete(command,sync.call(command));}
async function callAsync(command){return sequence.complete(command,await asyncRpc.call(command));}
function pollAuthJob(input,run){
  const key=input.action+'/'+input.attempt;
  let job=authJobs.get(key);
  if(!job) {
    if(quiescing||authJobs.size>=512)return {ok:false,error:'busy'};
    job={result:null,cancelled:false};authJobs.set(key,job);
    Promise.resolve().then(run).then(result=>{job.result=result;if(job.cancelled)authJobs.delete(key);},()=>{
      failed=true;job.result={ok:false,error:'transport_unknown'};if(job.cancelled)authJobs.delete(key);
    });
  }
  return job.result||{pending:true};
}
function readState(reply){
  if(!reply||!reply.ok||!reply.state)throw Error('authority snapshot unavailable');
  if(reply.worldCheckpointVersion!==1)throw Error('authority world checkpoint version mismatch');
  return reply.state;
}
function database(s=state){
  const out=clone(s.globals);delete out.shardWorlds;
  const world=s.globals.shardWorlds?.[config.node]||{};
  for(const key of worldTables)if(world[key]!==undefined)out[key]=clone(world[key]);
  out.players={};for(const [id,a]of Object.entries(s.accounts))out.players[id]=clone(a.record);
  return out;
}
function refresh(){if(pending)throw Error('commit still pending');state=readState(sync.call({op:'snapshot'}));return JSON.stringify(database());}
const {merge}=require('./shard-global-merge.cjs');
function buildWrites(delta){
  const records={...delta.players};
  for(const user of Object.values(delta.users||{}))if(user?.id&&state.accounts[user.id]&&!Object.hasOwn(records,user.id))
    records[user.id]=state.accounts[user.id].record;
  const writes=[];
  for(const [id,record]of Object.entries(records)){
    if(!record)throw Error('account deletion requires a lifecycle transaction');
    const existing=state.accounts[id],lease=leases.get(id);
    if(!existing){
      const pair=Object.entries(delta.users||{}).find(([,u])=>u.id===id);
      if(!pair)throw Error('new player without identity');
      writes.push({account:id,record,create:true,username:pair[0]});
    }else{
      if(!lease)throw Error('account not exclusively owned');
      writes.push({account:id,record,...lease});
    }
  }
  return writes;
}
function accepted(reply,{writes,patch,global,world}){
  if(!object(reply.epochs)||!object(reply.revisions))throw Error('invalid commit acknowledgement');
  if(global){applyPatch(patch);state.globalRevision=reply.globalRevision;}
  if(world){
    const local=((state.globals.shardWorlds??={})[config.node]??={});
    for(const [key,value]of Object.entries(world)){
      if(value===null)delete local[key];else local[key]=clone(value);
    }
  }
  state.sequence=reply.sequence;
  for(const write of writes){
    const lease={epoch:reply.epochs[write.account],revision:reply.revisions[write.account]};
    leases.set(write.account,lease);
    state.accounts[write.account]={...(state.accounts[write.account]||{}),...lease,record:clone(write.record),phase:'active',shard:config.node,boot:config.boot};
  }
}
function patchFor(delta){
  const patch={users:clone(delta.users||{}),sessions:clone(delta.sessions||{}),globals:{}};
  const world={};
  for(const [key,value]of Object.entries(delta.globals||{})) {
    if(key==='shardWorlds')throw Error('private world namespace');
    (worldTables.has(key)?world:patch.globals)[key]=clone(value);
  }
  return {patch,world:Object.keys(world).length?world:null};
}
function selected(patch){
  const out={users:{},sessions:{},globals:{}};
  for(const table of Object.keys(out))for(const key of Object.keys(patch[table]))
    out[table][key]=clone((table==='globals'?state.globals[key]:state.globals[table]?.[key])??null);
  return out;
}
function applyPatch(patch){
  for(const table of ['users','sessions','globals'])for(const [key,value]of Object.entries(patch[table]||{})) {
    const target=table==='globals'?state.globals:(state.globals[table]??={});
    if(value===null)delete target[key];else target[key]=clone(value);
  }
}
function commitPlan(delta){
  const writes=buildWrites(delta),{patch,world}=patchFor(delta);
  const global=Object.values(patch).some(rows=>Object.keys(rows).length);
  return {writes,patch,world,global,original:global?selected(patch):null};
}
function commitCommand(plan){
  const fields={writes:plan.writes};
  if(plan.world)fields.world=plan.world;
  if(!plan.global)return request('checkpoint_batch',fields);
  return request('global_patch',{...fields,globalRevision:state.globalRevision,patch:plan.patch});
}
function refreshCommand(plan){return {op:'read_globals',keys:Object.fromEntries(Object.entries(plan.patch).map(([table,rows])=>[table,Object.keys(rows)]))};}
function rebase(plan,current){
  if(!current?.ok||!object(current.patch))throw Error('global conflict refresh unavailable');
  plan.patch=merge(plan.original,plan.patch,current.patch);plan.original=clone(current.patch);
  applyPatch(current.patch);state.globalRevision=current.globalRevision;
}
function outcome(reply){
  if(reply?.ok)return 'committed';
  if(['transport_unknown','commit_unknown','authority_frozen','commit_outcome_unknown'].includes(reply?.error)){console.error('[authority] uncertain operation:',reply.error);failed=true;return 'unknown';}
  console.error('[authority] rejected operation:',typeof reply?.error==='string'?reply.error:'invalid_reply');
  return 'rejected';
}
function fullDelta(next){
  const before=database(),delta={users:{},players:{},sessions:{},globals:{}};
  for(const table of ['users','players','sessions'])for(const key of new Set([...Object.keys(before[table]||{}),...Object.keys(next[table]||{})]))
    if(!equal(before[table]?.[key],next[table]?.[key]))delta[table][key]=next[table]?.[key]??null;
  for(const key of new Set([...Object.keys(before),...Object.keys(next)]))
    if(!['users','players','sessions'].includes(key)&&!equal(before[key],next[key]))delta.globals[key]=next[key]??null;
  return delta;
}
function commitSync(delta){
  if(failed)return 'unknown';
  let dispatched=false;
  try{
    const plan=commitPlan(delta);
    if(!plan.global&&!plan.writes.length&&!plan.world)return 'committed';
    for(let attempt=0;attempt<3;++attempt){
      dispatched=true;const reply=callSync(commitCommand(plan));
      if(reply.ok){accepted(reply,plan);return 'committed';}
      dispatched=false;
      if(reply.error!=='global_conflict')return outcome(reply);
      rebase(plan,sync.call(refreshCommand(plan)));
    }
    return 'rejected';
  }catch(error){console.error('[authority] synchronous commit failed:',error.message);if(dispatched){failed=true;return 'unknown';}return 'rejected';}
}
async function commitAsync(delta){
  if(failed)return 'unknown';
  let dispatched=false;
  try{
    const plan=commitPlan(delta);
    if(!plan.global&&!plan.writes.length&&!plan.world)return 'committed';
    for(let attempt=0;attempt<3;++attempt){
      dispatched=true;const reply=await callAsync(commitCommand(plan));
      if(reply.ok){accepted(reply,plan);return 'committed';}
      dispatched=false;
      if(reply.error!=='global_conflict')return outcome(reply);
      rebase(plan,await asyncRpc.call(refreshCommand(plan)));
    }
    return 'rejected';
  }catch(error){console.error('[authority] asynchronous commit failed:',error.message);if(dispatched){failed=true;return 'unknown';}return 'rejected';}
}
const hyper=require('./shard-hyper-client.cjs')({
  read:command=>asyncRpc.call(command), mutate, operator:config.node==='one',
  write:(op,fields)=>callAsync(request(op,fields)),
  lease:account=>leases.get(account),
  install:(id,a)=>{leases.set(id,{epoch:a.epoch,revision:a.revision});state.accounts[id]=clone(a);},
  installForges:forges=>{state.globals.hyperForges=clone(forges);},
});
module.exports={
  directory(){return publicDirectory;},
  snapshot:refresh,
  async council(token,input){
    if(failed||quiescing)return {error:'共享服务暂不可用，请稍后重试'};
    const reply=await mutate(()=>callAsync(request('council',{token,input})));
    if(!reply?.ok){outcome(reply);return {error:'操作结果尚未确认，请稍后查询议题状态'};}
    return reply.body;
  },
  commit(text){return commitSync(fullDelta(JSON.parse(text)));},
  commitDelta(delta){
    const key=JSON.stringify(delta);
    if(pending){
      if(pending.key!==key){console.error('[authority] pending commit changed');failed=true;return 'unknown';}
      if(!pending.result)return 'pending';
      const result=pending.result;pending=null;return result;
    }
    const job=pending={key,result:null};mutate(()=>commitAsync(clone(delta))).then(result=>{job.result=result;});return 'pending';
  },
  invoke(input){
    if(input.action==='chatAnnouncement'){
      if(![0,1,7].includes(input.channel)||typeof input.text!=='string'||
          Buffer.byteLength(input.text)>65535||typeof input.author!=='string'||
          Buffer.byteLength(input.author)>128)return {ok:false};
      if(outgoingNotices.length===256)outgoingNotices.shift();
      outgoingNotices.push({id:crypto.randomBytes(16).toString('hex'),kind:'chat',
        channel:input.channel,author:input.author,text:input.text});
      return {ok:true};
    }
    if(input.action==='mobAnnouncement'){
      if(outgoingNotices.length===256)outgoingNotices.shift();
      outgoingNotices.push({id:crypto.randomBytes(16).toString('hex'),mob:input.mob,
        rarity:input.rarity,summoned:input.summoned});
      return {ok:true};
    }
    if(input.action==='presence') {
      if(!presencePending&&!failed&&!quiescing) {
        presencePending=true;
        const notices=outgoingNotices.slice(0,64);
        asyncRpc.call({op:'presence',rows:input.rows,notices}).then(reply=>{
          if(reply?.ok&&Array.isArray(reply.rows)){
            presenceRows=reply.rows;presenceAt=Date.now();
            const acknowledged=new Set(notices.map(notice=>notice.id));
            for(let i=outgoingNotices.length-1;i>=0;--i)
              if(acknowledged.has(outgoingNotices[i].id))outgoingNotices.splice(i,1);
            for(const [id,at] of seenNotices)if(presenceAt-at>60000)seenNotices.delete(id);
            for(const notice of reply.notices||[]){
              if(seenNotices.has(notice.id))continue;
              seenNotices.set(notice.id,presenceAt);
              incomingNotices.push(notice);
            }
          }
          else {presenceRows=[];presenceAt=0;}
        },()=>{presenceRows=[];presenceAt=0;}).finally(()=>{presencePending=false;});
      }
      return {current:config.node,rows:Date.now()-presenceAt<8000?presenceRows:[],notices:incomingNotices.splice(0)};
    }

    if(input.action==='publicView')return pollAuthJob(input,()=>asyncRpc.call({op:'public_view',input:input.query}));
    if(input.action==='finishView'){
      const key='publicView/'+input.attempt,job=authJobs.get(key);
      if(job){job.cancelled=true;if(job.result)authJobs.delete(key);}return {ok:true};
    }
    if(input.action==='hyperBegin')return hyper.begin(input);
    if(input.action==='hyperService')return hyper.service();
    if(input.action==='hyperReady')return hyper.ready(input.transaction);
    if(input.action==='hyperThaw')return hyper.thaw(input.transaction);
    if(input.action==='finishAuth') {
      for(const action of ['lookup','acquireAsync']) {
        const key=action+'/'+input.attempt,job=authJobs.get(key);
        if(job){job.cancelled=true;if(job.result)authJobs.delete(key);}
      }
      for(const [id,attempt]of reserved)if(attempt===input.attempt)reserved.delete(id);
      return {ok:true};
    }
    if(input.action==='quiesce') {
      quiescing=true;
      for(const job of authJobs.values())job.cancelled=true;
      if(asyncRpc.active||asyncRpc.queue.length||mutationCount||[...authJobs.values()].some(job=>!job.result))return {pending:true};
      authJobs.clear();reserved.clear();return {ok:!failed};
    }
    if(failed)return {ok:false,error:'authority_unavailable'};
    if(input.action==='lookup')return pollAuthJob(input,async()=>{
      const query={op:'read_identity'};
      if(input.sessionHash)query.sessionHash=input.sessionHash;else query.username=input.username;
      return asyncRpc.call(query);
    });
    if(input.action==='acquireAsync')return pollAuthJob(input,()=>mutate(async()=>{
      const existing=leases.get(input.account);
      const fields={account:input.account,identityKey:input.identityKey,expectedIdentity:input.expectedIdentity};
      if(input.sessionHash){fields.sessionHash=input.sessionHash;fields.expectedSession=input.expectedSession;}
      if(existing)Object.assign(fields,existing);
      const reply=await callAsync(request(existing?'reaffirm':'acquire',fields));
      if(reply?.ok) {
        leases.set(input.account,{epoch:reply.epoch,revision:reply.revision});
        state.accounts[input.account]={...(state.accounts[input.account]||{}),epoch:reply.epoch,revision:reply.revision,
          record:reply.record,phase:'active',shard:config.node,boot:config.boot};
        state.globals.users??={};state.globals.users[input.identityKey]=clone(input.expectedIdentity);
        if(input.sessionHash){state.globals.sessions??={};state.globals.sessions[input.sessionHash]=clone(input.expectedSession);}
        const job=authJobs.get('acquireAsync/'+input.attempt);
        if(job&&!job.cancelled)reserved.set(input.account,input.attempt);
        return {...reply,alreadyOwned:!!existing};
      }
      outcome(reply);return reply;
    }));
    if(input.action==='releaseUnusedAsync') {
      if(process.env.FLIX_SHARD_DIAGNOSTICS==='1') {
        const counts=JSON.stringify({active:input.active,reserved:[...reserved.keys()],leases:[...leases.keys()],pending:!!releaseJob});
        if(counts!==module.exports.lastReleaseTrace){console.error('[release_trace]',counts);module.exports.lastReleaseTrace=counts;}
      }
      releaseDesired=new Set(input.active||[]);
      if(releaseJob) {
        if(!releaseJob.result)return {pending:true};
        const result=releaseJob.result;releaseJob=null;return result;
      }
      // The active list belongs to this poll. A queued release must never sweep
      // leases created, renewed, or reserved after the game supplied that list.
      const candidates=[...leases].filter(([account])=>!releaseDesired.has(account)&&!reserved.has(account));
      const job=releaseJob={result:null};
      mutate(async()=>{
        for(const [account,lease]of candidates) {
          if(leases.get(account)!==lease||releaseDesired.has(account)||reserved.has(account))continue;
          const reply=await callAsync(request('release',{account,...lease,record:state.accounts[account].record}));
          if(!reply?.ok){outcome(reply);return reply;}
          leases.delete(account);Object.assign(state.accounts[account],{phase:'offline',shard:'',boot:'',epoch:reply.epoch,revision:reply.revision});
        }
        if(quiescing) {
          const verified=await asyncRpc.call({op:'read_owned'});
          if(!verified?.ok)return verified||{ok:false,error:'transport_unknown'};
          if(!Array.isArray(verified.accounts)||verified.accounts.length)
            return {ok:false,error:'ownership_not_released'};
        }
        return {ok:true};
      }).then(result=>{job.result=result;},()=>{failed=true;job.result={ok:false,error:'transport_unknown'};});
      return {pending:true};
    }
    return {ok:false,error:'unknown_adapter_action'};
  },
};
