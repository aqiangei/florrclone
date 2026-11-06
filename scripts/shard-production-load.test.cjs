'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),crypto=require('node:crypto');
const {spawn}=require('node:child_process'),{once}=require('node:events');const {ShardRpc}=require('./shard-rpc.cjs');
test('production-shaped shared startup preserves all offline records and H history',async()=>{
  assert(process.env.SHARD_PRODUCTION_COPY&&process.env.SHARD_COORDINATOR_BIN);
  const root=path.resolve(__dirname,'..'),home=fs.mkdtempSync(path.join(os.tmpdir(),'shard-load-')),children=[],files=[];
  const endpoint='http://127.0.0.1:19097/rpc',key=crypto.randomBytes(32).toString('hex');let rpc;
  const launch=(name,args,env)=>{const fd=fs.openSync(path.join(home,name+'.log'),'w');files.push(fd);const child=spawn(process.execPath,args,{cwd:root,env:{...process.env,...env},stdio:['ignore',fd,fd]});children.push(child);return child;};
  const ready=async(child,url,status)=>{for(let i=0;i<1200;++i){assert.equal(child.exitCode,null,'startup exited');try{if((await fetch(url,{signal:AbortSignal.timeout(5000)})).status===status)return;}catch{}await new Promise(r=>setTimeout(r,100));}throw Error('readiness timeout');};
  try{
    const original=JSON.parse(fs.readFileSync(process.env.SHARD_PRODUCTION_COPY));
    fs.copyFileSync(process.env.SHARD_PRODUCTION_COPY,path.join(home,'seed.json'));
    const host=path.join(home,'host.json'),game=path.join(home,'game.json');
    fs.writeFileSync(host,JSON.stringify({executable:process.env.SHARD_COORDINATOR_BIN,stateBase:path.join(home,'authority'),bootstrap:path.join(home,'seed.json'),port:19097,nodes:{one:{key}}}));
    fs.writeFileSync(game,JSON.stringify({endpoint,node:'one',key,shards:[{id:'one',label:'one',host:'127.0.0.1',port:3097,available:true},{id:'two',label:'two',host:'127.0.0.1',port:3098,available:false}]}));
    await ready(launch('host',['scripts/shard-coordinator-host.cjs'],{FLIX_SHARD_HOST_CONFIG:host}),endpoint,404);
    rpc=new ShardRpc({endpoint,node:'one',boot:'inspect',key});
    const start=Date.now();
    const child=launch('game',['-r','./scripts/deploy-runtime.cjs','dist/server.js','--port','3097','--db',path.join(home,'cache.json'),'--bots','0'],{FLIX_AUTHORITY_MODULE:path.join(root,'scripts/shard-game-authority.cjs'),FLIX_SHARD_GAME_CONFIG:game});
    await ready(child,'http://127.0.0.1:3097/api/health',200);
    console.log('production-sized startup milliseconds:',Date.now()-start);
    await new Promise(resolve=>setTimeout(resolve,10000));
    const stopped=once(child,'exit');child.kill('SIGTERM');assert.deepEqual(await stopped,[0,null]);
    const snapshot=rpc.call({op:'snapshot'});assert(snapshot.ok);
    const records=Object.fromEntries(Object.entries(snapshot.state.accounts).map(([id,a])=>[id,a.record]));
    assert.deepEqual(records,original.players,'offline inventory/progress/history cannot be rewritten by either game');
    assert(!fs.existsSync(path.join(home,'cache.json')));
    console.log('verified offline accounts:',Object.keys(records).length);
    if(process.env.SHARD_LOAD_EVIDENCE)for(const name of ['host','game'])fs.copyFileSync(path.join(home,name+'.log'),process.env.SHARD_LOAD_EVIDENCE+'-'+name+'.log');
  }catch(e){for(const n of ['host','game']){const p=path.join(home,n+'.log');if(fs.existsSync(p))console.error(fs.readFileSync(p,'utf8').slice(-1500));}throw e;}
  finally{
    if(rpc)await rpc.close();for(const child of children.reverse())if(child.exitCode===null){const done=once(child,'exit');child.kill('SIGTERM');const t=setTimeout(()=>child.kill('SIGKILL'),15000);await done;clearTimeout(t);}
    for(const fd of files)fs.closeSync(fd);
    if(process.env.SHARD_LOAD_EVIDENCE)for(const name of ['host','game'])if(fs.existsSync(path.join(home,name+'.log')))fs.copyFileSync(path.join(home,name+'.log'),process.env.SHARD_LOAD_EVIDENCE+'-'+name+'.log');
    assert.equal(path.dirname(home),os.tmpdir());assert(path.basename(home).startsWith('shard-load-'));fs.rmSync(home,{recursive:true});
  }
});
