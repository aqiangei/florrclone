'use strict';
if(process.argv[2]==='--restore-stable') {
  require('./shards/ops/restore-stable.cjs').main(process.argv[3],process.argv[4]).then(result=>console.log(JSON.stringify(result)))
    .catch(error=>{console.error(error.message);process.exitCode=1;});
} else if(process.argv[2]==='--cluster') {
  const cluster=require('./shards/ops/cluster-deploy.cjs');
  (process.argv[4]==='--resume-after-install'?cluster.resumeAfterInstall(process.argv[3],process.argv[5]):cluster.main(process.argv[3])).then(result=>console.log(JSON.stringify(result)))
    .catch(error=>{console.error(JSON.stringify({error:error.message,deployment:error.deployment}));process.exitCode=1;});
} else {
// Run on the owner's Windows computer. A stopped snapshot must reach this
// computer and pass SHA256 verification before any live files are replaced.
// Usage: node scripts/deploy-with-backup.cjs RELEASE_DIRECTORY [BACKUP_ROOT]
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const cp=require('node:child_process'),assert=require('node:assert/strict');
const release=process.argv[2];
if(!/^\/opt\/florrclone\/releases\/[a-zA-Z0-9_-]+$/.test(release||''))
  throw Error('Supply a staged /opt/florrclone/releases/RELEASE directory');
const root=path.resolve(__dirname,'..');
const backupRoot=path.resolve(process.argv[3]||path.join(root,'../backups/florrclone'));
const stamp=new Date().toISOString().replace(/[-:]/g,'').replace(/\.\d+Z$/,'Z');
const local=path.join(backupRoot,stamp);fs.mkdirSync(local,{recursive:true});
const remote='/var/lib/florrclone/cutover-backups/'+stamp;
const key=path.join(process.env.USERPROFILE||process.env.HOME,'.ssh/ASHFLOR.pem');
const host='root@154.219.105.161',marker='/var/www/florrclone-maintenance/enabled';
const run=(exe,args)=>{
  for(let attempt=0;;attempt++)try{return cp.execFileSync(exe,args,{encoding:'utf8',maxBuffer:1024*1024});}
  catch(error){
    // Retry only failures before SSH authentication/command execution. Never
    // replay a command after an uncertain remote execution or a command error.
    const preConnect=/^(?:ssh: connect to host .*: (?:Connection timed out|No route to host)|Connection timed out during banner exchange)\r?$/m.test(String(error.stderr||''));
    if(!['ssh','scp'].includes(exe)||!preConnect||attempt>=3)throw error;
  }
};
const sshOptions=['-i',key,'-o','BatchMode=yes','-o','ConnectTimeout=15'];
if(process.env.FLORR_DEPLOY_SSH_PROXY)sshOptions.push('-o','ProxyCommand='+process.env.FLORR_DEPLOY_SSH_PROXY);
const ssh=s=>run('ssh',[...sshOptions,host,s]);
const scp=(a,b)=>run('scp',[...sshOptions,a,b]);
const q=s=>"'"+s.replace(/'/g,"'\\''")+"'";
let locked=false,stopped=false,switched=false,opened=false,old='',bonusStoppedAt=0,bonusHelper='';
function preserveMaintenanceBonus(){
  assert(bonusHelper&&bonusStoppedAt>0,'Maintenance bonus preparation missing');
  const state=ssh('systemctl show florrclone -p MainPID -p ActiveState -p SubState');
  assert(state.includes('MainPID=0')&&state.includes('ActiveState=inactive')&&state.includes('SubState=dead'),'Bonus adjustment requires a stopped game');
  const result=JSON.parse(ssh('/opt/florrclone/runtime/bin/node '+q(bonusHelper)+' /var/lib/florrclone/cpp-state/inventory.json '+q(stamp)+' '+bonusStoppedAt));
  fs.writeFileSync(path.join(local,'maintenance-bonus.json'),JSON.stringify(result,null,2));
  return result;
}
async function main(){
  const announcements=require("./deploy-announcement.cjs");
  const noticeConfig=announcements.readConfig(process.env.FLORR_DEPLOY_ADMIN_FILE);
  const bootstrap=process.env.FLORR_BOOTSTRAP_DEPLOY_ADMIN==='1';
  try{
    // Once shared authority is installed, the one-host path may neither replace
    // its local cache nor silently roll one server back to independent storage.
    assert(ssh('if test -e /etc/florrclone/shard-game.json || test -e /var/lib/florrclone-shards/authority.snapshot; then echo SHARDED; else echo LEGACY; fi').trim()==='LEGACY',
      'Shared-account servers require deploy-with-backup.cjs --cluster CONFIG');
    ssh('mkdir /var/lib/florrclone/deploy.lock');locked=true;
    old=ssh('readlink -f /opt/florrclone/current').trim();
    assert.match(old,/^\/opt\/florrclone\/releases\/[a-zA-Z0-9_-]+$/);
    if(process.env.FLORR_EXPECTED_RELEASE)assert.equal(old,process.env.FLORR_EXPECTED_RELEASE,'Online release changed; coordinate before deploying');
    const releaseProbe=release+'/release-hello.cjs';
    const probe=ssh('if test -s '+q(releaseProbe)+'; then echo '+q(releaseProbe)+'; else echo /var/lib/florrclone/release-hello.cjs; fi').trim();
    ssh('test -s '+q(release+'/server.wasm')+' && test -s '+q(release+'/bundle.wasm')+' && test -s '+q(release+'/deploy-runtime.cjs'));
    ssh('curl -fsS http://127.0.0.1:8788/api/health');
    ssh('install -d -m 700 '+q(remote));
    // Publish this release's Chinese notes before enabling maintenance.
    const notes=JSON.parse(fs.readFileSync(process.env.FLORR_RELEASE_NOTES_FILE||path.join(__dirname,'release-notes.json'),'utf8'));
    assert(Array.isArray(notes.changes)&&notes.changes.length>0,'Release notes are required');
    const escape=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
    const html=fs.readFileSync(path.join(__dirname,'maintenance.html'),'utf8').replace('<!-- RELEASE_NOTES -->',
      '<section style="text-align:left;font-size:14px"><h2>'+escape(notes.title)+'</h2><ul>'+notes.changes.map(t=>'<li>'+escape(t)+'</li>').join('')+'</ul></section>');
    const waitingFile=path.join(local,'maintenance.html');fs.writeFileSync(waitingFile,html);
    scp(waitingFile,host+':/var/www/florrclone-maintenance/maintenance.next.html');
    ssh('mv /var/www/florrclone-maintenance/maintenance.next.html /var/www/florrclone-maintenance/maintenance.html');
    // Announce while players are still connected, before the maintenance gate.
    // Only an echoed Admin-channel broadcast counts as delivery confirmation.
    if (!bootstrap) {
      const announcer=await announcements.connect(noticeConfig);
      try { await announcements.countdown(message=>announcer.broadcast(message)); }
      finally { announcer.close(); }
    } else {
      assert(process.env.FLORR_PRESERVE_SAVE==='1', 'Operator bootstrap requires preserving every existing account');
      console.log('首次配置广播管理员：本次使用维护等待页，下次起全服广播倒计时。');
    }
    // Serve the waiting page before gracefully stopping the old process.
    ssh('touch '+q(marker));
    const waiting=ssh('curl -sS http://127.0.0.1:8088/');
    assert(waiting.includes('花朵躲石头'),'Waiting page must work before stopping');
    assert(waiting.includes(escape(notes.title)) && waiting.includes(escape(notes.changes[0])),'Current release notes must be visible before stopping');
    if (bootstrap) await new Promise(resolve=>setTimeout(resolve,60000));
    bonusStoppedAt=Number(ssh('date +%s%3N').trim());
    assert(Number.isSafeInteger(bonusStoppedAt)&&bonusStoppedAt>0);
    stopped=true;ssh('systemctl stop florrclone');
    ssh('cp /var/lib/florrclone/cpp-state/inventory.json '+q(remote+'/inventory.json')+' && chmod 600 '+q(remote+'/inventory.json'));
    scp(host+':'+remote+'/inventory.json',path.join(local,'inventory.json'));
    const bytes=fs.readFileSync(path.join(local,'inventory.json'));
    const hash=crypto.createHash('sha256').update(bytes).digest('hex');
    assert.equal(ssh('sha256sum '+q(remote+'/inventory.json')).split(/\s/)[0],hash);
    const before=JSON.parse(bytes);
    const metadata={timestamp:stamp,previousRelease:old,nextRelease:release,sha256:hash,
      accounts:Object.keys(before.players||{}).length,remoteBackup:remote};
    fs.writeFileSync(path.join(local,'backup-metadata.json'),JSON.stringify(metadata,null,2));
    console.log('已验证本地最新存档备份：'+path.join(local,'inventory.json'));
    const uploadedBonusHelper=remote+'/maintenance-drop-bonus.cjs';
    const helper=path.join(__dirname,'maintenance-drop-bonus.cjs');
    scp(helper,host+':'+uploadedBonusHelper);
    assert.equal(ssh('sha256sum '+q(uploadedBonusHelper)).split(/\s/)[0],crypto.createHash('sha256').update(fs.readFileSync(helper)).digest('hex'));
    bonusHelper=uploadedBonusHelper;
    // Preserve the original migration snapshot's fixed TP debt without
    // changing passwords, progress or inventory earned since the cutover.
    let next,report;
    if(process.env.FLORR_PRESERVE_SAVE==='1'){
      assert(!process.env.FLORR_RESTORE_SNAPSHOT &&
        !process.env.FLORR_OPERATOR_HYPER_USER && process.env.FLORR_REFUND_ETERNAL_PRICE!=='1',
        'Preserve-save deployment cannot include account maintenance');
      next=path.join(local,'inventory.json');
      report={preservedSave:true};
      if(process.env.FLORR_PASSWORD_RESET_FILE){
        const reset=path.join(local,'inventory.password-reset.json');
        report.passwordReset=JSON.parse(run(process.execPath,[path.join(__dirname,'reset-password-offline.cjs'),next,reset,process.env.FLORR_PASSWORD_RESET_FILE]));
        next=reset;
      }
      if (bootstrap) {
        next=path.join(local,'inventory.operator.json');
        report=JSON.parse(run(process.execPath,[path.join(__dirname,'prepare-deploy-operator.cjs'),path.join(local,'inventory.json'),process.env.FLORR_DEPLOY_ADMIN_FILE,next]));
      }
    }else if(process.env.FLORR_RESTORE_SNAPSHOT){
      const source=path.resolve(process.env.FLORR_RESTORE_SNAPSHOT);
      const bytes=fs.readFileSync(source);
      const expected=process.env.FLORR_RESTORE_SHA256;
      assert(expected && /^[a-f0-9]{64}$/.test(expected),'Explicit restore SHA256 required');
      assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'),expected,'Restore snapshot hash mismatch');
      const restored=JSON.parse(bytes);assert(restored.users && restored.players,'Invalid restore database');
      next=path.join(local,'inventory.restored.json');fs.writeFileSync(next,bytes);
      report={restoreSnapshot:{source,sha256:expected,accounts:Object.keys(restored.players).length},accountAdjustments:[]};
      fs.writeFileSync(path.join(local,'snapshot-restore.json'),JSON.stringify(report.restoreSnapshot,null,2));
    }else{
    const baseline=path.join(root,'migrate/latest-final-cpp.json');
    next=path.join(local,'inventory.reconciled.json');
    report=JSON.parse(run(process.execPath,[path.join(__dirname,'reconcile-migrated-tp.js'),baseline,path.join(local,'inventory.json'),next]));
    const compare=JSON.parse(fs.readFileSync(next));
    for(const [id,p]of Object.entries(compare.players||{}))for(const k of ['talentPointBonus','talentPointReconciledAt']){
      if(k in before.players[id])p[k]=before.players[id][k];else delete p[k];
    }
    assert.deepEqual(compare,before);
    fs.writeFileSync(path.join(local,'tp-reconciliation.json'),JSON.stringify(report,null,2));
    // Explicitly requested releases, idempotent per account. This is AFTER
    // the stopped-save download/SHA gate and before the candidate is installed.
    const adjustments=path.join(root,'scripts/deferred-account-adjustments.json');
    if(fs.existsSync(adjustments)){
      const adjusted=path.join(local,'inventory.adjusted.json');
      report.accountAdjustments=JSON.parse(run(process.execPath,[path.join(__dirname,'release-selected-hypers.cjs'),next,adjusted,adjustments]));
      fs.writeFileSync(path.join(local,'account-adjustments.json'),JSON.stringify(report.accountAdjustments,null,2));
      next=adjusted;
    }
    const grantDb=JSON.parse(fs.readFileSync(next,'utf8'));
    report.requestedPetals=require('./grant-requested-petals.cjs').apply(grantDb);
    const granted=path.join(local,'inventory.granted.json');
    fs.writeFileSync(granted,JSON.stringify(grantDb));
    fs.writeFileSync(path.join(local,'requested-petals.json'),JSON.stringify(report.requestedPetals,null,2));
    next=granted;
    const shopManifest=path.join(__dirname,'manifests/shop-locks-20261104.json');
    if(fs.existsSync(shopManifest)){
      const restored=path.join(local,'inventory.shop-restored.json');
      report.shopLockRestore=JSON.parse(run(process.execPath,[path.join(__dirname,'restore-original-shop-locks.cjs'),'--input',next,'--output',restored,'--manifest',shopManifest]));
      fs.writeFileSync(path.join(local,'shop-lock-restore.json'),JSON.stringify(report.shopLockRestore,null,2));
      next=restored;
    }
    if(process.env.FLORR_REFUND_ETERNAL_PRICE==='1'){
      const refunded=path.join(local,'inventory.refunded.json');
      report.eternalRefund=JSON.parse(run(process.execPath,[path.join(__dirname,'refund-eternal-forging.cjs'),next,refunded]));
      fs.writeFileSync(path.join(local,'eternal-refund.json'),JSON.stringify(report.eternalRefund,null,2));
      next=refunded;
    }
    if(process.env.FLORR_PASSWORD_RESET_FILE){
      const reset=path.join(local,'inventory.password-reset.json');
      report.passwordReset=JSON.parse(run(process.execPath,[path.join(__dirname,'reset-password-offline.cjs'),next,reset,process.env.FLORR_PASSWORD_RESET_FILE]));
      fs.writeFileSync(path.join(local,'password-reset.json'),JSON.stringify(report.passwordReset,null,2));
      next=reset;
    }
    if(process.env.FLORR_OPERATOR_HYPER_USER){
      const queued=path.join(local,'inventory.operator.json');
      // Only the database directory is mounted into the WASM filesystem.
      const ready='/var/lib/florrclone/cpp-state/operator-ready-'+stamp;
      report.operatorRequest=JSON.parse(run(process.execPath,[path.join(__dirname,'prepare-operator-hyper-forge.cjs'),next,queued,process.env.FLORR_OPERATOR_HYPER_USER,'hammer-'+stamp,marker,ready]));
      fs.writeFileSync(path.join(local,'operator-request.json'),JSON.stringify(report.operatorRequest,null,2));
      next=queued;
    }
    }
    if(process.env.FLORR_COUNCIL_HISTORY_FILE){
      assert(process.env.FLORR_PRESERVE_SAVE==='1','History import requires preserve-save mode');
      const saved=JSON.parse(fs.readFileSync(next,'utf8'));
      const seed=JSON.parse(fs.readFileSync(process.env.FLORR_COUNCIL_HISTORY_FILE,'utf8'));
      report.councilHistory=require('./merge-council-history.cjs').apply(saved,seed);
      const historyFile=path.join(local,'inventory.history.json');fs.writeFileSync(historyFile,JSON.stringify(saved));next=historyFile;
    }
    scp(next,host+':'+remote+'/inventory.next.json');
    // All validation and the local backup gate have completed at this point.
    ssh('install -o florrclone -g florrclone -m 600 '+q(remote+'/inventory.next.json')+' /var/lib/florrclone/cpp-state/inventory.next.json');
    ssh('mv /var/lib/florrclone/cpp-state/inventory.next.json /var/lib/florrclone/cpp-state/inventory.json');
    ssh('ln -s '+q(release)+' /opt/florrclone/current.next && mv -Tf /opt/florrclone/current.next /opt/florrclone/current');switched=true;
    report.maintenanceBonus=preserveMaintenanceBonus();
    ssh('systemctl start florrclone');
    let healthy=false;
    for(let i=0;i<40;i++){
      try{const h=JSON.parse(ssh('curl -fsS http://127.0.0.1:8788/api/health'));if(h.ok){healthy=true;break;}}catch{}
      await new Promise(r=>setTimeout(r,1000));
    }
    assert(healthy,'New service failed health check');
    for(const file of ['','bundle.js','bundle.wasm','bundle.js.bin','bundle.wasm.bin'])
      ssh('curl -fsS -o /dev/null '+q('http://127.0.0.1:8788/'+file));
    ssh('PORT=8788 /opt/florrclone/runtime/bin/node '+q(probe));
    if(process.env.FLORR_REQUIRE_COUNCIL==='1') {
      ssh('test -s '+q(release+'/council.html')+' && curl -fsS -o /dev/null http://127.0.0.1:8788/council.html');
      const check="(async()=>{const r=await fetch('http://127.0.0.1:8788/api/council',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:'view'})});if(!r.ok)throw Error('Council HTTP '+r.status);const j=await r.json();if(j.error||!Array.isArray(j.players)||!Array.isArray(j.polls))throw Error('Invalid council state');if(j.players.length&&j.players.reduce((s,p)=>s+p.units,0)!==1000000)throw Error('Invalid council weights');console.log('PASS council API and exact weight total');})().catch(e=>{console.error(e.message);process.exitCode=1});";
      console.log(ssh('/opt/florrclone/runtime/bin/node -e '+q(check)));
    }
    if (bootstrap) {
      const newHash=Number(process.env.FLORR_NEXT_CONTENT_HASH);
      assert(Number.isInteger(newHash) && newHash>=0 && newHash<=0xffffffff, 'Candidate content hash required for operator validation');
      const candidate={...noticeConfig,contentHash:newHash,url:'ws://127.0.0.1:8788/ws'};
      const verify=`const fs=require('fs');(async()=>{const a=await require(${JSON.stringify(release+'/deploy-announcement.cjs')}).connect(JSON.parse(fs.readFileSync(0,'utf8')));try{await a.broadcast('维护更新验证完成：花粉伤害、数量和冷却保持不变。');console.log('PASS operator Admin broadcast');}finally{a.close();}})().catch(e=>{console.error(e.message);process.exitCode=1});`;
      console.log(cp.execFileSync('ssh',['-i',key,host,'/opt/florrclone/runtime/bin/node -e '+q(verify)],{input:JSON.stringify(candidate),encoding:'utf8',timeout:30000}));
      fs.writeFileSync(process.env.FLORR_DEPLOY_ADMIN_FILE,JSON.stringify({...noticeConfig,contentHash:newHash}));
    }
    ssh('rm '+q(marker));opened=true;
    ssh('PORT=8088 /opt/florrclone/runtime/bin/node '+q(probe));
    if(report.operatorRequest)ssh('touch '+q(report.operatorRequest.readyFile)+' && chmod 644 '+q(report.operatorRequest.readyFile));
    fs.writeFileSync(path.join(local,'deployment-result.json'),JSON.stringify({...metadata,...report,status:'deployed'},null,2));
    console.log(JSON.stringify({status:'deployed',backup:local,...report}));
  }catch(error){
    if(opened){
      // Players may already have earned progress: never restore the pre-cutover
      // save after reopening. Keep this verified backend and close the gate for
      // diagnosis if the final public probe fails.
      ssh('touch '+q(marker));
      console.error('Public verification failed after reopening; current save and backend retained behind maintenance.');
    }else if(stopped){
      // The maintenance gate has kept players out throughout this attempt.
      ssh('touch '+q(marker));
      ssh('systemctl stop florrclone');
      ssh('install -o florrclone -g florrclone -m 600 '+q(remote+'/inventory.json')+' /var/lib/florrclone/cpp-state/inventory.json');
      if(switched)ssh('ln -s '+q(old)+' /opt/florrclone/current.rollback && mv -Tf /opt/florrclone/current.rollback /opt/florrclone/current');
      if(bonusHelper)preserveMaintenanceBonus();
      ssh('systemctl start florrclone');
    }
    // Keep a useful waiting page on failure; never reopen an unverified backend.
    throw error;
  }finally{if(locked)ssh('rmdir /var/lib/florrclone/deploy.lock');}
}
main().catch(e=>{console.error(e.message);process.exitCode=1;});
}
