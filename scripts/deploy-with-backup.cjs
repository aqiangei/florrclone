'use strict';
if(process.argv[2]==='--legacy-third') {
  require('./deploy-legacy-third.cjs').main(process.argv.slice(3)).catch(error=>{
    console.error(error.message);process.exitCode=1;
  });
} else if(process.argv[2]==='--restore-stable') {
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
const clientAssets=require('./shards/ops/client-assets.cjs');
const release=process.argv[2];
if(!/^\/opt\/florrclone\/releases\/[a-zA-Z0-9_-]+$/.test(release||''))
  throw Error('Supply a staged /opt/florrclone/releases/RELEASE directory');
const root=path.resolve(__dirname,'..');
const backupRoot=path.resolve(process.argv[3]||path.join(root,'../backups/florrclone'));
const stamp=new Date().toISOString().replace(/[-:]/g,'').replace(/\.\d+Z$/,'Z');
const local=path.join(backupRoot,stamp);fs.mkdirSync(local,{recursive:true});
const target=process.env.FLORR_DEPLOY_TARGET_FILE?JSON.parse(fs.readFileSync(process.env.FLORR_DEPLOY_TARGET_FILE,'utf8')):{};
assert(!target.host||/^[a-zA-Z0-9.-]+$/.test(target.host));
const key=target.key||path.join(process.env.USERPROFILE||process.env.HOME,'.ssh/ASHFLOR.pem');
assert([undefined, 'fresh', 'third'].includes(target.instance), 'Unknown deployment instance');
const fresh = target.instance === 'fresh';
const third = target.instance === 'third';
if (fresh) assert.equal(target.host, '154.219.105.161', 'Unexpected fresh instance host');
if (third) assert.equal(target.host, '154.219.105.164', 'Unexpected third instance host');
const host='root@'+(target.host||'154.219.105.161');
const service = third ? 'florr-third' : fresh ? 'florr-fresh' : 'florrclone';
const installOwner = third ? 'florr-third' : 'florrclone';
const runtime = third ? '/usr/bin/node --experimental-websocket' : '/opt/florrclone/runtime/bin/node';
const current = third ? '/opt/florrclone-third/current' : '/opt/'+service+'/current';
const stateRoot = '/var/lib/'+service;
const databaseFile = stateRoot + '/cpp-state/inventory.json';
const databaseNext = stateRoot + '/cpp-state/inventory.next.json';
const remote = stateRoot + '/cutover-backups/' + stamp;
const maintenance = third ? stateRoot+'/maintenance' : '/var/www/'+service+'-maintenance';
const marker = maintenance + '/enabled';
const nginxConfig = '/etc/nginx/conf.d/'+(third ? 'gamesrv-10000' : fresh ? 'florr-fresh-10000' : 'florrclone')+'.conf';
const publicPort = fresh || third ? 10000 : 8088;
const backendPort = third ? 38788 : fresh ? 28788 : 8788;
const backend = 'http://127.0.0.1:' + backendPort;
const run=(exe,args,input)=>{
  for(let attempt=0;;attempt++)try{return cp.execFileSync(exe,args,{input,encoding:'utf8',maxBuffer:1024*1024});}
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
let locked=false,stopped=false,saveInstallStarted=false,switched=false,opened=false,old='',bonusStoppedAt=0,bonusHelper='';
function preserveMaintenanceBonus(){
  assert(bonusHelper&&bonusStoppedAt>0,'Maintenance bonus preparation missing');
  const state=ssh('systemctl show '+service+' -p MainPID -p ActiveState -p SubState');
  assert(state.includes('MainPID=0')&&state.includes('ActiveState=inactive')&&state.includes('SubState=dead'),'Bonus adjustment requires a stopped game');
  const result=JSON.parse(ssh(runtime+' '+q(bonusHelper)+' '+q(databaseFile)+' '+q(stamp)+' '+bonusStoppedAt));
  fs.writeFileSync(path.join(local,'maintenance-bonus.json'),JSON.stringify(result,null,2));
  return result;
}
function nginxFor(version, downloadQueue = process.env.FLORR_DOWNLOAD_QUEUE === '1', assetHashes, assetMirror) {
  let base = fs.readFileSync(path.join(__dirname, 'shards/ops/nginx-game.conf'), 'utf8');
  if (fresh || third) {
    base = base.replaceAll('8088 default_server', '10000')
      .replaceAll('127.0.0.1:8788', '127.0.0.1:' + backendPort)
      .replaceAll('florrclone_connection_upgrade', service.replaceAll('-', '_')+'_connection_upgrade');
  }
  return clientAssets.configure(base, {
    release: version,
    assetHost: process.env.FLORR_CLIENT_ASSET_HOST,
    assetPort: publicPort,
    downloadQueue,
    entryProxy: process.env.FLORR_ENTRY_PROXY_IP,
    assetHashes,
    assetMirror,
  }).replaceAll('/var/www/florrclone-maintenance/', maintenance + '/');
}
function installNginx(localFile, backupFile) {
  const staging = remote + '/nginx-next.conf';
  scp(localFile, host + ':' + staging);
  ssh('cp '+q(nginxConfig)+' '+q(backupFile)+' && cp '+q(staging)+' '+q(nginxConfig)+
      ' && { nginx -t && systemctl reload nginx || { cp '+q(backupFile)+' '+q(nginxConfig)+
      '; nginx -t && systemctl reload nginx; exit 1; }; }');
}
async function main(){
  const announcements=require("./deploy-announcement.cjs");
  const noticeConfig=announcements.readConfig(process.env.FLORR_DEPLOY_ADMIN_FILE);
  const bootstrap=process.env.FLORR_BOOTSTRAP_DEPLOY_ADMIN==='1';
  try{
    const resetFile=process.env.FLORR_PASSWORD_RESET_FILE;
    const preserveSave=process.env.FLORR_PRESERVE_SAVE==='1'||Boolean(resetFile);
    const resetRequests=resetFile?require('./reset-password-offline.cjs').readRequests(resetFile):null;
    const councilManagerId=process.env.FLORR_FRESH_COUNCIL_MANAGER_ID;
    const newSkinModeratorFile=process.env.FLORR_CREATE_SKIN_MODERATOR_FILE;
    const newSkinModerator=newSkinModeratorFile ? JSON.parse(fs.readFileSync(newSkinModeratorFile,'utf8')) : null;
    if(newSkinModerator)assert(third && preserveSave && !process.env.FLORR_SKIN_MODERATOR_ID &&
      /^[a-z0-9_]{3,20}$/.test(newSkinModerator.username) &&
      /^[A-Za-z0-9_-]{24,64}$/.test(newSkinModerator.password) &&
      /^[a-f0-9-]{36}$/.test(newSkinModerator.accountId), 'Invalid new skin moderator configuration');
    const skinModeratorId=newSkinModerator?.accountId || process.env.FLORR_SKIN_MODERATOR_ID;
    const skinTakedownsFile=process.env.FLORR_SKIN_TAKEDOWNS_FILE;
    const skinTakedowns=skinTakedownsFile ? JSON.parse(fs.readFileSync(skinTakedownsFile,'utf8')) : [];
    assert(Array.isArray(skinTakedowns) && skinTakedowns.every(entry=>
      typeof entry.id==='string' && /^sk_[a-z0-9_]+$/.test(entry.id) &&
      typeof entry.author==='string' && typeof entry.name==='string'), 'Invalid skin takedown list');
    if(skinTakedowns.length)assert(preserveSave && !process.env.FLORR_RESTORE_SNAPSHOT,
      'Skin takedowns require a preserved save');
    if(skinModeratorId)assert(preserveSave && !process.env.FLORR_RESTORE_SNAPSHOT &&
      /^[A-Za-z0-9_-]{1,128}$/.test(skinModeratorId),
      'Skin moderation requires an explicit account ID and a preserved save');
    if(councilManagerId)assert(fresh && preserveSave && !bootstrap,
      'Council delegation requires the independent second-server save');
    if(resetRequests) {
      assert(!fresh&&!third&&!bootstrap&&!process.env.FLORR_RESTORE_SNAPSHOT &&
        !process.env.FLORR_OPERATOR_HYPER_USER && process.env.FLORR_REFUND_ETERNAL_PRICE!=='1',
        'Password resets require an independent first-server update using its current save');
    }
    // Once shared authority is installed, the one-host path may neither replace
    // its local cache nor silently roll one server back to independent storage.
    assert(ssh('if test -e /etc/florrclone/shard-game.json || test -e /var/lib/florrclone-shards/authority.snapshot; then echo SHARDED; else echo LEGACY; fi').trim()==='LEGACY',
      'Shared-account servers require deploy-with-backup.cjs --cluster CONFIG');
    ssh('mkdir '+q(stateRoot+'/deploy.lock'));locked=true;
    old=ssh('readlink -f '+q(current)).trim();
    if (fresh || third) {
      const queuePolicy = third ? ['0'] : ['0','1'];
      const restoring = Boolean(process.env.FLORR_RESTORE_SNAPSHOT);
      const verifiedRestore = restoring && !preserveSave &&
        /^[a-f0-9]{64}$/.test(process.env.FLORR_RESTORE_SHA256 || '');
      assert((verifiedRestore || (preserveSave && !restoring)) &&
             queuePolicy.includes(process.env.FLORR_DOWNLOAD_QUEUE) &&
             process.env.FLORR_CLIENT_ASSET_HOST === target.host && !process.env.FLORR_ENTRY_PROXY_IP &&
             !process.env.FLORR_PASSWORD_RESET_FILE,
             'Independent instance requires a verified restore or its own preserved save and a download policy');
      const command = ssh('systemctl show '+service+' --property=ExecStart --value');
      assert(command.includes(current+'/server.js') &&
             command.includes('--db '+databaseFile+' '),
             'Unexpected independent instance command or save');
      ssh('test -f '+q(nginxConfig)+' && test ! -e '+q(marker)+' && install -d -m 755 '+q(maintenance));
    }
    assert.match(old,/^\/opt\/florrclone\/releases\/[a-zA-Z0-9_-]+$/);
    if(process.env.FLORR_EXPECTED_RELEASE)assert.equal(old,process.env.FLORR_EXPECTED_RELEASE,'Online release changed; coordinate before deploying');
    const releaseProbe=release+'/release-hello.cjs';
    const probe=ssh('if test -s '+q(releaseProbe)+'; then echo '+q(releaseProbe)+'; else echo /var/lib/florrclone/release-hello.cjs; fi').trim();
    ssh('test -s '+q(release+'/server.wasm')+' && test -s '+q(release+'/bundle.wasm')+' && test -s '+q(release+'/deploy-runtime.cjs'));
    const backgroundSave=dir=>ssh('test -f '+q(dir+'/server.background-save')+' && echo enabled || true').trim()==='enabled';
    const retiringBackgroundSave=backgroundSave(old), candidateBackgroundSave=backgroundSave(release);
    let assetHashes;
    const assetMirror=process.env.FLORR_CLIENT_ASSET_MIRROR;
    if(process.env.FLORR_CLIENT_ASSET_HOST){
      for(const file of ['index.html',...clientAssets.assetNames])
        ssh('runuser -u www-data -- test -r '+q(release+'/'+file));
      assetHashes=clientAssets.readAssetHashes(ssh,release);
    }
    if(assetMirror){
      assert(assetHashes && process.env.FLORR_DOWNLOAD_QUEUE==='0','Mirror requires direct, versioned downloads');
      const url=clientAssets.mirrorAssetUrl(assetMirror,assetHashes);
      const digest=assetHashes['bundle.wasm.bin'];
      const check=`(async()=>{const r=await fetch(${JSON.stringify(url)},{redirect:'error',signal:AbortSignal.timeout(60000)});
        if(r.status!==200 || r.headers.get('access-control-allow-origin')!=='*' ||
            (r.headers.get('content-encoding')||'identity')!=='identity')throw Error('Mirror HTTP/CORS check failed');
        const size=Number(r.headers.get('content-length'));
        const hash=require('node:crypto').createHash('sha256');for await(const chunk of r.body)hash.update(chunk);
        if(hash.digest('hex')!==${JSON.stringify(digest)})throw Error('Mirror WASM SHA256 differs from candidate');
        const range=await fetch(${JSON.stringify(url)},{redirect:'error',headers:{Range:'bytes=0-0'},signal:AbortSignal.timeout(10000)});
        if(range.status!==206 || range.headers.get('access-control-allow-origin')!=='*' ||
            range.headers.get('content-range')!=='bytes 0-0/'+size || (await range.arrayBuffer()).byteLength!==1)
          throw Error('Mirror does not serve valid byte ranges');
        console.log('PASS public mirror WASM SHA256');})().catch(e=>{console.error(e.message);process.exitCode=1});`;
      console.log(ssh(runtime+' -e '+q(check)));
    }
    ssh('curl -fsS '+q(backend+'/api/health'));
    ssh('install -d -m 700 '+q(remote));
    // Publish this release's Chinese notes before enabling maintenance.
    const notes=JSON.parse(fs.readFileSync(process.env.FLORR_RELEASE_NOTES_FILE||path.join(__dirname,'release-notes.json'),'utf8'));
    assert(Array.isArray(notes.changes)&&notes.changes.length>0,'Release notes are required');
    const escape=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
    const html=fs.readFileSync(path.join(__dirname,'maintenance.html'),'utf8').replace('<!-- RELEASE_NOTES -->',
      '<section style="text-align:left;font-size:14px"><h2>'+escape(notes.title)+'</h2><ul>'+notes.changes.map(t=>'<li>'+escape(t)+'</li>').join('')+'</ul></section>');
    const waitingFile=path.join(local,'maintenance.html');fs.writeFileSync(waitingFile,html);
    scp(waitingFile,host+':'+maintenance+'/maintenance.next.html');
    ssh('mv '+q(maintenance+'/maintenance.next.html')+' '+q(maintenance+'/maintenance.html'));
    if (fresh) {
      const nginx = path.join(local, 'nginx-waiting-ready.conf');
      const previousQueue=ssh('systemctl is-active florr-download-queue || true').trim()==='active';
      fs.writeFileSync(nginx, nginxFor(old,previousQueue,clientAssets.readAssetHashes(ssh,old)));
      installNginx(nginx, remote+'/nginx-initial.conf');
    }
    // Announce while players are still connected, before the maintenance gate.
    // Only an echoed Admin-channel broadcast counts as delivery confirmation.
    if (!bootstrap) {
      const announcer=await announcements.connect(noticeConfig);
      try {
        if (process.env.FLORR_ANNOUNCE_MIGRATION === '1') {
          await announcer.broadcast('【一服迁移】旧入口跳转至新址；游客请在账户设置用户名和密码，避免丢失进度。');
          console.log('已确认一服迁移提醒广播');
        }
        await announcements.countdown(message=>announcer.broadcast(message));
      }
      finally { announcer.close(); }
    } else {
      assert(process.env.FLORR_PRESERVE_SAVE==='1', 'Operator bootstrap requires preserving every existing account');
      console.log('首次配置广播管理员：本次使用维护等待页，下次起全服广播倒计时。');
    }
    // Serve the waiting page before gracefully stopping the old process.
    ssh('touch '+q(marker));
    const waiting=ssh('curl -sS http://127.0.0.1:'+publicPort+'/');
    assert(waiting.includes('花朵躲石头'),'Waiting page must work before stopping');
    assert(waiting.includes(escape(notes.title)) && waiting.includes(escape(notes.changes[0])),'Current release notes must be visible before stopping');
    if (bootstrap) await new Promise(resolve=>setTimeout(resolve,60000));
    bonusStoppedAt=Number(ssh('date +%s%3N').trim());
    assert(Number.isSafeInteger(bonusStoppedAt)&&bonusStoppedAt>0);
    const retiringPid=ssh('systemctl show '+service+' -p MainPID --value').trim();
    assert.match(retiringPid,/^[1-9][0-9]*$/,'No running game process to save');
    stopped=true;ssh('systemctl stop '+service);
    assert.equal(ssh('systemctl show '+service+' -p ExecMainStatus --value').trim(),'0','Game failed to stop cleanly');
    let saved;
    if(retiringBackgroundSave) {
      // The process can exit before journald drains its final stdout record.
      const journal='journalctl --boot -u '+service+' _PID='+retiringPid+
        ' -n 120 -o cat --no-pager | grep -m 1 -F '+q('[persist-shutdown]');
      const line=ssh('for attempt in $(seq 1 15); do '+journal+' && exit 0; sleep 1; done; exit 1').trim();
      const match=line.match(/generation=(\d+) accounts=(\d+) players=(\d+) bytes=(\d+) sha256=([0-9a-f]{64})$/);
      assert(match,'The retiring process did not confirm its final durable save');
      saved={generation:match[1],accounts:Number(match[2]),players:Number(match[3]),bytes:Number(match[4]),sha256:match[5]};
    }
    ssh('cp '+q(databaseFile)+' '+q(remote+'/inventory.json')+' && chmod 600 '+q(remote+'/inventory.json'));
    scp(host+':'+remote+'/inventory.json',path.join(local,'inventory.json'));
    const bytes=fs.readFileSync(path.join(local,'inventory.json'));
    const hash=crypto.createHash('sha256').update(bytes).digest('hex');
    assert.equal(ssh('sha256sum '+q(remote+'/inventory.json')).split(/\s/)[0],hash);
    const before=JSON.parse(bytes);
    if(saved) {
      assert.equal(hash,saved.sha256,'Backup SHA does not match the final save receipt');
      assert.equal(bytes.length,saved.bytes);
      assert.equal(Object.keys(before.users||{}).length,saved.accounts);
      assert.equal(Object.keys(before.players||{}).length,saved.players);
    }
    const metadata={timestamp:stamp,previousRelease:old,nextRelease:release,sha256:hash,
      accounts:Object.keys(before.players||{}).length,remoteBackup:remote,...(saved&&{finalSave:saved})};
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
    if(preserveSave){
      assert(!process.env.FLORR_RESTORE_SNAPSHOT &&
        !process.env.FLORR_OPERATOR_HYPER_USER && process.env.FLORR_REFUND_ETERNAL_PRICE!=='1',
        'Preserve-save deployment cannot include account maintenance');
      next=path.join(local,'inventory.json');
      report={preservedSave:true};
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
    if(process.env.FLORR_OPERATOR_HYPER_USER){
      const queued=path.join(local,'inventory.operator.json');
      // Only the database directory is mounted into the WASM filesystem.
      const ready='/var/lib/florrclone/cpp-state/operator-ready-'+stamp;
      report.operatorRequest=JSON.parse(run(process.execPath,[path.join(__dirname,'prepare-operator-hyper-forge.cjs'),next,queued,process.env.FLORR_OPERATOR_HYPER_USER,'hammer-'+stamp,marker,ready]));
      fs.writeFileSync(path.join(local,'operator-request.json'),JSON.stringify(report.operatorRequest,null,2));
      next=queued;
    }
    }
    if(process.env.FLORR_PRIOR_MAINTENANCE_FILE){
      assert(process.env.FLORR_PRESERVE_SAVE==='1');
      const credit=JSON.parse(fs.readFileSync(process.env.FLORR_PRIOR_MAINTENANCE_FILE,'utf8'));
      assert(/^[a-zA-Z0-9_-]+$/.test(credit.id)&&Number.isSafeInteger(credit.durationMs)&&credit.durationMs>0&&credit.durationMs<3600000);
      const db=JSON.parse(fs.readFileSync(next,'utf8'));report.priorMaintenance={id:credit.id,extended:0,skipped:0};
      for(const row of credit.accounts){
        const bonus=db.players[row.accountId]?.dailyDropBonus;
        if(!bonus||bonus.day!==row.day||bonus.until!==row.until||bonus.until<=Date.now()||bonus.priorMaintenanceCredit===credit.id){report.priorMaintenance.skipped++;continue;}
        bonus.until+=credit.durationMs;bonus.priorMaintenanceCredit=credit.id;report.priorMaintenance.extended++;
      }
      next=path.join(local,'inventory.prior-maintenance.json');fs.writeFileSync(next,JSON.stringify(db));
    }
    if(process.env.FLORR_PETAL_GRANT_FILE){
      assert(process.env.FLORR_PRESERVE_SAVE==='1','Explicit petal grant requires preserve-save mode');
      const db=JSON.parse(fs.readFileSync(next,'utf8'));
      const request=JSON.parse(fs.readFileSync(process.env.FLORR_PETAL_GRANT_FILE,'utf8'));
      report.authorizedPetals=require('./grant-authorized-petals.cjs').apply(db,request);
      next=path.join(local,'inventory.authorized-petals.json');fs.writeFileSync(next,JSON.stringify(db));
      fs.writeFileSync(path.join(local,'authorized-petals.json'),JSON.stringify(report.authorizedPetals,null,2));
    }
    if(process.env.FLORR_COUNCIL_HISTORY_FILE){
      assert(process.env.FLORR_PRESERVE_SAVE==='1','History import requires preserve-save mode');
      const saved=JSON.parse(fs.readFileSync(next,'utf8'));
      const seed=JSON.parse(fs.readFileSync(process.env.FLORR_COUNCIL_HISTORY_FILE,'utf8'));
      report.councilHistory=require('./merge-council-history.cjs').apply(saved,seed);
      const historyFile=path.join(local,'inventory.history.json');fs.writeFileSync(historyFile,JSON.stringify(saved));next=historyFile;
    }
    if(councilManagerId){
      const saved=JSON.parse(fs.readFileSync(next,'utf8'));
      const {prepareCouncilManagers}=require('./prepare-council-managers.cjs');
      const delegated=prepareCouncilManagers(saved,[councilManagerId]);
      next=path.join(local,'inventory.council-delegation.json');
      fs.writeFileSync(next,JSON.stringify(delegated),{mode:0o600});
      report.councilDelegation=true;
    }
    if(skinModeratorId || skinTakedowns.length){
      const saved=JSON.parse(fs.readFileSync(next,'utf8'));
      if(newSkinModerator){
        const {username,password,accountId}=newSkinModerator;
        const existing=saved.users[username];
        if(existing){
          assert.equal(existing.id,accountId,'Requested new account name is already in use');
        } else {
          assert(!Object.values(saved.users).some(account=>account.id===accountId) &&
            !Object.hasOwn(saved.players,accountId),'New moderator ID already exists');
          const salt=crypto.randomBytes(16).toString('hex');
          saved.users[username]={id:accountId,username,
            password:crypto.scryptSync(password,salt,64).toString('hex'),
            salt,isPlainText:false,passwordChosen:true,createdAt:Date.now(),lastActiveAt:0};
          saved.players[accountId]={};
          report.skinModeratorCreated=true;
        }
      }
      if(skinModeratorId){
        const matches=Object.values(saved.users).filter(account=>account.id===skinModeratorId);
        assert(matches.length===1 && !matches[0].guest && !matches[0].adminBanned,
          'Skin moderator account is missing or ineligible in the stopped save');
        const moderators=saved.skinModerators ?? {};
        assert(typeof moderators==='object' && !Array.isArray(moderators),'Invalid skin moderator table');
        saved.skinModerators={...moderators,[skinModeratorId]:true};
        report.skinModeration=true;
      }
      if(skinTakedowns.length){
        const ids=new Set();
        let removed=0,unequipped=0;
        for(const {id,author,name} of skinTakedowns){
          assert(!ids.has(id),'Duplicate skin takedown ID');
          ids.add(id);
          const skin=saved.customSkins?.[id];
          if(!skin)continue;
          assert(skin.id===id && skin.author===author && skin.name===name,
            'Skin takedown target differs from the approved catalog entry');
          delete saved.customSkins[id];
          ++removed;
        }
        for(const record of Object.values(saved.players)){
          if(!ids.has(record.equippedSkinId))continue;
          delete record.equippedSkinId;
          delete record.renderFlags;
          ++unequipped;
        }
        report.skinTakedowns={removed,unequipped};
      }
      next=path.join(local,'inventory.skin-moderation.json');
      fs.writeFileSync(next,JSON.stringify(saved),{mode:0o600});
    }
    if(resetRequests){
      const reset=path.join(local,'inventory.password-reset.json');
      report.passwordReset=JSON.parse(run(process.execPath,
        [path.join(__dirname,'reset-password-offline.cjs'),next,reset,'-'],JSON.stringify(resetRequests)));
      next=reset;
      fs.writeFileSync(path.join(local,'password-reset.json'),JSON.stringify(report.passwordReset,null,2));
    }
    scp(next,host+':'+remote+'/inventory.next.json');
    if(resetRequests||councilManagerId||skinModeratorId||skinTakedowns.length||bootstrap||process.env.FLORR_RESTORE_SNAPSHOT){
      const candidateHash=crypto.createHash('sha256').update(fs.readFileSync(next)).digest('hex');
      assert.equal(ssh('sha256sum '+q(remote+'/inventory.next.json')).split(/\s/)[0],candidateHash,
        'Save candidate SHA256 mismatch');
    }
    // All validation and the local backup gate have completed at this point.
    saveInstallStarted=true;
    ssh('install -o '+installOwner+' -g '+installOwner+' -m 600 '+q(remote+'/inventory.next.json')+' '+q(databaseNext));
    ssh('mv '+q(databaseNext)+' '+q(databaseFile));
    ssh('ln -s '+q(release)+' '+q(current+'.next')+' && mv -Tf '+q(current+'.next')+' '+q(current));switched=true;
    report.maintenanceBonus=preserveMaintenanceBonus();
    ssh('systemctl start '+service);
    let healthy=false;
    for(let i=0;i<40;i++){
      try{const h=JSON.parse(ssh('curl -fsS '+q(backend+'/api/health')));if(h.ok){healthy=true;break;}}catch{}
      await new Promise(r=>setTimeout(r,1000));
    }
    assert(healthy,'New service failed health check');
    if(candidateBackgroundSave) {
      const pid=ssh('systemctl show '+service+' -p MainPID --value').trim();
      assert.match(pid,/^[1-9][0-9]*$/);
      ssh('for attempt in $(seq 1 75); do journalctl --boot -u '+service+' _PID='+pid+
        ' --no-pager -o cat --grep '+q('\\[persist-worker\\] saved generation=')+
        ' | tail -1 | grep -q generation= && exit 0; sleep 1; done; exit 1');
    }
    for(const file of ['','bundle.js','bundle.wasm','bundle.js.bin','bundle.wasm.bin'])
      ssh('curl -fsS -o /dev/null '+q(backend+'/'+file));
    ssh('PORT='+backendPort+' '+runtime+' '+q(probe));
    if(process.env.FLORR_REQUIRE_COUNCIL==='1') {
      ssh('test -s '+q(release+'/council.html')+' && curl -fsS -o /dev/null '+q(backend+'/council.html'));
      const councilUrl = JSON.stringify(backend+'/api/council');
      const check="(async()=>{const r=await fetch("+councilUrl+",{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:'view'})});if(!r.ok)throw Error('Council HTTP '+r.status);const j=await r.json();if(j.error||!Array.isArray(j.players)||!Array.isArray(j.polls))throw Error('Invalid council state');if(j.players.length&&j.players.reduce((s,p)=>s+p.units,0)!==1000000)throw Error('Invalid council weights');console.log('PASS council API and exact weight total');})().catch(e=>{console.error(e.message);process.exitCode=1});";
      console.log(ssh(runtime+' -e '+q(check)));
    }
    if(skinModeratorId || skinTakedowns.length){
      const check="const fs=require('node:fs');const db=JSON.parse(fs.readFileSync(process.argv[1],'utf8'));"+
        "const moderator=process.argv[2],ids=new Set(JSON.parse(process.argv[3]));"+
        "if(moderator&&db.skinModerators?.[moderator]!==true)throw Error('Missing skin moderation in live save');"+
        "if([...ids].some(id=>db.customSkins?.[id])||Object.values(db.players).some(p=>ids.has(p.equippedSkinId)))"+
        "throw Error('Removed skin still appears in the live save');"+
        "console.log('PASS skin moderation and takedown save checks')";
      console.log(ssh(runtime+' -e '+q(check)+' '+q(databaseFile)+' '+q(skinModeratorId||'')+' '+
        q(JSON.stringify(skinTakedowns.map(entry=>entry.id)))));
    }
    if(newSkinModerator){
      const candidate={...newSkinModerator,
        protocolVersion:Number(process.env.FLORR_NEXT_PROTOCOL_VERSION),
        contentHash:Number(process.env.FLORR_NEXT_CONTENT_HASH),
        url:'ws://127.0.0.1:'+backendPort+'/ws'};
      const verify="const fs=require('node:fs');(async()=>{const client=await require("+
        JSON.stringify(release+'/deploy-announcement.cjs')+").connect(JSON.parse(fs.readFileSync(0,'utf8')));"+
        "client.close();console.log('PASS new skin moderator can sign in');})().catch(error=>{console.error(error.message);process.exitCode=1});";
      console.log(cp.execFileSync('ssh',[...sshOptions,host,runtime+' -e '+q(verify)],
        {input:JSON.stringify(candidate),encoding:'utf8',timeout:30000}));
    }
    if (bootstrap) {
      const newHash=Number(process.env.FLORR_NEXT_CONTENT_HASH);
      assert(Number.isInteger(newHash) && newHash>=0 && newHash<=0xffffffff, 'Candidate content hash required for operator validation');
      const version = Number(process.env.FLORR_NEXT_PROTOCOL_VERSION || noticeConfig.protocolVersion);
      assert(Number.isInteger(version) && version > 0 && version <= 65535, 'Invalid candidate protocol');
      const candidate={...noticeConfig,protocolVersion:version,contentHash:newHash,url:'ws://127.0.0.1:'+backendPort+'/ws'};
      const verify=`const fs=require('fs');(async()=>{const a=await require(${JSON.stringify(release+'/deploy-announcement.cjs')}).connect(JSON.parse(fs.readFileSync(0,'utf8')));try{await a.broadcast('维护更新验证完成，即将开放入口。');console.log('PASS operator Admin broadcast');}finally{a.close();}})().catch(e=>{console.error(e.message);process.exitCode=1});`;
      console.log(cp.execFileSync('ssh',[...sshOptions,host,runtime+' -e '+q(verify)],{input:JSON.stringify(candidate),encoding:'utf8',timeout:30000}));
    }
    if(process.env.FLORR_DOWNLOAD_QUEUE==='1'){
      assert(process.env.FLORR_CLIENT_ASSET_HOST,'Queue requires a versioned asset host');
      const unit=path.join(local,'florr-download-queue.service');
      const assetBase='http://'+process.env.FLORR_CLIENT_ASSET_HOST+':'+publicPort+'/client-assets/'+release.split('/').pop()+'/';
      const slots=Number(process.env.FLORR_DOWNLOAD_SLOTS||8);
      assert(Number.isInteger(slots)&&slots>=1&&slots<=32,'Invalid download slot count');
      fs.writeFileSync(unit,'[Unit]\nDescription=Florr client download queue\nAfter=network.target\n[Service]\nUser=florrclone\nEnvironment=PORT=8789\nEnvironment=DOWNLOAD_SLOTS='+slots+'\nEnvironment=DOWNLOAD_ASSET_BASE='+assetBase+'\nExecStart=/opt/florrclone/runtime/bin/node '+release+'/download-queue.cjs\nRestart=on-failure\nNoNewPrivileges=true\nProtectSystem=strict\nProtectHome=true\n[Install]\nWantedBy=multi-user.target\n');
      ssh('test -s '+q(release+'/download-queue.cjs')+' && if test -f /etc/systemd/system/florr-download-queue.service; then cp /etc/systemd/system/florr-download-queue.service '+q(remote+'/download-queue-before.service')+'; fi');
      scp(unit,host+':/etc/systemd/system/florr-download-queue.service');
      ssh('systemctl daemon-reload && systemctl enable florr-download-queue && systemctl restart florr-download-queue');
      let queueReady=false;for(let i=0;i<15;i++){try{assert(JSON.parse(ssh('curl -fsS http://127.0.0.1:8789/health')).ok);queueReady=true;break;}catch{await new Promise(r=>setTimeout(r,1000));}}
      assert(queueReady,'Download queue unavailable; keep maintenance closed');
    }
    if(process.env.FLORR_CLIENT_ASSET_HOST){
      const nginx=path.join(local,'nginx-client-assets.conf');
      fs.writeFileSync(nginx, nginxFor(release,undefined,assetHashes,assetMirror));
      installNginx(nginx, remote+'/nginx-before.conf');
      const assetRoot='http://127.0.0.1:'+publicPort+'/client-assets/'+release.split('/').pop()+'/';
      const cachePaths=Object.fromEntries(clientAssets.assetNames.map(file=>
        [file,clientAssets.cacheAssetPath(file,assetHashes)]));
      const verifyAssets=`(async()=>{
        const queue='http://127.0.0.1:8789/download-queue/';
        let ticket;
        try {
          if(${process.env.FLORR_DOWNLOAD_QUEUE==='1'}){
            const response=await fetch(queue+'join',{method:'POST',signal:AbortSignal.timeout(5000)});
            const row=await response.json();
            if(!response.ok || row.state!=='ready' || !row.ticket)throw Error('No download test slot');
            ticket=row.ticket;
          }
          for(const [file,cachePath] of Object.entries(${JSON.stringify(cachePaths)})){
            const headers={Range:'bytes=0-63',Origin:'http://154.219.105.161:8088'};
            if(ticket)headers['X-Download-Ticket']=ticket;
            const response=await fetch(${JSON.stringify(assetRoot)}+file,{headers,signal:AbortSignal.timeout(5000)});
            if(response.status!==206 || new URL(response.url).pathname!==cachePath ||
               !response.headers.get('cache-control')?.includes('immutable') ||
               response.headers.get('access-control-allow-origin')!=='*' ||
               (await response.arrayBuffer()).byteLength!==64)
              throw Error('Nginx asset probe: '+file+' HTTP '+response.status);
          }
        }finally{
          if(ticket)await fetch(queue+'release?ticket='+encodeURIComponent(ticket),{method:'POST',signal:AbortSignal.timeout(5000)});
        }
      })().catch(e=>{console.error(e.message);process.exitCode=1})`;
      ssh(runtime+' -e '+q(verifyAssets));
    }
    ssh('rm '+q(marker));opened=true;
    ssh('PORT='+publicPort+' '+runtime+' '+q(probe));
    if(report.operatorRequest)ssh('touch '+q(report.operatorRequest.readyFile)+' && chmod 644 '+q(report.operatorRequest.readyFile));
    if(process.env.FLORR_NEXT_CONTENT_HASH){
      const nextHash=Number(process.env.FLORR_NEXT_CONTENT_HASH);assert(Number.isInteger(nextHash)&&nextHash>=0&&nextHash<=0xffffffff);
      fs.writeFileSync(process.env.FLORR_DEPLOY_ADMIN_FILE,JSON.stringify({...noticeConfig,
        protocolVersion:Number(process.env.FLORR_NEXT_PROTOCOL_VERSION || noticeConfig.protocolVersion),
        contentHash:nextHash}));
    }
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
      ssh('if test -s '+q(remote+'/nginx-before.conf')+'; then cp '+q(remote+'/nginx-before.conf')+' '+q(nginxConfig)+' && nginx -t && systemctl reload nginx; fi');
      if(process.env.FLORR_DOWNLOAD_QUEUE==='1'){
        ssh('if test -s '+q(remote+'/download-queue-before.service')+'; then cp '+q(remote+'/download-queue-before.service')+' /etc/systemd/system/florr-download-queue.service && systemctl daemon-reload && systemctl restart florr-download-queue; else systemctl disable --now florr-download-queue || true; fi');
      }

      // The maintenance gate has kept players out throughout this attempt.
      ssh('touch '+q(marker));
      ssh('systemctl stop '+service);
      if(saveInstallStarted)ssh('install -o '+installOwner+' -g '+installOwner+' -m 600 '+q(remote+'/inventory.json')+' '+q(databaseFile));
      if(switched)ssh('ln -s '+q(old)+' '+q(current+'.rollback')+' && mv -Tf '+q(current+'.rollback')+' '+q(current));
      if(bonusHelper)preserveMaintenanceBonus();
      ssh('systemctl start '+service);
    }
    // Keep a useful waiting page on failure; never reopen an unverified backend.
    throw error;
  }finally{if(locked)ssh('rmdir '+q(stateRoot+'/deploy.lock'));}
}
main().catch(e=>{console.error(e.message);process.exitCode=1;});
}
