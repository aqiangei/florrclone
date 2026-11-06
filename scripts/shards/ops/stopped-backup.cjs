'use strict';
// Called only by the coordinated deployment entry point. No remote credentials
// are kept here; transports are supplied by its validated target configuration.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const quote=s=>"'"+s.replace(/'/g,"'\\''")+"'";
function stopped(transport,unit,allowAbsent=false){
  assert(['florrclone','florr-shard-coordinator'].includes(unit));
  const output=transport.ssh('systemctl show '+unit+' -p LoadState -p ActiveState -p SubState -p Result -p ExecMainStatus');
  const fields=Object.fromEntries(output.trim().split(/\r?\n/).map(line=>{const at=line.indexOf('=');return [line.slice(0,at),line.slice(at+1)];}));
  if(allowAbsent&&fields.LoadState==='not-found')return;
  assert.equal(fields.LoadState,'loaded',unit+' unit unavailable');
  assert.equal(fields.ActiveState,'inactive',unit+' must be stopped before backup');
  assert.equal(fields.SubState,'dead',unit+' has not finished stopping');
  assert.equal(fields.Result,'success',unit+' did not stop cleanly');
  assert.equal(fields.ExecMainStatus,'0',unit+' exited with an error');
}
function collect({mode,stamp,directory,one,two}){
  assert(['initial','upgrade'].includes(mode),'Explicit migration mode required');
  assert.match(stamp,/^\d{8}T\d{6}Z$/);
  const destination=path.resolve(directory);
  assert(!fs.existsSync(path.join(destination,'verified-backup.json')),'Backup manifest already exists');
  // Verify both simulation processes, including the not-yet-created second
  // service on first migration, before taking any purported stopped snapshot.
  stopped(one,'florrclone');stopped(two,'florrclone',mode==='initial');
  stopped(one,'florr-shard-coordinator',mode==='initial');
  const authority='/var/lib/florrclone-shards/authority';
  if(mode==='initial')one.ssh('test ! -e '+quote(authority+'.snapshot')+' && test ! -e '+quote(authority+'.journal'));
  const sources=mode==='initial'
    ? [['inventory.json','/var/lib/florrclone/cpp-state/inventory.json']]
    : [['authority.snapshot',authority+'.snapshot'],['authority.journal',authority+'.journal'],
       ['authority.snapshot.lock',authority+'.snapshot.lock'],['bootstrap.json','/var/lib/florrclone-shards/bootstrap.json']];
  const remote='/var/lib/florrclone/cutover-backups/'+stamp+'/shared';
  one.ssh('install -d -m 700 '+quote(remote));
  fs.mkdirSync(destination,{recursive:true,mode:0o700});
  const files=[];
  for(const [name,source]of sources){
    const target=path.join(destination,name);
    assert(!fs.existsSync(target),'Refusing to overwrite a local backup');
    const copy=remote+'/'+name;
    one.ssh('test -f '+quote(source)+' && cp -- '+quote(source)+' '+quote(copy)+' && chmod 600 '+quote(copy));
    const expected=one.ssh('sha256sum -- '+quote(copy)).trim().split(/\s+/)[0];
    assert.match(expected,/^[a-f0-9]{64}$/,'Invalid remote hash');
    one.download(copy,target);
    const actual=crypto.createHash('sha256').update(fs.readFileSync(target)).digest('hex');
    assert.equal(actual,expected,'Downloaded backup SHA256 mismatch: '+name);
    const current=one.ssh('sha256sum -- '+quote(source)).trim().split(/\s+/)[0];
    assert.equal(current,expected,'Stopped source changed during backup: '+name);
    files.push({name,source,remote:copy,sha256:actual,bytes:fs.statSync(target).size});
  }
  // A partial download never creates this completion marker. Deployment must
  // receive this complete result before bootstrap, release or unit replacement.
  const inventory=JSON.parse(fs.readFileSync(path.join(destination,mode==='initial'?'inventory.json':'bootstrap.json'),'utf8'));
  assert(inventory&&typeof inventory.users==='object'&&typeof inventory.players==='object','Invalid inventory snapshot');
  const report={mode,stamp,files,bootstrapAccounts:Object.keys(inventory.players).length};
  fs.writeFileSync(path.join(destination,'verified-backup.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx',mode:0o600});
  return report;
}
module.exports={stopped,collect};
