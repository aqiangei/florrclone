'use strict';
// Download a consistent, already-persisted production snapshot without stopping
// the game. Database::save publishes inventory.json with an atomic rename.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const cp = require('node:child_process');
const assert = require('node:assert/strict');

const targetFile=path.resolve(__dirname,'../outputs/production-backup-target.json');
const target=fs.existsSync(targetFile)?JSON.parse(fs.readFileSync(targetFile,'utf8')):{};
assert(!target.host||/^[a-zA-Z0-9.-]+$/.test(target.host));
const host = 'root@'+(process.env.FLORR_BACKUP_HOST||target.host||'154.219.105.161');
const source = '/var/lib/florrclone/cpp-state/inventory.json';
const key = process.env.FLORR_BACKUP_KEY||target.key||path.join(process.env.USERPROFILE || process.env.HOME, '.ssh', 'ASHFLOR.pem');
const root = path.resolve(process.argv[2] || 'D:/ashflor/backups/florrclone');
const stamp = new Date().toISOString().replace(/[-:]/g, '').replace(/\.(\d+)Z$/, '-$1Z');
const directory = path.join(root, stamp);
const options = ['-i', key, '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=15',
  '-o', 'ServerAliveInterval=15', '-o', 'ServerAliveCountMax=2'];
if(process.env.FLORR_BACKUP_SSH_PROXY||target.proxyCommand)
  options.push('-o','ProxyCommand='+(process.env.FLORR_BACKUP_SSH_PROXY||target.proxyCommand));
