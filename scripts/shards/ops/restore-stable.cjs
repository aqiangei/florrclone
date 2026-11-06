'use strict';
// Explicit user-authorized exit from a failed, still-gated first migration.
// Export the CURRENT authority; never restore an older inventory snapshot.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),cp=require('node:child_process'),assert=require('node:assert/strict');
const {load}=require('./cluster-deploy.cjs'),{transport,quote:q}=require('./cluster-targets.cjs');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const node='/opt/florrclone/runtime/bin/node',gate='/var/www/florrclone-maintenance/enabled';
async function main(file,stable){
  const {config}=load(file);
  const currentCandidate=stable===config.release;
  assert(currentCandidate||stable==='/opt/florrclone/releases/cpp-20261105-council20-94');
  const protocol=currentCandidate?config.protocolVersion:94;
  const contentHash=currentCandidate?config.contentHash:2707239527;
  const remotes=Object.fromEntries(['one','two'].map(id=>[id,transport(config.targets[id])]));
  const token=crypto.randomBytes(16).toString('hex'),locked=[];
  const stamp=new Date().toISOString().replace(/[-:]/g,'').replace(/\.\d+Z$/,'Z');
  try{
    for(const [id,r]of Object.entries(remotes)){
      assert.equal(r.ssh('cat /etc/machine-id').trim(),config.targets[id].machineId);
      assert.equal(r.ssh('readlink -f /opt/florrclone/current').trim(),config.release);
      r.ssh('test -f '+gate+' && mkdir /var/lib/florrclone/deploy.lock && printf %s '+q(token)+' > /var/lib/florrclone/deploy.lock/owner');locked.push(id);
    }
    console.error('[restore] stopping both games, preserving latest authority');
    for(const r of Object.values(remotes)){
      r.ssh('systemctl stop florrclone');
      const state=r.ssh('systemctl show florrclone -p MainPID -p ActiveState -p SubState');
      assert(state.includes('MainPID=0')&&state.includes('ActiveState=inactive')&&state.includes('SubState=dead'),'Game has not stopped');
    }
    // The read-only checkpoint is taken AFTER the game processes are stopped.
    // It retains ownership, H, council, request receipts and every account.
    const output=cp.execFileSync(process.execPath,[path.resolve(__dirname,'../../backup-player-save.cjs'),config.backupRoot],{encoding:'utf8',windowsHide:true,timeout:180000});
    const backup=JSON.parse(output);assert.equal(backup.status,'verified');assert.equal(backup.source,'shared-account-authority');
    const local=backup.backup;assert.equal(path.dirname(path.resolve(local)),path.resolve(config.backupRoot));
    for(const f of backup.files)assert.equal(sha(fs.readFileSync(path.join(local,f.name))),f.sha256);
    const state=JSON.parse(fs.readFileSync(path.join(local,'authority.snapshot'),'utf8'));
    assert(Object.values(state.accounts).every(a=>a.phase==='offline'),'Account ownership remains live; refusing fallback');
    const inventory=JSON.parse(fs.readFileSync(path.join(local,'bootstrap.json'),'utf8'));
    const world=inventory.shardWorlds?.one;
    for(const key of ['bossClocks','sacrificeSpawn'])if(world?.[key]!==undefined)inventory[key]=world[key];
    delete inventory.shardWorlds;
    const exported=path.join(local,'inventory-stable.json');fs.writeFileSync(exported,JSON.stringify(inventory));
    assert.equal(Object.keys(inventory.players).length,backup.accounts);
    const r=remotes.one;
    console.error('[restore] verified stopped backup',local,'accounts',backup.accounts);
    r.ssh('systemctl stop florr-shard-coordinator && systemctl disable florr-shard-coordinator');
    remotes.two.ssh('systemctl disable florrclone && systemctl stop florr-shard-tunnel && systemctl disable florr-shard-tunnel');
    // Preserve the complete retired coordinator state and configs for diagnosis.
    for(const [id,remote]of Object.entries(remotes)){
      const retired='/etc/florrclone/retired-'+stamp;
      remote.ssh('install -d -m 700 '+q(retired));
      for(const name of id==='one'?['shard-game.json','shard-supervisor.json','shard-host.json','shard-backup.json']:['shard-game.json','shard-supervisor.json'])
        remote.ssh('if test -f /etc/florrclone/'+name+'; then mv /etc/florrclone/'+name+' '+q(retired+'/'+name)+'; fi');
    }
    r.ssh('test -d /var/lib/florrclone-shards && test ! -e /var/lib/florrclone-shards-retired-'+stamp+' && mv /var/lib/florrclone-shards /var/lib/florrclone-shards-retired-'+stamp);
    r.upload(exported,'/var/lib/florrclone/cpp-state/inventory.stable-next.json');
    const exportHash=sha(fs.readFileSync(exported));assert.equal(r.ssh('sha256sum /var/lib/florrclone/cpp-state/inventory.stable-next.json').split(/\s+/)[0],exportHash);
    r.ssh('chown florrclone:florrclone /var/lib/florrclone/cpp-state/inventory.stable-next.json && chmod 600 /var/lib/florrclone/cpp-state/inventory.stable-next.json && mv /var/lib/florrclone/cpp-state/inventory.stable-next.json /var/lib/florrclone/cpp-state/inventory.json');
    const unit=fs.readFileSync(path.join(__dirname,'florrclone-sharded.service'),'utf8')
      .replace(/^After=.*$/m,'After=network-online.target').replace(/^Environment=FLIX_.*\n/gm,'').replace(/^ExecStartPre=.*\n/gm,'');
    const unitFile=path.join(local,'florrclone-stable.service');fs.writeFileSync(unitFile,unit);r.upload(unitFile,'/etc/systemd/system/florrclone.service');
    r.ssh('if test -f /etc/systemd/system/florrclone.service.d/90-diagnostic.conf; then rm /etc/systemd/system/florrclone.service.d/90-diagnostic.conf; fi');
    r.ssh('test -s '+q(stable+'/server.wasm')+' && ln -s '+q(stable)+' /opt/florrclone/stable.next && mv -Tf /opt/florrclone/stable.next /opt/florrclone/current && systemctl daemon-reload && systemctl reset-failed florrclone && systemctl start florrclone');
    const {check}=require('./release-check.cjs');
    let verified=false,last;
    for(let i=0;i<60;i++){
      try{const result=JSON.parse(r.ssh(node+' '+q(config.release+'/release-check.cjs')+' http://127.0.0.1:8788 '+protocol+' '+contentHash));assert(result.ok);verified=true;break;}
      catch(e){last=e;await new Promise(resolve=>setTimeout(resolve,1000));}
    }
    if(!verified)throw last;
    r.ssh('rm '+gate);
    const health=await check('http://'+config.targets.one.host+':8088/',protocol,contentHash);
    const announcer=require('../../deploy-announcement.cjs'),admin=announcer.readConfig(config.adminFiles.one);
    const restoredAdmin={...admin,protocolVersion:protocol,contentHash};
    const client=await announcer.connect(restoredAdmin);client.close();
    fs.writeFileSync(config.adminFiles.one,JSON.stringify(restoredAdmin)+'\n',{mode:0o600});
    const result={status:currentCandidate?'deployed-single':'restored-stable',release:stable,protocolVersion:protocol,contentHash,backup:local,accounts:backup.accounts,exportHash,health,secondShard:'closed',retiredAuthority:'/var/lib/florrclone-shards-retired-'+stamp};
    fs.writeFileSync(path.join(local,'stable-restore-result.json'),JSON.stringify(result,null,2));return result;
  }catch(error){for(const r of Object.values(remotes))try{r.ssh('touch '+gate);}catch{}throw error;}
  finally{for(const id of locked.reverse())remotes[id].ssh('if test "$(cat /var/lib/florrclone/deploy.lock/owner)" = '+q(token)+'; then rm /var/lib/florrclone/deploy.lock/owner && rmdir /var/lib/florrclone/deploy.lock; fi');}
}
module.exports={main};
