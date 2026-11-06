'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs');
const {configurations,prepare}=require('./prepare-configs.cjs');
test('game nodes have separate keys and no supervisor credential',()=>{
 const c=configurations(),h=c['shard-host.json'];
 assert.equal(h.nodes.one.key,c['shard-one.json'].key);assert.equal(h.nodes.two.key,c['shard-two.json'].key);
 assert.equal(h.nodes.operator.role,'supervisor');assert.equal(h.nodes.one.role,'game');assert.equal(h.nodes.two.role,'game');
 assert.equal(new Set(Object.values(h.nodes).map(n=>n.key)).size,6);
 assert.equal(h.nodes.backup.role,'backup');assert.equal(h.nodes.backup.key,c['shard-backup.json'].key);
 for(const shard of ['one','two']){const n=h.nodes['supervisor-'+shard];assert.equal(n.role,'supervisor');assert.equal(n.scopeShard,shard);assert.equal(n.key,c['shard-supervisor-'+shard+'.json'].key);}
 for(const n of ['one','two']){assert.equal(c['shard-'+n+'.json'].endpoint,'http://127.0.0.1:19090/rpc');assert(!JSON.stringify(c['shard-'+n+'.json']).includes(h.nodes.operator.key));}
});
test('configuration generation cannot write secrets inside the repository',()=>{
 assert.throws(()=>prepare(__dirname+'/private'),/outside the repository/);
 assert.throws(()=>configurations(()=> 'a'.repeat(64)),/independent key/);
});
test('coordinator identity is separate from game identity and writes only its state directory',()=>{
 const s=fs.readFileSync(__dirname+'/florr-shard-coordinator.service','utf8');
 assert(s.includes('User=florr-coordinator'));assert(s.includes('ProtectSystem=strict'));
 assert(s.includes('ReadWritePaths=/var/lib/florrclone-shards'));assert(s.includes('UMask=0077'));
});
test('direct tunnel is loopback-only, validates host keys, and restricts its target',()=>{
 const t=fs.readFileSync(__dirname+'/florr-shard-tunnel.service','utf8'),c=fs.readFileSync(__dirname+'/shard-ssh.conf','utf8');
 assert(t.includes('-L 127.0.0.1:19090:127.0.0.1:19090'));assert(t.includes('ExitOnForwardFailure=yes'));
 assert(c.includes('StrictHostKeyChecking yes'));assert(c.includes('ForwardAgent no'));assert(!c.includes('ProxyJump'));assert(!c.includes('154.8.157.208'));
 for(const [file,target]of [['sshd-coordinator.conf','127.0.0.1:19090']]){
  const s=fs.readFileSync(__dirname+'/'+file,'utf8');assert(s.includes('PermitOpen '+target));assert(s.includes('PermitListen none'));assert(s.includes('ForceCommand /bin/false'));assert(s.includes('AllowTcpForwarding local'));
 }
});