const run = (exe, args, input) => cp.execFileSync(exe, args, {
  encoding: 'utf8', input, timeout: exe === 'scp' ? 600000 : 120000, maxBuffer: 1024 * 1024, windowsHide: true,
});
const ssh = (command, input) => run('ssh', [...options, host, command], input);
let remote;
let remoteDirectory;
const scheduled = process.env.FLORR_SCHEDULED_BACKUP === '1';
const storageMode = 'if test -e /var/lib/florrclone-shards/authority.snapshot || test -e /etc/florrclone/shard-game.json; then echo authority; else echo single; fi';
function productionMode() {
  const maintenance = 'if test -e /var/www/florrclone-maintenance/enabled || test -e /var/lib/florrclone/deploy.lock; then echo maintenance; else ';
  return ssh(scheduled ? maintenance + storageMode + '; fi' : storageMode).trim();
}
function confirmMode(mode) {
  if (scheduled) assert.equal(productionMode(), mode, 'Production mode changed or maintenance began during backup');
}
function authorityBackup(){
  // Execute the installed entry point: `node -` does not satisfy its
  // require.main guard and would silently return no snapshot metadata.
  const info=JSON.parse(ssh('/opt/florrclone/runtime/bin/node /opt/florrclone/current/online-snapshot.cjs'));
  assert.match(info.directory,/^\/var\/tmp\/florrclone-authority-backup-[a-zA-Z0-9_-]+$/);
  remoteDirectory=info.directory;
  const names=['authority.snapshot','authority.journal','authority.snapshot.lock','bootstrap.json'];
  assert.deepEqual(info.files.map(f=>f.name).sort(),[...names].sort(),'Incomplete authority backup group');
  fs.mkdirSync(directory,{recursive:true});
  for(const item of info.files){
    assert.match(item.sha256,/^[a-f0-9]{64}$/);
    const partial=path.join(directory,item.name+'.partial');
    // Compress the immutable checkpoint before transport; hash the original bytes.
    const compressed=remoteDirectory+'/'+item.name+'.transfer.gz';
    ssh('gzip -c -- '+remoteDirectory+'/'+item.name+' > '+compressed);
    const packed=partial+'.gz';
    run('scp',[...options,`${host}:${compressed}`,packed]);
    const bytes=require('node:zlib').gunzipSync(fs.readFileSync(packed));
    fs.writeFileSync(partial,bytes);
    fs.unlinkSync(packed);
    assert.equal(bytes.length,item.bytes,'Authority backup size mismatch');
    assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'),item.sha256,'Authority backup SHA256 mismatch');
    fs.renameSync(partial,path.join(directory,item.name));
  }
  const state=JSON.parse(fs.readFileSync(path.join(directory,'authority.snapshot'),'utf8'));
  assert.equal(state.sequence,info.sequence);assert.equal(Object.keys(state.accounts).length,info.accounts);
  confirmMode('authority');
  const metadata={timestampUtc:new Date().toISOString(),host,source:'shared-account-authority',
    gameServers:['154.219.105.161','154.219.105.158'],...info,directory:undefined,verified:true};
  fs.writeFileSync(path.join(directory,'backup-metadata.json'),JSON.stringify(metadata,null,2));
  console.log(JSON.stringify({status:'verified',backup:directory,...metadata}));
}
try {
  const mode=productionMode();
  if (mode === 'maintenance') {
    console.log(JSON.stringify({ status: 'skipped', reason: 'production-maintenance' }));
  } else {
  assert(['single','authority'].includes(mode),'Unknown production storage mode');
  if(mode==='authority')authorityBackup();
  else {
  // Open once: an autosave rename cannot replace the inode being read. The
  // private temporary snapshot is removed even if its download fails.
  const script = `import os,json,tempfile,hashlib,datetime
with open('${source}','rb') as f:
    raw=f.read()
    stat=os.fstat(f.fileno())
db=json.loads(raw)
assert isinstance(db.get('players'),dict), 'Missing player table'
fd,name=tempfile.mkstemp(prefix='florrclone-local-backup-',suffix='.json',dir='/var/tmp')
with os.fdopen(fd,'wb') as out:
    out.write(raw)
    out.flush()
    os.fsync(out.fileno())
print(json.dumps(dict(snapshot=name,sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw),accounts=len(db['players']),sourceModifiedUtc=datetime.datetime.fromtimestamp(stat.st_mtime,datetime.timezone.utc).isoformat())))
`;
  const info = JSON.parse(ssh('python3 -', script));
  assert.match(info.snapshot, /^\/var\/tmp\/florrclone-local-backup-[a-zA-Z0-9_-]+\.json$/);
  remote = info.snapshot;
  fs.mkdirSync(directory, { recursive: true });
  const partial = path.join(directory, 'inventory.json.partial');
  // JSON compresses well; SSH transport compression reduces slow-link timeouts
  // while the received file stays byte-for-byte identical for SHA256 checks.
  run('scp', ['-C', ...options, `${host}:${remote}`, partial]);
  const bytes = fs.readFileSync(partial);
  const sha256 = crypto.createHash('sha256').update(bytes).digest('hex');
  assert.equal(sha256, info.sha256, 'Downloaded snapshot SHA256 mismatch');
  assert.equal(bytes.length, info.bytes, 'Downloaded snapshot size mismatch');
  const db = JSON.parse(bytes);
  assert.equal(Object.keys(db.players).length, info.accounts);
  confirmMode('single');
  fs.renameSync(partial, path.join(directory, 'inventory.json'));
  const metadata = { timestampUtc: new Date().toISOString(), host, source,
    sourceModifiedUtc: info.sourceModifiedUtc, sha256, bytes: bytes.length,
    accounts: info.accounts, verified: true, kind: 'online-persisted-snapshot' };
  fs.writeFileSync(path.join(directory, 'backup-metadata.json'), JSON.stringify(metadata, null, 2));
  console.log(JSON.stringify({ status: 'verified', backup: directory, ...metadata }));
  }
  }
} catch (error) {
  console.error(`Player save backup failed: ${error.message}`);
  process.exitCode = 1;
} finally {
  if (remote) {
    try { ssh(`rm -- '${remote}'`); }
    catch (error) { console.error(`Remote snapshot cleanup failed: ${error.message}`); process.exitCode = 1; }
  }
  if(remoteDirectory){
    try{ssh('python3 -',`import shutil\nshutil.rmtree(${JSON.stringify(remoteDirectory)})\n`);}
    catch(error){console.error('Authority snapshot cleanup failed: '+error.message);process.exitCode=1;}
  }
}
