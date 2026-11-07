'use strict';
// Invoked exclusively through scripts/deploy-with-backup.cjs --cluster CONFIG.
// CONFIG and its role credentials belong outside Git. No implicit target.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),cp=require('node:child_process'),assert=require('node:assert/strict');
const {validate,transport,inspect,quote:q}=require('./cluster-targets.cjs');
const {deploy}=require('./cluster-workflow.cjs'),{collect,stopped}=require('./stopped-backup.cjs');
const node='/opt/florrclone/runtime/bin/node',marker='/var/www/florrclone-maintenance/enabled';
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function acceptance(result){
  assert.equal(result?.ok,true,'Paired acceptance failed');
  for(const name of ['roundTrip','progress','exclusiveLogin','hyperUnique','council','independentWorlds'])
    assert.equal(result.checks?.[name],true,'Missing actual paired acceptance: '+name);
  return result;
}
function load(file){
  const c=validate(JSON.parse(fs.readFileSync(file,'utf8')));
  for(const name of ['manifest','privateDirectory','acceptanceScript'])assert(typeof c[name]==='string'&&path.isAbsolute(c[name]),'Absolute '+name+' required');
  assert.match(c.acceptanceSha256||'',/^[a-f0-9]{64}$/);
  assert.equal(sha(fs.readFileSync(c.acceptanceScript)),c.acceptanceSha256,'Unreviewed acceptance script');
  assert(Number.isInteger(c.protocolVersion)&&c.protocolVersion>0&&c.protocolVersion<=65535);
  assert(Number.isInteger(c.contentHash)&&c.contentHash>=0&&c.contentHash<=0xffffffff);
  const root=path.resolve(__dirname,'../../..'),relative=path.relative(root,path.resolve(c.privateDirectory));
  assert(relative.startsWith('..'+path.sep)||path.isAbsolute(relative),'Credential directory must stay outside repository');
  const m=JSON.parse(fs.readFileSync(c.manifest,'utf8'));
  const required=['shard_coordinator','shard-hyper-client.cjs','shard-global-merge.cjs','shard-coordinator-host.cjs','shard-rpc.cjs','shard-rpc-worker.cjs','shard-request-sequence.cjs','shard-directory.cjs','shard-supervisor.cjs','release-check.cjs','florrclone-sharded.service','florr-shard-coordinator.service','nginx-game.conf'];
  for(const name of required)assert(m.files?.[name],'Missing coordinated release artifact: '+name);
  return {config:c,manifest:m};
}
function create(config,manifest,connections){
  const stamp=new Date().toISOString().replace(/[-:]/g,'').replace(/\.\d+Z$/,'Z');
  const local=config.resumeBackup||path.join(path.resolve(config.backupRoot||'D:/ashflor/backups/florrclone'),stamp);
  fs.mkdirSync(local,{recursive:true,mode:0o700});
  const token=crypto.randomBytes(16).toString('hex'),held=[],results={};let installed=false,bonusStoppedAt=0;
  const roles=['one','two'];
  const initialSource=config.initialSource||'one';
  const standby=initialSource==='one'?'two':'one';
  const privateJSON=name=>JSON.parse(fs.readFileSync(path.join(config.privateDirectory,name),'utf8'));
  const writeJSON=(remote,destination,value,owner)=>{
    // Credentials go over encrypted stdin, never command arguments or output.
    const code="const fs=require('fs');const p=process.argv[1];const b=fs.readFileSync(0);JSON.parse(b);fs.writeFileSync(p+'.next',b,{mode:0o600});fs.chmodSync(p+'.next',0o600);fs.renameSync(p+'.next',p);";
    remote.ssh(node+' -e '+q(code)+' '+q(destination),JSON.stringify(value));
    remote.ssh('chown '+owner+' '+q(destination)+' && chmod 600 '+q(destination));
  };
  const executeCheck=(id,port)=>JSON.parse(connections[id].ssh(node+' '+q(config.release+'/release-check.cjs')+' '+q('http://127.0.0.1:'+port)+' '+config.protocolVersion+' '+config.contentHash));
  return {
    async preflight(){
      inspect(config,connections,manifest);
      if(config.mode==='initial')stopped(connections[standby],'florrclone',true);
      for(const id of roles){
        const remote=connections[id];
        assert.equal(remote.ssh(node+' --version').trim(),'v22.23.4','Unverified Node runtime');
        remote.ssh('nginx -t');
        if(!(id===standby&&config.mode==='initial'))assert(JSON.parse(remote.ssh('curl -fsS http://127.0.0.1:8788/api/health')).ok);
      }
      if(config.mode==='initial')connections.one.ssh('test ! -e /var/lib/florrclone-shards/authority.snapshot && test ! -e /var/lib/florrclone-shards/authority.journal');
      const host=privateJSON('shard-host.json');
      assert.equal(host.executable,'/opt/florrclone/current/shard_coordinator');assert.equal(host.stateBase,'/var/lib/florrclone-shards/authority');
      assert.equal(host.bootstrap,'/var/lib/florrclone-shards/bootstrap.json');assert.equal(host.port,19090);
      for(const id of roles){
        const game=privateJSON('shard-'+id+'.json'),supervisor=privateJSON('shard-supervisor-'+id+'.json');
        assert.equal(game.node,id);assert.equal(host.nodes[id].key,game.key);assert.equal(host.nodes[id].role,'game');
        assert.equal(supervisor.node,'supervisor-'+id);assert.equal(supervisor.shard,id);
        assert.equal(game.endpoint,'http://127.0.0.1:19090/rpc');assert.equal(supervisor.endpoint,game.endpoint);
        assert(!game.boot,'Each game restart must use a fresh boot identity');
        assert.equal(host.nodes[supervisor.node].scopeShard,id);assert.equal(host.nodes[supervisor.node].key,supervisor.key);
      }
      assert.equal(host.nodes.backup.role,'backup');assert.equal(host.nodes.backup.key,privateJSON('shard-backup.json').key);
      assert.equal(new Set(Object.values(host.nodes).map(n=>n.key)).size,Object.keys(host.nodes).length);
      for(const row of Object.values(host.nodes))assert.match(row.key,/^[a-f0-9]{64}$/);
      const admins=[];
      for(const id of config.mode==='initial'?[initialSource]:roles){
        assert(config.adminFiles?.[id]);const admin=require('../../deploy-announcement.cjs').readConfig(config.adminFiles[id]);
        const url=new URL(admin.url);assert.equal(url.hostname,config.targets[id].host);assert.equal(url.port,'8088');
        admins.push(admin.username.toLowerCase());
      }
      assert.equal(new Set(admins).size,admins.length,'Two-shard broadcasts need distinct administrator accounts to respect exclusive login');
    },
    async lock(){for(const id of roles){held.push(id);connections[id].ssh('mkdir /var/lib/florrclone/deploy.lock && printf %s '+q(token)+' > /var/lib/florrclone/deploy.lock/owner');}},
    async unlock(){for(const id of held.reverse())connections[id].ssh('if test -f /var/lib/florrclone/deploy.lock/owner && test "$(cat /var/lib/florrclone/deploy.lock/owner)" = '+q(token)+'; then rm /var/lib/florrclone/deploy.lock/owner && rmdir /var/lib/florrclone/deploy.lock; fi');},
    async announce(){
      if(config.maintenanceContinuation){
        assert.equal(config.mode,'initial','Maintenance continuation is only for initial migration');
        const prior=path.resolve(config.maintenanceContinuation.backupDirectory);
        assert.equal(path.dirname(prior),path.resolve(config.backupRoot),'Continuation backup outside backup root');
        const meta=JSON.parse(fs.readFileSync(path.join(prior,'backup-metadata.json'),'utf8'));
        assert.match(meta.timestamp,/^\d{8}T\d{6}Z$/);
        assert.equal(meta.nextRelease,config.targets.one.expectedRelease,'Continuation release changed');
        assert.equal(sha(fs.readFileSync(path.join(prior,'inventory.json'))),meta.sha256,'Continuation backup changed');
        assert.equal(connections.one.ssh('sha256sum '+q('/var/lib/florrclone/cutover-backups/'+meta.timestamp+'/inventory.json')).split(/\s+/)[0],meta.sha256);
        const log=fs.readFileSync(config.maintenanceContinuation.announcementLog,'utf8');
        for(const seconds of [120,60,30,10])assert(log.includes('已确认全服广播：'+seconds+'秒后维护'),'Missing confirmed announcement');
        connections.one.ssh('test -f '+marker);
        const bonus=JSON.parse(fs.readFileSync(path.join(prior,'maintenance-bonus.json'),'utf8'));
        assert(Number.isSafeInteger(bonus.resumedAt)&&bonus.resumedAt>0&&Date.now()-bonus.resumedAt<3600000,'Stale maintenance continuation');
        bonusStoppedAt=bonus.resumedAt;
        fs.writeFileSync(path.join(local,'continued-maintenance.json'),JSON.stringify({prior,sha256:meta.sha256,bonusStoppedAt}));
        console.log('Continuing the already-announced maintenance; public gate remains closed.');
        return;
      }
      const announcements=require('../../deploy-announcement.cjs'),clients=[];
      try{
        for(const id of config.mode==='initial'?[initialSource]:roles)clients.push(await announcements.connect(announcements.readConfig(config.adminFiles[id])));
        await announcements.countdown(async message=>{for(const client of clients)await client.broadcast(message);});
      }finally{for(const client of clients)client.close();}
    },
    async closeGates(){
      const notes=JSON.parse(fs.readFileSync(process.env.FLORR_RELEASE_NOTES_FILE||path.resolve(__dirname,'../../release-notes.json'),'utf8'));
      assert(Array.isArray(notes.changes)&&notes.changes.length);
      const escape=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
      const html=fs.readFileSync(path.resolve(__dirname,'../../maintenance.html'),'utf8').replace('<!-- RELEASE_NOTES -->','<section><h2>'+escape(notes.title)+'</h2><ul>'+notes.changes.map(c=>'<li>'+escape(c)+'</li>').join('')+'</ul></section>');
      const maintenance=path.join(local,'maintenance.html');fs.writeFileSync(maintenance,html);
      const errors=[];
      for(const id of roles)try{
        const remote=connections[id];remote.upload(maintenance,'/var/www/florrclone-maintenance/maintenance.next.html');
        remote.ssh('mv /var/www/florrclone-maintenance/maintenance.next.html /var/www/florrclone-maintenance/maintenance.html && touch '+marker);
        if(id===standby&&config.mode==='initial')remote.ssh('systemctl unmask nginx && systemctl start nginx');
        const page=remote.ssh('curl -sS http://127.0.0.1:8088/');assert(page.includes('花朵躲石头')&&page.includes(escape(notes.title)),'Maintenance page unavailable');
      }catch(error){errors.push(id+': '+error.message);}
      assert(!errors.length,errors.join('; '));
    },
    async stopGames(){
      if(!bonusStoppedAt)bonusStoppedAt=Number(connections.one.ssh('date +%s%3N').trim());assert(Number.isSafeInteger(bonusStoppedAt)&&bonusStoppedAt>0);
      fs.writeFileSync(path.join(local,'maintenance-start.json'),JSON.stringify({id:stamp,stoppedAt:bonusStoppedAt}));
      for(const id of roles){if(!(id===standby&&config.mode==='initial'))connections[id].ssh('systemctl stop florrclone');stopped(connections[id],'florrclone',id===standby&&config.mode==='initial');}
    },
    async stopCoordinator(){if(config.mode==='upgrade')connections.one.ssh('systemctl stop florr-shard-coordinator');stopped(connections.one,'florr-shard-coordinator',config.mode==='initial');},
    async backup(){return collect({mode:config.mode,initialSource,stamp,directory:local,...connections});},
    async install(backup){
      assert(fs.existsSync(path.join(local,'verified-backup.json'))&&backup.files.length);
      const primary=connections.one;
      primary.ssh('id florr-coordinator >/dev/null 2>&1 || useradd --system --home /var/lib/florrclone-shards --shell /usr/sbin/nologin florr-coordinator');
      primary.ssh('install -d -o florr-coordinator -g florr-coordinator -m 700 /var/lib/florrclone-shards');
      if(config.mode==='initial'){
        const saved=path.join(local,'inventory.json');assert.equal(sha(fs.readFileSync(saved)),backup.files.find(f=>f.name==='inventory.json').sha256);
        let bootstrap=saved;
        if(config.councilManagerIds?.length){
          const {prepareCouncilManagers}=require('../../prepare-council-managers.cjs');
          const next=prepareCouncilManagers(JSON.parse(fs.readFileSync(saved,'utf8')),config.councilManagerIds);
          bootstrap=path.join(local,'inventory-council-delegated.json');
          fs.writeFileSync(bootstrap,JSON.stringify(next));
          fs.writeFileSync(path.join(local,'council-delegation.json'),JSON.stringify({accountIds:config.councilManagerIds,originalSha256:sha(fs.readFileSync(saved)),preparedSha256:sha(fs.readFileSync(bootstrap))}));
        }
        if(config.authorizedPetalGrantFile){
          assert(path.isAbsolute(config.authorizedPetalGrantFile));
          const inventory=JSON.parse(fs.readFileSync(bootstrap,'utf8'));
          const grants=require('../../grant-authorized-petals.cjs').apply(inventory,JSON.parse(fs.readFileSync(config.authorizedPetalGrantFile,'utf8')));
          bootstrap=path.join(local,'inventory-authorized-grants.json');
          fs.writeFileSync(bootstrap,JSON.stringify(inventory));
          fs.writeFileSync(path.join(local,'authorized-petals.json'),JSON.stringify(grants,null,2));
        }
        primary.upload(bootstrap,'/var/lib/florrclone-shards/bootstrap.next.json');
        assert.equal(primary.ssh('sha256sum /var/lib/florrclone-shards/bootstrap.next.json').split(/\s+/)[0],sha(fs.readFileSync(bootstrap)));
        primary.ssh('test ! -e /var/lib/florrclone-shards/bootstrap.json && chown florr-coordinator:florr-coordinator /var/lib/florrclone-shards/bootstrap.next.json && chmod 600 /var/lib/florrclone-shards/bootstrap.next.json && mv /var/lib/florrclone-shards/bootstrap.next.json /var/lib/florrclone-shards/bootstrap.json');
      }
      for(const id of roles){
        const remote=connections[id];remote.ssh('install -d -o root -g root -m 755 /etc/florrclone');
        const game=privateJSON('shard-'+id+'.json');game.shards=roles.map(role=>({id:role,label:role==='one'?'一服':'二服',host:config.targets[role].host,port:config.targets[role].publicPort,available:true}));
        writeJSON(remote,'/etc/florrclone/shard-game.json',game,'florrclone:florrclone');
        writeJSON(remote,'/etc/florrclone/shard-supervisor.json',privateJSON('shard-supervisor-'+id+'.json'),'root:root');
        remote.ssh('install -m 644 '+q(config.release+'/florrclone-sharded.service')+' /etc/systemd/system/florrclone.service');
        if(id==='two'&&config.mode==='initial')remote.ssh('if test -L /etc/nginx/sites-enabled/default; then test "$(readlink -f /etc/nginx/sites-enabled/default)" = /etc/nginx/sites-available/default && unlink /etc/nginx/sites-enabled/default; fi');
        if(manifest.files['download-queue.cjs']){
          const {configure,downloadQueueUnit}=require('./client-assets.cjs');
          const assetHost=config.targets[id].host;
          const configFile=path.join(local,'nginx-'+id+'.conf');
          const queueFile=path.join(local,'download-queue-'+id+'.service');
          fs.writeFileSync(configFile,configure(fs.readFileSync(path.join(__dirname,'nginx-game.conf'),'utf8'),
            {release:config.release,assetHost,downloadQueue:true,assetHashes:manifest.files}));
          fs.writeFileSync(queueFile,downloadQueueUnit(config.release,assetHost));
          remote.upload(configFile,'/etc/nginx/conf.d/florrclone.conf');
          remote.upload(queueFile,'/etc/systemd/system/florr-download-queue.service');
          remote.ssh('nginx -t && systemctl daemon-reload && systemctl enable florr-download-queue && systemctl restart florr-download-queue');
          for(const asset of ['index.html','bundle.js','bundle.wasm','bundle.js.bin','bundle.wasm.bin'])
            remote.ssh('runuser -u www-data -- test -r '+q(config.release+'/'+asset));
        }else{
          remote.ssh('install -m 644 '+q(config.release+'/nginx-game.conf')+' /etc/nginx/conf.d/florrclone.conf && nginx -t');
        }
        remote.ssh('test ! -e /opt/florrclone/current.next && ln -s '+q(config.release)+' /opt/florrclone/current.next && mv -Tf /opt/florrclone/current.next /opt/florrclone/current');
      }
      writeJSON(primary,'/etc/florrclone/shard-host.json',privateJSON('shard-host.json'),'florr-coordinator:florr-coordinator');
      writeJSON(primary,'/etc/florrclone/shard-backup.json',privateJSON('shard-backup.json'),'root:root');
      primary.ssh('install -m 644 '+q(config.release+'/florr-shard-coordinator.service')+' /etc/systemd/system/florr-shard-coordinator.service');
      for(const id of roles)connections[id].ssh('systemctl daemon-reload && systemctl reload nginx');
      assert(bonusStoppedAt>0);
      stopped(primary,'florr-shard-coordinator',config.mode==='initial');
      const bonus=JSON.parse(primary.ssh('runuser -u florr-coordinator -- '+q(config.release+'/shard_coordinator')+' /var/lib/florrclone-shards/authority /var/lib/florrclone-shards/bootstrap.json --pause-bonus '+q(stamp)+' '+bonusStoppedAt));
      assert.equal(bonus.ok,true);fs.writeFileSync(path.join(local,'maintenance-bonus.json'),JSON.stringify(bonus,null,2));
      installed=true;
    },
    async startCoordinator(){
      assert(installed);connections.one.ssh('systemctl enable --now florr-shard-coordinator');
      connections.two.ssh('systemctl enable --now florr-shard-tunnel');
      for(const id of roles){
        let last;
        for(let attempt=0;attempt<8;attempt++){
          try{const result=JSON.parse(connections[id].ssh(node+' '+q(config.release+'/shard-supervisor.cjs')+' --probe'));assert.equal(result.event,'coordinator-ready');last=null;break;}
          catch(error){last=error;await pause(1000);}
        }
        if(last)throw last;
      }
    },
    async recoverStoppedOwners(){
      // The root pre-start hook performs the same proof again immediately before
      // each game starts. A failed RPC leaves both gates closed, never steals.
      for(const id of roles){stopped(connections[id],'florrclone');connections[id].ssh(node+' '+q(config.release+'/shard-supervisor.cjs'));}
    },
    async startGames(){for(const id of roles){
      connections[id].ssh('systemctl enable --now florrclone');
      // Bootstrap reads the complete save. Admit the second bootstrap only
      // after the first game is ready, as in the real-host integration test.
      let last;for(let i=0;i<60;i++){try{executeCheck(id,8788);last=null;break;}catch(error){last=error;await pause(1000);}}if(last)throw last;
    }},
    async verifyPrivate(){
      for(const id of roles){let last;for(let i=0;i<45;i++){try{results[id]=executeCheck(id,8788);last=null;break;}catch(error){last=error;await pause(1000);}}if(last)throw last;}
      if(config.skipPairedAcceptance === true){
        assert.equal(config.skipPairedAcceptanceReason,'user-authorized-20261106');
        results.paired={status:'skipped',reason:config.skipPairedAcceptanceReason,
          checksSkipped:['crossHostConnection','roundTrip','saveConsistency','sustainedLoad']};
        fs.writeFileSync(path.join(local,'private-acceptance.json'),JSON.stringify(results,null,2)+'\n');
        return;
      }
      assert.equal(sha(fs.readFileSync(config.acceptanceScript)),config.acceptanceSha256,'Acceptance script changed during deployment');
      const output=cp.execFileSync(process.execPath,[config.acceptanceScript,...(config.acceptanceArgs||[])],{encoding:'utf8',windowsHide:true,timeout:300000,maxBuffer:1024*1024});
      results.paired=acceptance(JSON.parse(output));
      fs.writeFileSync(path.join(local,'private-acceptance.json'),JSON.stringify(results,null,2)+'\n');
    },
    async openGates(){for(const id of roles)connections[id].ssh('rm '+marker);},
    async verifyPublic(){for(const id of roles){results[id].public=await require('./release-check.cjs').check('http://'+config.targets[id].host+':8088/',config.protocolVersion,config.contentHash);}},
    async recordSuccess(backup){
      fs.writeFileSync(path.join(local,'deployment-result.json'),JSON.stringify({status:'deployed',release:config.release,targets:roles.map(id=>({id,host:config.targets[id].host})),backup,results},null,2)+'\n',{mode:0o600});
      for(const id of Object.keys(config.adminFiles)){
        const file=config.adminFiles[id],admin=JSON.parse(fs.readFileSync(file,'utf8'));
        fs.writeFileSync(file,JSON.stringify({...admin,protocolVersion:config.protocolVersion,contentHash:config.contentHash})+'\n',{mode:0o600});
      }
    }
  };
}
async function main(file){const {config,manifest}=load(file);const connections=Object.fromEntries(['one','two'].map(id=>[id,transport(config.targets[id])]));return deploy(create(config,manifest,connections));}
async function resumeAfterInstall(file,backupDirectory){
  const {config,manifest}=load(file);
  const local=path.resolve(backupDirectory),backupRoot=path.resolve(config.backupRoot);
  assert(path.isAbsolute(backupDirectory)&&path.dirname(local)===backupRoot,'Existing backup under configured root required');
  const backup=JSON.parse(fs.readFileSync(path.join(local,'verified-backup.json'),'utf8'));
  assert(backup.files?.length,'Verified stopped backup required');
  for(const f of backup.files){assert(/^[a-zA-Z0-9_.-]+$/.test(f.name));assert.equal(sha(fs.readFileSync(path.join(local,f.name))),f.sha256);}
  const connections=Object.fromEntries(['one','two'].map(id=>[id,transport(config.targets[id])]));
  const installed={...config,initialSource:undefined,mode:'upgrade',targets:Object.fromEntries(['one','two'].map(id=>[id,{...config.targets[id],expectedRelease:config.release}]))};
  const verify=()=>{inspect(installed,connections,manifest);for(const id of ['one','two'])connections[id].ssh('test -f '+marker);assert.equal(connections.one.ssh('systemctl is-active florr-shard-coordinator').trim(),'active');};
  verify();const ops=create({...config,resumeBackup:local},manifest,connections);let locked=false;
  try{locked=true;await ops.lock();verify();
    console.error('[deploy] resuming installed release, sequential game startup');await ops.startGames();
    console.error('[deploy] verifying private paired acceptance');await ops.verifyPrivate();
    await ops.openGates();await ops.verifyPublic();await ops.recordSuccess(backup);return {status:'deployed',resumed:true,backup};
  }catch(error){await ops.closeGates();throw error;}finally{if(locked)await ops.unlock();}
}
module.exports={load,create,acceptance,main,resumeAfterInstall};
