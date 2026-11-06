'use strict';
// Generate private deployment files outside the repository. No remote actions.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
function configurations(random=()=>crypto.randomBytes(32).toString('hex')) {
  const one=random(),two=random(),operator=random(),backup=random(),supervisorOne=random(),supervisorTwo=random();
  const keys=[one,two,operator,backup,supervisorOne,supervisorTwo];
  assert(new Set(keys).size===6,'Each role needs an independent key');
  for(const key of keys)assert(/^[a-f0-9]{64}$/.test(key),'Expected 256-bit hex keys');
  const endpoint='http://127.0.0.1:19090/rpc';
  return {
    'shard-host.json':{executable:'/opt/florrclone/current/shard_coordinator',
      stateBase:'/var/lib/florrclone-shards/authority',bootstrap:'/var/lib/florrclone-shards/bootstrap.json',port:19090,
      nodes:{one:{key:one,role:'game'},two:{key:two,role:'game'},operator:{key:operator,role:'supervisor'},backup:{key:backup,role:'backup'},
        'supervisor-one':{key:supervisorOne,role:'supervisor',scopeShard:'one'},
        'supervisor-two':{key:supervisorTwo,role:'supervisor',scopeShard:'two'}}},
    'shard-one.json':{endpoint,node:'one',key:one},
    'shard-two.json':{endpoint,node:'two',key:two},
    'shard-operator.json':{endpoint,node:'operator',key:operator},
    'shard-backup.json':{endpoint,node:'backup',key:backup},
    'shard-supervisor-one.json':{endpoint,node:'supervisor-one',shard:'one',key:supervisorOne},
    'shard-supervisor-two.json':{endpoint,node:'supervisor-two',shard:'two',key:supervisorTwo}
  };
}
function prepare(destination){
  assert(destination,'Supply a private output directory outside the repository');
  const root=path.resolve(__dirname,'../../..'),out=path.resolve(destination);
  const relative=path.relative(root,out);
  assert(relative.startsWith('..'+path.sep)||path.isAbsolute(relative),'Private configurations must stay outside the repository');
  fs.mkdirSync(out,{recursive:true,mode:0o700});
  const configs=configurations();
  for(const name of Object.keys(configs))assert(!fs.existsSync(path.join(out,name)),'Refusing to overwrite existing credentials: '+name);
  for(const [name,value]of Object.entries(configs))fs.writeFileSync(path.join(out,name),JSON.stringify(value,null,2)+'\n',{flag:'wx',mode:0o600});
  return Object.keys(configs).map(name=>path.join(out,name));
}
if(require.main===module){try{for(const name of prepare(process.argv[2]))console.log(name);}catch(e){console.error(e.message);process.exitCode=1;}}
module.exports={configurations,prepare};
