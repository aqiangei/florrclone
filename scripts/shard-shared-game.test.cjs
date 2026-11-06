'use strict';
// Real coordinator + two shipping WASM game processes, synthetic accounts only.
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto');
const {spawn}=require('node:child_process'),{once}=require('node:events');
const {ShardRpc}=require('./shard-rpc.cjs');
const wire=require('./smoke-auth.js');
const root=path.resolve(__dirname,'..');
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
test('two real WASM games share one account and forbid simultaneous ownership',async()=>{
  assert(process.env.SHARD_COORDINATOR_BIN,'native coordinator required');
  const home=fs.mkdtempSync(path.join(os.tmpdir(),'shards-shared-game-'));
  const children=[],sockets=[],logs=[];
  const remote=process.env.SHARD_REMOTE_CONFIG?await require('./shard-remote-test.cjs')(process.env.SHARD_REMOTE_CONFIG,home):null;
  const keys={one:crypto.randomBytes(32).toString('hex'),two:crypto.randomBytes(32).toString('hex')};
  const coordinatorPort=19092,ports={one:3093,two:3094};
  const endpoint=`http://127.0.0.1:${coordinatorPort}/rpc`;
  let inspector;
  function launch(name,args,env){
    const log=fs.openSync(path.join(home,name+'.log'),'w');logs.push(log);
    const child=remote?remote.launch(name,args,env,log):spawn(process.execPath,args,{cwd:root,env:{...process.env,...env},stdio:['ignore',log,log]});
    children.push(child);return child;
  }
  async function ready(child,url,status=200){
    const started=Date.now();
    for(let i=0;i<1200;++i){
      if(child.exitCode!==null)throw Error('process exited before readiness');
      try{if((await fetch(url,{signal:AbortSignal.timeout(3000)})).status===status){console.log('ready',url,Date.now()-started,'ms');return;}}catch{}
      await sleep(100);
    }throw Error('readiness timeout');
  }
  async function connect(node){
    const socket=await wire.wsConnect(ports[node]);sockets.push(socket);
    const hello=Buffer.alloc(7);hello[0]=wire.ClientMessage.Hello;hello.writeUInt16LE(wire.ver,1);hello.writeUInt32LE(wire.CONTENT_HASH,3);
    wire.send(socket,hello);
    const handshake=wire.parseAuth(await wire.readMessages(socket,3000)); assert(handshake.welcome?.accepted,'handshake: '+JSON.stringify(handshake.welcome));
    return socket;
  }
  async function authenticate(socket,kind,...strings){
    wire.send(socket,Buffer.concat([Buffer.from([kind]),...strings.map(wire.encStr)]));
    return wire.parseAuth(await wire.readMessages(socket,2500));
  }
  async function offline(id){
    for(let i=0;i<60;++i){
      const reply=inspector.call({op:'snapshot'});assert(reply.ok,JSON.stringify({phase:'offline',error:reply.error}));
      if(reply.state.accounts[id]?.phase==='offline')return;
      await sleep(100);
    }throw Error('account ownership was not released: '+JSON.stringify(inspector.call({op:'snapshot'}).state?.accounts[id]));
  }
  async function council(node,token,input){
    const response=await fetch(`http://127.0.0.1:${ports[node]}/api/council`,{method:'POST',
      headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},body:JSON.stringify(input)});
    assert.equal(response.status,200);return response.json();
  }
  try{
    const bootstrap=path.join(home,'seed.json');
    const seed=process.env.SHARD_PRODUCTION_COPY?JSON.parse(fs.readFileSync(process.env.SHARD_PRODUCTION_COPY,'utf8')):{users:{},players:{}};
    if(process.env.SHARD_PRODUCTION_COPY) {
      // Isolated load rehearsal only: exclude copied users from synthetic poll
      // eligibility and remove their polls/forge state in this temporary copy.
      // The separate production-load test checks unchanged real account records.
      // Preserve guest flags: converting existing registered players to guests
      // triggers guest normalization and is not representative of the save.
      for(const account of Object.values(seed.users))account.admin=true;
      seed.councilPolls={};seed.hyperForges={};delete seed.hyperTransition;
      console.log('production-shaped offline account count:',Object.keys(seed.players).length);
    }
    fs.writeFileSync(bootstrap,JSON.stringify(seed));
    const hostConfig=path.join(home,'host.json');fs.writeFileSync(hostConfig,JSON.stringify({
      executable:process.env.SHARD_COORDINATOR_BIN,stateBase:path.join(home,'authority'),bootstrap,port:coordinatorPort,
      nodes:{one:{key:keys.one},two:{key:keys.two}}}),{mode:0o600});
    const coordinator=launch('coordinator',['scripts/shard-coordinator-host.cjs'],{FLIX_SHARD_HOST_CONFIG:hostConfig});
    await ready(coordinator,endpoint,404);
    inspector=new ShardRpc({endpoint,node:'one',boot:'inspector',key:keys.one});
    for(const node of ['one','two']){
      fs.mkdirSync(path.join(home,node,'db'),{recursive:true});
      const shards=Object.entries(ports).map(([id,port])=>({id,label:id,host:'127.0.0.1',port,available:true}));
      const config=path.join(home,node+'.json');fs.writeFileSync(config,JSON.stringify({endpoint,node,key:keys[node],shards}),{mode:0o600});
      const child=launch(node,['-r','./scripts/deploy-runtime.cjs','dist/server.js','--port',String(ports[node]),
        '--db',path.join(home,node,'db','forbidden-local.json'),'--bots','0'],{
        FLIX_AUTHORITY_MODULE:path.join(root,'scripts/shard-game-authority.cjs'),FLIX_SHARD_GAME_CONFIG:config});
      await ready(child,`http://127.0.0.1:${ports[node]}/api/health`);
      const directory=await (await fetch(`http://127.0.0.1:${ports[node]}/api/shards`)).json();
      assert.equal(directory.current,node);assert.equal(directory.choices.length,2);assert(!JSON.stringify(directory).includes(keys[node]));
    }
    const one=await connect('one');
    const registered=await authenticate(one,wire.ClientMessage.Register,'sharedplayer','test-password-123');
    assert.equal(registered.auth?.status,wire.AuthStatus.Ok,'register persisted on one');
    assert(registered.profile,'first profile');
    const state=inspector.call({op:'snapshot'}).state;
    const id=state.globals.users.sharedplayer.id;
    assert.equal(state.accounts[id].shard,'one');
    if(process.env.SHARD_PLAY_ONLY==='1') {
      const dimensions=Buffer.alloc(4);dimensions.writeUInt16LE(1280);dimensions.writeUInt16LE(720,2);
      const messages=wire.readMessages(one,5000);
      wire.send(one,Buffer.concat([Buffer.from([wire.ClientMessage.JoinGame]),dimensions,wire.encStr('garden'),wire.encStr('PlayProbe'),Buffer.from([0])]));
      assert((await messages).some(m=>m[0]===wire.ServerMessage.Snapshot));
      one.end();console.log('PLAY_ONLY_FIN');await offline(id);return;
    }
    const two=await connect('two');
    const busy=await authenticate(two,wire.ClientMessage.Login,'sharedplayer','test-password-123');
    assert.notEqual(busy.auth?.status,wire.AuthStatus.Ok,'second world cannot admit same account');
    one.destroy();await offline(id);
    // A test actor modifies the released account through the same fenced API;
    // the next real game login must consume that newer authoritative progress.
    const lease=inspector.call({op:'acquire',requestId:'test-progress-acquire',account:id});assert(lease.ok);
    const released=inspector.call({op:'release',requestId:'test-progress-release',account:id,
      epoch:lease.epoch,revision:lease.revision,record:{...lease.record,totalXP:1000}});assert(released.ok);
    const signedIn=await authenticate(two,wire.ClientMessage.Login,'sharedplayer','test-password-123');
    assert.equal(signedIn.auth?.status,wire.AuthStatus.Ok,'login on two after source saves and releases');
    assert.equal(signedIn.profile?.xp,1000);assert.equal(signedIn.profile?.stacks,registered.profile.stacks);
    const firstView=await council('one',registered.auth.token,{action:'view'});
    const secondView=await council('two',signedIn.auth.token,{action:'view'});
    assert.equal(firstView.me,id);assert.equal(secondView.me,id);
    assert.deepEqual(firstView.players,secondView.players);assert(firstView.canPublish);
    const created=await council('one',registered.auth.token,{action:'create',title:'Two shard proposal'});
    assert(!created.error,created.error);assert.equal(created.polls.length,1);
    const poll=created.polls[0].id;
    const voted=await council('two',signedIn.auth.token,{action:'vote',id:poll,choice:'yes'});
    assert.equal(voted.polls[0].yes,1000000);
    const changed=await council('one',registered.auth.token,{action:'vote',id:poll,choice:'no'});
    assert.equal(changed.polls[0].no,1000000);assert.equal(changed.polls[0].count,1);
    assert.equal((await council('two','',{action:'view'})).polls[0].no,1000000);
    assert((await council('two','',{action:'vote',id:poll,choice:'yes'})).error);
    assert(!firstView.canClose&&!secondView.canClose,'ordinary accounts cannot close polls');
    assert((await council('one',registered.auth.token,{action:'close',id:poll})).error);
    const delegateState=inspector.call({op:'snapshot'}).state;
    assert(inspector.call({op:'global_patch',requestId:'council-delegate-test',
      globalRevision:delegateState.globalRevision,writes:[],patch:{globals:{councilManagers:{[id]:true}}}}).ok);
    for(const node of ['one','two']){
      const view=await council(node,registered.auth.token,{action:'view'});
      assert(view.canClose&&!view.admin,'council role is shared without game-admin privileges');
    }
    const closed=await council('two',signedIn.auth.token,{action:'close',id:poll});
    assert(!closed.error);assert(closed.polls[0].closed);assert.equal(closed.polls[0].result,'rejected');
    assert((await council('one',registered.auth.token,{action:'view'})).polls[0].closed);
    assert((await council('one',registered.auth.token,{action:'vote',id:poll,choice:'yes'})).error);
    two.destroy();await offline(id);
    const back=await connect('one');
    const resumed=await authenticate(back,wire.ClientMessage.ResumeSession,registered.auth.token);
    assert.equal(resumed.auth?.status,wire.AuthStatus.Ok,'original shared token resumes on one');
    assert.equal(resumed.profile?.stacks,registered.profile.stacks);
    assert.equal(resumed.profile?.xp,1000);
    if(remote)remote.noCache();
    assert(!fs.existsSync(path.join(home,'one','db','forbidden-local.json')));
    assert(!fs.existsSync(path.join(home,'two','db','forbidden-local.json')));
    back.destroy();await offline(id);
    // Seed through fenced authority transactions, then exercise the actual
    // two game adapters and their C++ freeze/apply/thaw loops.
    const bSocket=await connect('two');
    const bAuth=await authenticate(bSocket,wire.ClientMessage.Register,'hyperholder','test-password-456');
    assert.equal(bAuth.auth?.status,wire.AuthStatus.Ok);
    const bid=inspector.call({op:'snapshot'}).state.globals.users.hyperholder.id;
    bSocket.destroy();await offline(bid);
    const aLease=inspector.call({op:'acquire',requestId:'h-setup-a',account:id});
    const bLease=inspector.call({op:'acquire',requestId:'h-setup-b',account:bid});
    assert(aLease.ok&&bLease.ok);
    const aRecord=structuredClone(aLease.record),bRecord=structuredClone(bLease.record);
    aRecord.inventory.unique??={};aRecord.inventory.unique.petal_rose=20;
    bRecord.inventory.apex??={};bRecord.inventory.apex.petal_rose=1;
    bRecord.inventory.unique??={};bRecord.inventory.unique.petal_rose=2;
    const beforeH=inspector.call({op:'snapshot'}).state;
    const setup=inspector.call({op:'global_patch',requestId:'h-setup-global',globalRevision:beforeH.globalRevision,
      writes:[{account:id,record:aRecord,epoch:aLease.epoch,revision:aLease.revision},
        {account:bid,record:bRecord,epoch:bLease.epoch,revision:bLease.revision}],
      patch:{globals:{hyperForges:{rose:{owner:bid,claims:1,paid:5}}}}});assert(setup.ok,JSON.stringify(setup));
    for(const [account,record]of [[id,aRecord],[bid,bRecord]])assert(inspector.call({op:'release',requestId:'h-setup-release-'+account,
      account,record,epoch:setup.epochs[account],revision:setup.revisions[account]}).ok);
    const hOne=await connect('one'),hTwo=await connect('two');
    assert.equal((await authenticate(hOne,wire.ClientMessage.Login,'sharedplayer','test-password-123')).auth?.status,wire.AuthStatus.Ok);
    assert.equal((await authenticate(hTwo,wire.ClientMessage.Login,'hyperholder','test-password-456')).auth?.status,wire.AuthStatus.Ok);
    const hState=inspector.call({op:'snapshot'}).state;
    const trigger=new ShardRpc({endpoint,node:'one',boot:hState.accounts[id].boot,key:keys.one});
    const winnerMessages=wire.readMessages(hOne,5000),loserMessages=wire.readMessages(hTwo,5000);
    const begin=trigger.call({op:'hyper_begin',requestId:'actual-game-h-forge',account:id,action:'forge',input:'rose'});
    assert(begin.ok,JSON.stringify(begin));
    async function settled(transaction){
      for(let i=0;i<150;++i){
        const state=inspector.call({op:'snapshot'}).state;
        if(state.globals.hyperTransition?.id===transaction&&state.globals.hyperTransition.stage==='complete')return state;
        await sleep(100);
      }throw Error('real games failed to freeze/apply/thaw H transaction');
    }
    const hAfter=await settled(begin.transaction.id);console.log('cross-host H settlement reached');
    assert(hAfter.globals.hyperTransition.result.ok,JSON.stringify(hAfter.globals.hyperTransition.result));
    assert.equal(hAfter.globals.hyperForges.rose.owner,id);
    assert.equal(hAfter.accounts[id].record.inventory.unique.petal_rose,15);
    assert.equal(hAfter.accounts[bid].record.inventory.unique.petal_rose,6);
    assert.equal(hAfter.accounts[id].shard,'one');assert.equal(hAfter.accounts[bid].shard,'two');
    assert(wire.parseAuth(await winnerMessages).profile,'winner receives applied profile');
    assert(wire.parseAuth(await loserMessages).profile,'loser receives applied profile');
    const boardReplies=[wire.readMessages(hOne,800),wire.readMessages(hTwo,800)];
    const boardRequest=Buffer.alloc(6);boardRequest[0]=wire.ClientMessage.RequestLeaderboard;
    wire.send(hOne,boardRequest);wire.send(hTwo,boardRequest);
    const boards=(await Promise.all(boardReplies)).map(messages=>messages.find(b=>b[0]===wire.ServerMessage.Leaderboard));
    assert(boards[0]&&boards[1],'both shards answer central H leaderboard');assert.deepEqual(boards[0],boards[1]);
    wire.send(hOne,Buffer.concat([Buffer.from([wire.ClientMessage.Chat]),wire.encStr('/hyper-abandon rose confirm')]));
    let abandoned;
    for(let i=0;i<150;++i){
      const state=inspector.call({op:'snapshot'}).state;
      if(state.globals.hyperTransition?.id!==begin.transaction.id&&state.globals.hyperTransition?.stage==='complete'){abandoned=state;break;}
      await sleep(100);
    }
    assert(abandoned,'real client abandon completes');assert(abandoned.globals.hyperTransition.result.ok);
    assert.equal(abandoned.accounts[id].record.inventory.unique.petal_rose,15);
    assert.equal(abandoned.accounts[id].record.inventory.apex?.petal_rose||0,0);
    // Both processes simulate the same map independently. Each connection
    // receives its own spawned player but never the other shard's nameplate.
    const names=['ShardProbeOne','ShardProbeTwo'];
    const dimensions=Buffer.alloc(4);dimensions.writeUInt16LE(1280);dimensions.writeUInt16LE(720,2);
    const worlds=[wire.readMessages(hOne,6000),wire.readMessages(hTwo,6000)];
    for(const [i,socket]of [hOne,hTwo].entries())wire.send(socket,Buffer.concat([Buffer.from([wire.ClientMessage.JoinGame]),dimensions,wire.encStr('garden'),wire.encStr(names[i]),Buffer.from([0])]));
    const messages=await Promise.all(worlds);console.log('both independent worlds spawned');
    for(let i=0;i<2;i++){
      assert(messages[i].some(m=>m[0]===4),'world join accepted');
      assert(messages[i].some(m=>m[0]===5&&m.includes(Buffer.from(names[i]))),'own player in snapshots: '+JSON.stringify(messages[i].map(m=>({type:m[0],bytes:m.length,text:m.toString().match(/[A-Za-z]{5,}/g)?.slice(0,8)}))));
      assert(!messages[i].some(m=>m[0]===5&&m.includes(Buffer.from(names[1-i]))),'other shard player absent');
    }
    // Reproduce real play, then transfer the same account between stale world
    // caches. Lobby-only round trips never exercised the activity save path.
    async function active(socket,seconds=5) {
      let sequence=0;
      for(let i=0;i<seconds*5;i++) {
        const input=Buffer.alloc(18);input[0]=wire.ClientMessage.Input;
        input.writeUInt32LE(++sequence,1);input[10]=1;input.writeUInt16LE(1280,11);input.writeUInt16LE(720,13);input[17]=4;
        wire.send(socket,input);
        if(i%5===0)for(const port of Object.values(ports))assert((await fetch(`http://127.0.0.1:${port}/api/health`,{signal:AbortSignal.timeout(3000)})).ok);
        await sleep(200);
      }
    }
    const historical=structuredClone(inspector.call({op:'snapshot'}).state.globals.councilActivity||{});
    await Promise.all([active(hOne),active(hTwo)]);
    hOne.end();hTwo.end();await offline(id);await offline(bid);
    let previous=inspector.call({op:'snapshot'}).state.accounts[id].record.councilActiveSeconds||0;
    assert(previous>=3,'active play must persist account-scoped seconds');
    for(const node of Array.from({length:Number(process.env.SHARD_HANDOFF_ROUNDS||2)},(_,i)=>i%2?'one':'two')) {
      const s=await connect(node);assert.equal((await authenticate(s,wire.ClientMessage.Login,'sharedplayer','test-password-123')).auth?.status,wire.AuthStatus.Ok);
      wire.send(s,Buffer.concat([Buffer.from([wire.ClientMessage.JoinGame]),dimensions,wire.encStr('garden'),wire.encStr('ActivityRoundTrip'),Buffer.from([0])]));
      await sleep(1500);await active(s,Number(process.env.SHARD_PLAY_SECONDS||5));s.end();await offline(id);
      const current=inspector.call({op:'snapshot'}).state;
      assert(current.accounts[id].record.councilActiveSeconds>=previous+3,'handoff retains and accumulates fresh activity');
      previous=current.accounts[id].record.councilActiveSeconds;console.log('handoff progress',node,previous);
      assert.deepEqual(current.globals.councilActivity||{},historical,'historical table must not be rewritten');
      assert.equal(current.accounts[id].record.inventory.unique.petal_rose,15);
    }
    console.log('active-play round trip preserved time and inventory; both health endpoints responsive');
    // Simultaneous arrivals and departures exercise release while other saves
    // are still in flight; isolated synthetic accounts only.
    const churn=await Promise.all(Array.from({length:8},async(_,i)=>{
      const node=i%2?'two':'one',socket=await connect(node),name='churn'+i;
      const result=await authenticate(socket,wire.ClientMessage.Register,name,'churn-test-123');
      assert.equal(result.auth?.status,wire.AuthStatus.Ok,'concurrent register '+i);
      return {socket,name,node};
    }));
    for(let round=0;round<3;round++) {
      for(const entry of churn)wire.send(entry.socket,Buffer.concat([Buffer.from([wire.ClientMessage.JoinGame]),dimensions,wire.encStr('garden'),wire.encStr(entry.name),Buffer.from([0])]));
      await Promise.all(churn.map(async entry=>{
        await active(entry.socket,5);
        const frames=await wire.readMessages(entry.socket,500);
        assert(frames.some(m=>m[0]===wire.ServerMessage.Snapshot),'concurrent player receives world');
        entry.socket.end();
      }));
      const state=inspector.call({op:'snapshot'}).state;
      for(const entry of churn)await offline(state.globals.users[entry.name].id);
      if(round<2)await Promise.all(churn.map(async entry=>{
        entry.node=entry.node==='one'?'two':'one';entry.socket=await connect(entry.node);
        assert.equal((await authenticate(entry.socket,wire.ClientMessage.Login,entry.name,'churn-test-123')).auth?.status,wire.AuthStatus.Ok);
      }));
      console.log('concurrent churn round',round+1,'players',churn.length);
    }

    await trigger.close();
    const {checkPair}=require('./shards/ops/live-paired-check.cjs');
    await checkPair({protocolVersion:wire.ver,contentHash:wire.CONTENT_HASH},
      {username:'sharedplayer',password:'test-password-123'},inspector,ports);
    console.log('production acceptance replay: account round trip and council passed');
    if (process.env.SHARD_TEST_ANNOUNCEMENTS === '1') {
      await offline(id);
      const state = inspector.call({op:'snapshot'}).state;
      const grant = inspector.call({op:'global_patch',requestId:'announcement-admin-fixture',
        globalRevision:state.globalRevision,writes:[],
        patch:{users:{hyperholder:{...state.globals.users.hyperholder,admin:true}}}});
      assert(grant.ok, JSON.stringify(grant));
      const host = await connect('two'), remotePlayer = await connect('one'), otherMap = await connect('two');
      assert.equal((await authenticate(host,wire.ClientMessage.Login,'hyperholder','test-password-456')).auth?.status,wire.AuthStatus.Ok);
      assert.equal((await authenticate(remotePlayer,wire.ClientMessage.Login,'sharedplayer','test-password-123')).auth?.status,wire.AuthStatus.Ok);
      assert.equal((await authenticate(otherMap,wire.ClientMessage.Register,'announceprobe','test-password-789')).auth?.status,wire.AuthStatus.Ok);
      const recipients = [host, remotePlayer, otherMap];
      const joined = recipients.map(socket=>wire.readMessages(socket,3000));
      for (const [i,socket] of recipients.entries()) {
        wire.send(socket, Buffer.concat([Buffer.from([wire.ClientMessage.JoinGame]),dimensions,
          wire.encStr(i===2?'garden':'pvp'),wire.encStr('AnnouncementProbe'+i),Buffer.from([0])]));
      }
      for (const messages of await Promise.all(joined)) {
        assert(messages.some(message=>message[0]===wire.ServerMessage.Snapshot), 'announcement recipient joined');
      }
      const observed = recipients.map(socket=>wire.readMessages(socket,10500));
      wire.send(host,Buffer.concat([Buffer.from([wire.ClientMessage.Chat]),wire.encStr('/admin spawn spider unique')]));
      const transcripts = (await Promise.all(observed)).map(messages=>messages
        .filter(message=>message[0]===wire.ServerMessage.Chat)
        .map(message=>{
          const textAt=4+message.readUInt16LE(2);
          return message.toString('utf8',textAt+2,textAt+2+message.readUInt16LE(textAt));
        }));
      const chat = transcripts.map(lines=>lines.filter(line=>line.includes('A Eternal Spider has spawned'))
        .map(line=>line.replace(/^<b style="color: #[0-9a-f]+;">(.*)<\/b>$/,'$1')));
      assert.deepEqual(chat,[['A Eternal Spider has spawned!'],
        ['A Eternal Spider has spawned somewhere!'],['A Eternal Spider has spawned somewhere!']],
        'announcement transcripts: '+JSON.stringify(transcripts));
      console.log('boss announcement delivered once to each recipient, with location privacy');
      for (const socket of recipients) socket.end();
    }
    if(process.env.SHARD_UI_HOLD==='1') {
      console.log('SHARD_UI_READY pid='+process.pid);
      await new Promise(resolve=>{
        const finish=()=>{clearTimeout(timer);process.off('SIGUSR2',finish);resolve();};
        const timer=setTimeout(finish,600000);process.once('SIGUSR2',finish);
      });
    }
    for(let i=1;i<children.length;++i) {
      const child=children[i],done=once(child,'exit');child.kill('SIGTERM');
      const timer=setTimeout(()=>{if(child.exitCode===null)child.kill('SIGKILL');},16000);
      const [code,signal]=await done;clearTimeout(timer);
      assert.equal(signal,null,'game must finish orderly shutdown');assert.equal(code,0,'game save/release audit must pass');
      const log=fs.readFileSync(path.join(home,(i===1?'one':'two')+'.log'),'utf8');
      assert(log.includes('[authority_shutdown] {"saved":true,"released":true}'));
    }
  }catch(error){
    if(process.env.SHARD_RESULTS)for(const name of ['coordinator','one','two']){const file=path.join(home,name+'.log');if(fs.existsSync(file))fs.copyFileSync(file,path.join(process.env.SHARD_RESULTS,'remote-full-'+name+'.log'));}
    // Test-only logs contain synthetic tokens, never production credentials.
    for(const name of ['coordinator','one','two']){
      const file=path.join(home,name+'.log');if(fs.existsSync(file))console.error(name,fs.readFileSync(file,'utf8').slice(-2500));
    }
    throw error;
  }finally{
    for(const socket of sockets)socket.destroy();
    if(inspector)await inspector.close();
    for(const child of children.slice().reverse())if(child.exitCode===null){
      const done=once(child,'exit');child.kill('SIGTERM');
      const timer=setTimeout(()=>{if(child.exitCode===null)child.kill('SIGKILL');},16000);
      await done;clearTimeout(timer);
    }
    if(remote)await remote.close();
    for(const fd of logs)fs.closeSync(fd);
    assert.equal(path.dirname(path.resolve(home)),path.resolve(os.tmpdir()));
    assert(path.basename(home).startsWith('shards-shared-game-'));
    fs.rmSync(home,{recursive:true});
  }
});
