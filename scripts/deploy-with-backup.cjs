'use strict';
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
const run=(exe,args)=>cp.execFileSync(exe,args,{encoding:'utf8',maxBuffer:1024*1024});
const ssh=s=>run('ssh',['-i',key,host,s]);
const scp=(a,b)=>run('scp',['-i',key,a,b]);
const q=s=>"'"+s.replace(/'/g,"'\\''")+"'";
let locked=false,stopped=false,switched=false,old='';
async function main(){
  try{
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
    const notes=JSON.parse(fs.readFileSync(path.join(__dirname,'release-notes.json'),'utf8'));
    assert(Array.isArray(notes.changes)&&notes.changes.length>0,'Release notes are required');
    const escape=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
    const html=fs.readFileSync(path.join(__dirname,'maintenance.html'),'utf8').replace('<!-- RELEASE_NOTES -->',
      '<section style="text-align:left;font-size:14px"><h2>'+escape(notes.title)+'</h2><ul>'+notes.changes.map(t=>'<li>'+escape(t)+'</li>').join('')+'</ul></section>');
    const waitingFile=path.join(local,'maintenance.html');fs.writeFileSync(waitingFile,html);
    scp(waitingFile,host+':/var/www/florrclone-maintenance/maintenance.next.html');
    ssh('mv /var/www/florrclone-maintenance/maintenance.next.html /var/www/florrclone-maintenance/maintenance.html');
    // Serve the waiting page before gracefully stopping the old process.
    ssh('touch '+q(marker));
    const waiting=ssh('curl -sS http://127.0.0.1:8088/');
    assert(waiting.includes('花朵躲石头'),'Waiting page must work before stopping');
    assert(waiting.includes(escape(notes.title)) && waiting.includes(escape(notes.changes[0])),'Current release notes must be visible before stopping');
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
    // Preserve the original migration snapshot's fixed TP debt without
    // changing passwords, progress or inventory earned since the cutover.
    const baseline=path.join(root,'migrate/latest-final-cpp.json');
    const next=path.join(local,'inventory.reconciled.json');
    const report=JSON.parse(run(process.execPath,[path.join(__dirname,'reconcile-migrated-tp.js'),baseline,path.join(local,'inventory.json'),next]));
    const compare=JSON.parse(fs.readFileSync(next));
    for(const [id,p]of Object.entries(compare.players||{}))for(const k of ['talentPointBonus','talentPointReconciledAt']){
      if(k in before.players[id])p[k]=before.players[id][k];else delete p[k];
    }
    assert.deepEqual(compare,before);
    fs.writeFileSync(path.join(local,'tp-reconciliation.json'),JSON.stringify(report,null,2));
    scp(next,host+':'+remote+'/inventory.next.json');
    // All validation and the local backup gate have completed at this point.
    ssh('install -o florrclone -g florrclone -m 600 '+q(remote+'/inventory.next.json')+' /var/lib/florrclone/cpp-state/inventory.next.json');
    ssh('mv /var/lib/florrclone/cpp-state/inventory.next.json /var/lib/florrclone/cpp-state/inventory.json');
    ssh('ln -s '+q(release)+' /opt/florrclone/current.next && mv -Tf /opt/florrclone/current.next /opt/florrclone/current');switched=true;
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
    ssh('rm '+q(marker));
    ssh('PORT=8088 /opt/florrclone/runtime/bin/node '+q(probe));
    fs.writeFileSync(path.join(local,'deployment-result.json'),JSON.stringify({...metadata,...report,status:'deployed'},null,2));
    console.log(JSON.stringify({status:'deployed',backup:local,...report}));
  }catch(error){
    if(stopped){
      // The maintenance gate has kept players out throughout this attempt.
      ssh('touch '+q(marker));
      ssh('systemctl stop florrclone');
      ssh('install -o florrclone -g florrclone -m 600 '+q(remote+'/inventory.json')+' /var/lib/florrclone/cpp-state/inventory.json');
      if(switched)ssh('ln -s '+q(old)+' /opt/florrclone/current.rollback && mv -Tf /opt/florrclone/current.rollback /opt/florrclone/current');
      ssh('systemctl start florrclone');
    }
    // Keep a useful waiting page on failure; never reopen an unverified backend.
    throw error;
  }finally{if(locked)ssh('rmdir /var/lib/florrclone/deploy.lock');}
}
main().catch(e=>{console.error(e.message);process.exitCode=1;});
