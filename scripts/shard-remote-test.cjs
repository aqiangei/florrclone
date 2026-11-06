'use strict';
// Runs the same synthetic integration test on the two real deployment hosts.
// SSH forwards expose only this test's private listeners to the local runner.
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const q=s=>"'"+String(s).replace(/'/g,"'\\''")+"'";
module.exports=function remoteTest(configPath,home){
  const c=JSON.parse(fs.readFileSync(configPath,'utf8'));
  assert.match(c.release,/^\/opt\/florrclone\/releases\/[a-zA-Z0-9_-]+$/);
  const stamp=path.basename(home).replace(/[^a-zA-Z0-9-]/g,'');
  const remoteHome='/var/tmp/'+stamp;
  const options=t=>['-i',t.key,'-p','22','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ExitOnForwardFailure=yes'];
  const ssh=(t,command)=>cp.execFileSync('ssh',[...options(t),'root@'+t.host,command],{encoding:'utf8',windowsHide:true,timeout:30000});
  const upload=(t,file,destination)=>cp.execFileSync('scp',['-i',t.key,'-o','BatchMode=yes','-o','StrictHostKeyChecking=yes',file,'root@'+t.host+':'+destination],{windowsHide:true,timeout:30000});
  for(const t of Object.values(c.targets)){
    assert.equal(ssh(t,'cat /etc/machine-id').trim(),t.machineId);
    ssh(t,'install -d -m 700 '+q(remoteHome));
  }
  return {
    launch(name,args,env,fd){
      const role=name==='two'?'two':'one',t=c.targets[role];
      const coordinator=name==='coordinator';
      const variable=coordinator?'FLIX_SHARD_HOST_CONFIG':'FLIX_SHARD_GAME_CONFIG';
      const value=JSON.parse(fs.readFileSync(env[variable],'utf8'));
      const destination=remoteHome+'/'+name+'.json';
      if(coordinator){
        value.executable=c.release+'/shard_coordinator';value.stateBase=remoteHome+'/authority';value.port=19090;
        upload(t,value.bootstrap,remoteHome+'/seed.json');value.bootstrap=remoteHome+'/seed.json';
      }else{
        value.endpoint='http://127.0.0.1:19090/rpc';
        ssh(t,'install -d -m 700 '+q(remoteHome+'/'+name+'/db'));
      }
      const local=path.join(home,name+'-remote.json');fs.writeFileSync(local,JSON.stringify(value));upload(t,local,destination);
      const unit='florr-test-'+stamp+'-'+name;
      const assignments=[variable+'='+destination,'FLIX_SHARD_DIAGNOSTICS=1'];
      if(!coordinator)assignments.push('FLIX_AUTHORITY_MODULE='+c.release+'/shard-game-authority.cjs');
      const command=coordinator?[c.release+'/shard-coordinator-host.cjs']:
        ['-r',c.release+'/deploy-runtime.cjs',c.release+'/server.js','--port',name==='one'?'3093':'3094','--db',remoteHome+'/'+name+'/db/forbidden-local.json','--bots','0','--web-root',c.release];
      const forward=coordinator?'19092:127.0.0.1:19090':name==='one'?'3093:127.0.0.1:3093':'3094:127.0.0.1:3094';
      const child=cp.spawn('ssh',[...options(t),'-L',forward,'root@'+t.host,
        'systemd-run --quiet --wait --pipe --collect --unit='+q(unit)+' --property='+q('WorkingDirectory='+c.release)+' /usr/bin/env '+assignments.map(q).join(' ')+' /opt/florrclone/runtime/bin/node '+command.map(q).join(' ')],
        {stdio:['ignore',fd,fd],windowsHide:true});
      child.kill=signal=>{
        cp.spawn('ssh',[...options(t),'root@'+t.host,signal==='SIGKILL'?'systemctl kill --signal=KILL '+q(unit):'systemctl stop '+q(unit)],{stdio:'ignore',windowsHide:true});return true;
      };
      return child;
    },
    noCache(){for(const role of ['one','two'])ssh(c.targets[role],'test ! -e '+q(remoteHome+'/'+role+'/db/forbidden-local.json'));},
  };
};
