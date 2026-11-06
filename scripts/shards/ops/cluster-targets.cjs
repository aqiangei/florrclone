'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),cp=require('node:child_process');
const quote=s=>"'"+s.replace(/'/g,"'\\''")+"'";
function validate(config){
  assert(config&&['initial','upgrade'].includes(config.mode),'Explicit deployment mode required');
  if(config.councilManagerIds!==undefined){
    assert(Array.isArray(config.councilManagerIds),'Council manager IDs must be an array');
    assert(config.councilManagerIds.every(id=>typeof id==='string'&&id.length>0&&id.trim()===id),'Invalid council account ID');
    assert.equal(new Set(config.councilManagerIds).size,config.councilManagerIds.length,'Duplicate council account ID');
    if(config.councilManagerIds.length)assert.equal(config.mode,'initial','Council delegation bootstrap requires initial mode');
  }
  assert.match(config.release||'',/^\/opt\/florrclone\/releases\/[a-zA-Z0-9_-]+$/);
  assert.deepEqual(Object.keys(config.targets||{}).sort(),['one','two'],'Exactly two explicit targets required');
  for(const id of ['one','two']){
    const target=config.targets[id];
    assert.equal(target.id,id,'Target role mismatch');
    assert.match(target.host||'',/^[a-zA-Z0-9][a-zA-Z0-9.-]*$/,'Invalid host');
    assert.equal(target.host,target.expectedHost,'Unconfirmed deployment host');
    assert(Number.isInteger(target.sshPort)&&target.sshPort>0&&target.sshPort<=65535,'Explicit SSH port required');
    assert.equal(target.publicPort,8088,'This reviewed nginx template requires public 8088');
    assert.equal(target.backendPort,8788,'This reviewed service requires backend 8788');
    assert.match(target.machineId||'',/^[a-f0-9]{32}$/,'Verified machine identity required');
    assert(typeof target.key==='string'&&path.isAbsolute(target.key),'Explicit absolute SSH key path required');
    if(id==='two'&&config.mode==='initial'&&target.expectedRelease===null){} // freshly provisioned host
    else assert.match(target.expectedRelease||'',/^\/opt\/florrclone\/releases\/[a-zA-Z0-9_-]+$/,'Expected live release required');
  }
  assert.notEqual(config.targets.one.host,config.targets.two.host,'Two roles cannot target the same host');
  assert.notEqual(config.targets.one.machineId,config.targets.two.machineId,'Two roles cannot target the same machine');
  return config;
}
function transport(target){
  const options=['-i',target.key,'-o','BatchMode=yes','-o','StrictHostKeyChecking=yes',
    '-o','ConnectTimeout=15','-o','ServerAliveInterval=15','-o','ServerAliveCountMax=3'];
  const host='root@'+target.host;
  const run=(exe,args,input)=>cp.execFileSync(exe,args,{input,encoding:'utf8',timeout:180000,maxBuffer:1024*1024,windowsHide:true});
  return {host,
    ssh(command,input){return run('ssh',[...options,'-p',String(target.sshPort),host,command],input);},
    download(remote,local){return run('scp',['-C',...options,'-P',String(target.sshPort),host+':'+remote,local]);},
    upload(local,remote){return run('scp',[...options,'-P',String(target.sshPort),local,host+':'+remote]);}
  };
}
function inspect(config,connections,manifest){
  validate(config);
  assert(manifest&&manifest.files&&Object.keys(manifest.files).length,'Release hash manifest required');
  for(const required of ['server.js','server.wasm','bundle.js','bundle.wasm','bundle.js.bin','bundle.wasm.bin','index.html','council.html','deploy-runtime.cjs','shard-game-authority.cjs'])assert(Object.hasOwn(manifest.files,required),'Missing paired release artifact: '+required);
  for(const [name,hash]of Object.entries(manifest.files)){
    assert.match(name,/^[a-zA-Z0-9][a-zA-Z0-9_.-]*$/,'Unsafe artifact filename');assert.match(hash,/^[a-f0-9]{64}$/,'Invalid artifact hash');
  }
  const result={};
  for(const id of ['one','two']){
    const target=config.targets[id],remote=connections[id];
    assert.equal(remote.ssh('cat /etc/machine-id').trim(),target.machineId,id+' machine identity changed');
    const current=remote.ssh('if test -L /opt/florrclone/current; then readlink -f /opt/florrclone/current; elif test -e /opt/florrclone/current; then echo INVALID_CURRENT; else echo ABSENT; fi').trim();
    assert.equal(current,target.expectedRelease??'ABSENT',id+' live release changed');
    const lines=remote.ssh('sha256sum -- '+Object.keys(manifest.files).map(name=>quote(config.release+'/'+name)).join(' ')).trim().split(/\r?\n/);
    const found=new Map(lines.map(line=>{const m=/^([a-f0-9]{64})\s+(.+)$/.exec(line);assert(m,'Invalid remote artifact hash output');return [m[2],m[1]];}));
    for(const [name,hash]of Object.entries(manifest.files))assert.equal(found.get(config.release+'/'+name),hash,id+' release hash mismatch: '+name);
    if(manifest.files.shard_coordinator)remote.ssh('test -x '+quote(config.release+'/shard_coordinator'));
    result[id]={host:target.host,machineId:target.machineId,currentRelease:target.expectedRelease,files:Object.keys(manifest.files).length};
  }
  return result;
}
module.exports={validate,transport,inspect,quote};
