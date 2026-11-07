'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),crypto=require('node:crypto');
const {collect}=require('./stopped-backup.cjs');
function fixture(t,{running=false,corrupt=false,changed=false}={}){
  const directory=fs.mkdtempSync(path.join(os.tmpdir(),'florr-stopped-backup-'));
  t.after(()=>{
    const resolved=path.resolve(directory),parent=path.resolve(os.tmpdir());
    assert.equal(path.dirname(resolved),parent);assert(path.basename(resolved).startsWith('florr-stopped-backup-'));
    fs.rmSync(resolved,{recursive:true,force:true});
  });
  const inventory=Buffer.from(JSON.stringify({users:{},players:{existing:{level:200}}}));
  const hash=crypto.createHash('sha256').update(inventory).digest('hex');let downloads=0;
  const transport={ssh(command){
    if(command.startsWith('systemctl'))return 'LoadState=loaded\nActiveState='+(running?'active':'inactive')+'\nSubState=dead\nResult=success\nExecMainStatus=0\n';
    if(command.startsWith('sha256sum'))return (changed&&command.includes('/cpp-state/')?'0'.repeat(64):hash)+'  snapshot\n';
    return '';
  },download(remote,local){downloads++;fs.writeFileSync(local,corrupt?'broken':inventory);}};
  return {options:{mode:'initial',stamp:'20261105T070000Z',directory,one:transport,two:transport},directory,get downloads(){return downloads;}};
}
test('a running shard prevents stopped backup and every download',t=>{
  const f=fixture(t,{running:true});assert.throws(()=>collect(f.options),/must be stopped/);assert.equal(f.downloads,0);
  assert(!fs.existsSync(path.join(f.directory,'verified-backup.json')));
});
test('corrupt transfer or source mutation never emits a verified manifest',t=>{
  for(const options of [{corrupt:true},{changed:true}]){
    const f=fixture(t,options);assert.throws(()=>collect(f.options),/mismatch|changed during/);
    assert(!fs.existsSync(path.join(f.directory,'verified-backup.json')));
  }
});
test('stopped latest inventory is downloaded unchanged and verified before completion',t=>{
  const f=fixture(t);const report=collect(f.options);assert.equal(report.bootstrapAccounts,1);assert.equal(f.downloads,1);
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(f.directory,'verified-backup.json'))),report);
  assert.equal(JSON.parse(fs.readFileSync(path.join(f.directory,'inventory.json'))).players.existing.level,200);
});
test('initial migration downloads the latest inventory from the active standalone host',t=>{
  const f=fixture(t);
  f.options.initialSource='two';
  f.options.one={...f.options.one,download(){assert.fail('Downloaded the retired standalone save');}};
  const report=collect(f.options);
  assert.equal(report.bootstrapAccounts,1);
  assert.equal(f.downloads,1);
});
