'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const { execFileSync } = require('node:child_process');
const { setTimeout: delay } = require('node:timers/promises');

const host = '154.219.105.164';
const nginx = '/etc/nginx/conf.d/gamesrv-10000.conf';
const legacyDb = '/srv/gamesrv/app/inventory.json';
const legacy = 'gamesrv.service';
const service = 'florr-third.service';
const state = '/var/lib/florr-third';
const current = '/opt/florrclone-third/current';
const stage = '/var/tmp/flix-night-capacity-stage';
const unit = '/etc/systemd/system/' + service;
const port = 38788;
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const q = s => "'" + s.replaceAll("'", "'\\''") + "'";
const escape = s => String(s).replace(/[&<>"']/g, c => ({
  '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
})[c]);

function checkSave(bytes) {
  const db = JSON.parse(bytes);
  assert(db.users && db.players && Object.keys(db.users).length > 0, 'Missing player data');
  assert(!Object.values(db.users).some(u => u.admin || u.owner || u.adminTier), 'Replacement save has an administrator');
  return { accounts: Object.keys(db.users).length, players: Object.keys(db.players).length };
}

async function main([proof, save, saveHash, action = 'preflight']) {
  assert(['preflight', 'deploy'].includes(action), 'Expected preflight or deploy');
  assert.match(saveHash, /^[a-f0-9]{64}$/);
  const name = path.basename(proof, '.json');
  assert.match(name, /^cpp-[a-zA-Z0-9_-]+$/);
  const manifest = JSON.parse(fs.readFileSync(proof, 'utf8'));
  assert.match(manifest.archiveSha256, /^[a-f0-9]{64}$/);
  assert(Number.isInteger(manifest.protocol) && Number.isInteger(manifest.contentHash));
  const release = '/opt/florrclone/releases/' + name;
  const archive = stage + '/' + name + '.tar.gz';
  const saveBytes = fs.readFileSync(save);
  assert.equal(hash(saveBytes), saveHash, 'Replacement save checksum');
  const counts = checkSave(saveBytes);
  const target = 'root@' + host;
  const key = process.env.FLORR_THIRD_SSH_KEY;
  assert(key && fs.existsSync(key), 'Set FLORR_THIRD_SSH_KEY to the third host key path');
  const opts = ['-i', key, '-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=yes',
    '-o', 'ConnectTimeout=12', '-o', 'ServerAliveInterval=10', '-o', 'ServerAliveCountMax=3'];
  function run(program, args, input) {
    for (let attempt = 0; ; attempt++) {
      try {
        return execFileSync(program, [...opts, ...args], { input, maxBuffer: 4 * 1024 * 1024 });
      } catch (e) {
        const message = String(e.stderr || '').trim();
        const beforeLogin = /^(?:ssh: connect to host .*: (?:Connection timed out|No route to host)|Connection timed out during banner exchange)/m;
        if (attempt === 3 || !beforeLogin.test(message)) throw Error(message || e.message);
      }
    }
  }
  const ssh = (command, input) => run('ssh', [target, 'set -eu; ' + command], input);
  const scp = (from, to) => run('scp', [from, to]);
  const remoteHash = file => String(ssh('sha256sum ' + q(file))).split(/\s+/)[0];
  const checksums = Object.entries(manifest.files).map(([file, digest]) => {
    assert.match(file, /^[a-zA-Z0-9_.-]+$/);
    assert.match(digest, /^[a-f0-9]{64}$/);
    return digest + '  ' + file;
  }).join('\n') + '\n';
  const marker = state + '/maintenance/enabled';
  const oldNginx = String(ssh('cat ' + nginx)).replace(/\r\n/g, '\n');
  const gate = `
    error_page 418 =503 @florr_third_waiting;
    if (-f ${marker}) { return 418; }
    location @florr_third_waiting {
        root ${state}/maintenance;
        rewrite ^ /maintenance.html break;
        add_header Cache-Control "no-store" always;
        add_header Retry-After 120 always;
    }
`;
  assert.equal(oldNginx.split('    server_name _;').length, 2, 'Unexpected third vhost');
  assert(oldNginx.includes('proxy_pass http://127.0.0.1:10100;') && oldNginx.includes('/var/www/gamesrv-assets'));
  const waitingNginx = oldNginx.replace('    server_name _;', '    server_name _;\n' + gate);
  const nextNginx = waitingNginx.replaceAll('/var/www/gamesrv-assets', current)
    .replace('proxy_pass http://127.0.0.1:10100;', 'proxy_pass http://127.0.0.1:' + port + ';');
  const notes = JSON.parse(fs.readFileSync(path.join(path.dirname(proof), 'payload', 'release-notes.json'), 'utf8'));
  const waiting = fs.readFileSync(path.join(__dirname, 'maintenance.html'), 'utf8').replace(
    '<!-- RELEASE_NOTES -->', '<section><h2>' + escape(notes.title) + '</h2><ul>' +
    notes.changes.map(change => '<li>' + escape(change) + '</li>').join('') + '</ul></section>');
  assert(waiting.includes('花朵躲石头') && waiting.includes(escape(notes.title)));
  assert.equal(remoteHash(archive), manifest.archiveSha256, 'Remote archive checksum');
  ssh('systemctl is-active ' + legacy + '; test -f ' + legacyDb +
    '; test ! -e ' + current + '; test ! -e ' + unit +
    '; test -z "$(ss -H -ltn sport = :' + port + ')"' +
    '; nginx -t; /usr/bin/node --experimental-websocket -e "if (!globalThis.WebSocket) process.exit(1)"');
  console.log('PASS third legacy preflight', counts, manifest.archiveSha256);
  if (action === 'preflight') return;
  assert.equal(process.env.FLORR_THIRD_WAITING_ONLY, '1', 'Third-only waiting page cutover needs explicit approval');

  const stamp = new Date().toISOString().replace(/[-:]/g, '').replace(/\.\d+Z$/, 'Z');
  const local = path.join('D:/ashflor/backups/florrclone', stamp + '-third');
  const remote = '/var/backups/florr-third/' + stamp;
  const lock = stage + '/deploy.lock';
  const oldNginxHash = hash(oldNginx);
  const backend = 'http://127.0.0.1:' + port;
  const publicBase = 'http://127.0.0.1:10000';
  const probe = url => ssh('/usr/bin/node --experimental-websocket ' + q(release + '/release-check.cjs') +
    ' ' + q(url) + ' ' + manifest.protocol + ' ' + manifest.contentHash);
  const nginxInstall = file => ssh('install -m 0644 ' + q(file) + ' ' + q(nginx + '.next') +
    '; mv -T ' + q(nginx + '.next') + ' ' + nginx + '; nginx -t; systemctl reload nginx');
  let gateInstalled = false;
  let legacyStopped = false;
  let newInstalled = false;
  let completed = false;
  let restored = false;
  fs.mkdirSync(local, { recursive: true });
  ssh('mkdir ' + q(lock));
  try {
    assert.equal(remoteHash(nginx), oldNginxHash, 'Legacy vhost changed during preflight');
    ssh('install -d -m 0700 ' + q(remote) + '; cp -p ' + nginx + ' ' + q(remote + '/nginx.old') +
      '; install -d -m 0755 ' + state + '/maintenance /opt/florrclone/releases /opt/florrclone-third');
    if (String(ssh('test ! -e ' + q(release) + ' || echo exists')).trim() !== 'exists') {
      ssh('d=$(mktemp -d ' + q(release + '.stage.XXXXXX') + '); chmod 0755 "$d"; tar -xzf ' +
        q(archive) + ' -C "$d"; mv -T "$d" ' + q(release));
    }
    ssh('cd ' + q(release) + '; sha256sum -c -; runuser -u www-data -- test -r bundle.wasm.bin', checksums);
    ssh('cat > ' + q(remote + '/nginx.waiting'), waitingNginx);
    ssh('cat > ' + q(remote + '/nginx.next'), nextNginx);
    ssh('cat > ' + q(state + '/maintenance/maintenance.html') + '; chmod 0644 ' +
      q(state + '/maintenance/maintenance.html'), waiting);
    gateInstalled = true;
    ssh('touch ' + q(marker));
    nginxInstall(remote + '/nginx.waiting');
    const verifyWaiting = () => {
      const response = String(ssh('for p in / /api/health /bundle.wasm.bin; do ' +
        'test "$(curl --max-time 10 -sS -o /dev/null -w "%{http_code}" ' + publicBase + '$p)" = 503; done; curl -sS ' + publicBase + '/'));
      assert(response.includes('花朵躲石头') && response.includes(escape(notes.title)), 'Waiting page verification failed');
    };
    verifyWaiting();
    console.log('Third maintenance page verified; 120-second countdown started');
    await delay(120_000);
    verifyWaiting();
    legacyStopped = true;
    ssh('systemctl stop ' + legacy + '; test "$(systemctl show ' + legacy + ' -p MainPID --value)" = 0' +
      '; test "$(systemctl show ' + legacy + ' -p Result --value)" = success' +
      '; install -m 0600 ' + legacyDb + ' ' + q(remote + '/inventory.legacy.json'));
    const backup = path.join(local, 'inventory.json');
    scp(target + ':' + remote + '/inventory.legacy.json', backup);
    const backupBytes = fs.readFileSync(backup);
    const backupHash = hash(backupBytes);
    assert.equal(backupHash, remoteHash(remote + '/inventory.legacy.json'), 'Stopped save checksum mismatch');
    JSON.parse(backupBytes);
    fs.writeFileSync(path.join(local, 'backup-metadata.json'), JSON.stringify({
      stamp, host, legacy, release, backupHash, replacementHash: saveHash, counts
    }, null, 2));
    console.log('Verified stopped third save on local computer', backup, backupHash);

    scp(save, target + ':' + remote + '/inventory.approved.json');
    assert.equal(remoteHash(remote + '/inventory.approved.json'), saveHash);
    newInstalled = true;
    ssh('getent passwd florr-third >/dev/null || useradd --system --user-group --home-dir ' + state +
      ' --shell /usr/sbin/nologin florr-third; install -d -m 0750 -o florr-third -g florr-third ' + state + '/cpp-state' +
      '; install -m 0600 -o florr-third -g florr-third ' + q(remote + '/inventory.approved.json') + ' ' + state + '/cpp-state/inventory.json' +
      '; ln -s ' + q(release) + ' ' + current);
    ssh('cat > ' + unit, `[Unit]
Description=Florrclone third game server
After=network.target

[Service]
User=florr-third
Group=florr-third
WorkingDirectory=${state}/cpp-state
ExecStart=/usr/bin/node -r ${current}/deploy-runtime.cjs ${current}/server.js --port ${port} --db ${state}/cpp-state/inventory.json --web-root ${current} --bots 0 --background-save --tick-probe
Restart=on-failure
TimeoutStopSec=90
UMask=0077
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
ReadWritePaths=${state}/cpp-state

[Install]
WantedBy=multi-user.target
`);
    ssh('systemctl daemon-reload; systemctl start ' + service +
      '; for i in $(seq 1 45); do if curl -fsS ' + backend + '/api/health; then exit 0; fi; sleep 1; done; exit 1');
    console.log(String(probe(backend)).trim());
    const persisted = ssh('for i in $(seq 1 15); do if journalctl -u ' + service +
      ' -b --no-pager -n 50 | grep -qF "[persist-worker] saved"; then cat ' +
      state + '/cpp-state/inventory.json; exit 0; fi; sleep 1; done; exit 1');
    assert.deepEqual(checkSave(persisted), counts);
    nginxInstall(remote + '/nginx.next');
    verifyWaiting();
    ssh('systemctl enable ' + service + '; rm -f ' + q(marker));
    console.log(String(probe(publicBase)).trim());
    ssh('test "$(curl -fsS -r 0-63 -o /dev/null -w "%{http_code}" ' + publicBase + '/bundle.wasm.bin)" = 206' +
      '; test "$(curl -fsS ' + publicBase + '/bundle.wasm.bin | sha256sum | cut -d " " -f 1)" = ' +
      q(manifest.files['bundle.wasm.bin']) + '; systemctl disable ' + legacy);
    completed = true;
    console.log('PASS third cutover', release);
  } catch (error) {
    if (gateInstalled) {
      try {
        ssh('touch ' + q(marker));
        if (newInstalled) ssh('if test -f ' + unit + '; then systemctl disable --now ' + service +
          '; fi; rm -f ' + unit + ' ' + current + '; systemctl daemon-reload');
        if (legacyStopped) ssh('systemctl enable --now ' + legacy + '; systemctl is-active ' + legacy +
          '; for i in $(seq 1 30); do if curl -sS --max-time 3 http://127.0.0.1:10100/ -o /dev/null; then exit 0; fi; sleep 1; done; exit 1');
        nginxInstall(remote + '/nginx.old');
        ssh('rm -f ' + q(marker));
        restored = true;
        console.error('Third legacy service restored');
      } catch (rollback) {
        throw Error('Third deployment and rollback need operator attention; backup ' + local +
          '\n' + error.message + '\n' + rollback.message);
      }
    }
    throw error;
  } finally {
    if (completed || restored || !gateInstalled) ssh('rmdir ' + q(lock));
  }
}

module.exports = { main };
