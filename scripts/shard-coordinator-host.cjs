'use strict';
// Transport only. Account state/transactions remain in the C++ coordinator.
// Bind loopback; use an authenticated SSH tunnel between hosts, never expose HTTP.
const fs=require('node:fs'), http=require('node:http'), crypto=require('node:crypto');
const {spawn}=require('node:child_process'), readline=require('node:readline');
const config=JSON.parse(fs.readFileSync(process.env.FLIX_SHARD_HOST_CONFIG,'utf8'));
if(!config.executable||!config.stateBase||!config.bootstrap||!config.nodes)throw Error('invalid coordinator configuration');
const configuredKeys=new Set();
for(const [id,node]of Object.entries(config.nodes)) {
  if(!/^[a-zA-Z0-9_.-]{1,128}$/.test(id)||typeof node.key!=='string'||node.key.length<32)throw Error('invalid node credential');
  if(node.role!==undefined&&!['game','supervisor','backup'].includes(node.role))throw Error('invalid configured node role');
  if(node.scopeShard!==undefined&&(node.role!=='supervisor'||!['one','two'].includes(node.scopeShard)))throw Error('invalid supervisor scope');
  if(configuredKeys.has(node.key))throw Error('node credentials must be distinct');configuredKeys.add(node.key);
}
const child=spawn(config.executable,[config.stateBase,config.bootstrap],{stdio:['pipe','pipe','inherit']});
const pending=new Map(), nonces=new Map();
let failed=false,stopping=false,childClosed=false,httpClosed=false,stopTimer;
function finishStop(){
  if(!childClosed||!httpClosed)return;
  clearTimeout(stopTimer);process.exitCode=failed?1:0;
}
function closeHttp(){
  server.close(()=>{httpClosed=true;finishStop();});
  server.closeIdleConnections();
}
function failPending(){
  for(const item of pending.values()){
    clearTimeout(item.timer);item.send(503,{nonce:item.nonce,error:'commit_outcome_unknown'});
  }
  pending.clear();
}
function fatal(){
  if(failed)return;
  failed=true;process.exitCode=1;failPending();
  if(!stopping){stopping=true;closeHttp();}
  if(!childClosed)child.kill('SIGTERM');
  clearTimeout(stopTimer);
  // Bound dead sockets/children without discarding queued normal-shutdown work.
  stopTimer=setTimeout(()=>{server.closeAllConnections();if(!childClosed)child.kill('SIGKILL');process.exit(1);},5000);
  finishStop();
}
function stop(){
  if(stopping)return;
  stopping=true;closeHttp();
  // All admitted writes precede EOF. Native coordinator drains stdin, fsyncs,
  // writes every reply and exits; 'close' fires after stdout is drained too.
  child.stdin.end();
  stopTimer=setTimeout(fatal,30000);
  finishStop();
}
function signature(key,body){return crypto.createHmac('sha256',key).update(body).digest('hex');}
function equal(a,b){return typeof a==='string'&&/^[a-f0-9]{64}$/.test(a)&&crypto.timingSafeEqual(Buffer.from(a,'hex'),Buffer.from(b,'hex'));}
readline.createInterface({input:child.stdout}).on('line',line=>{
  let reply;try{reply=JSON.parse(line);}catch{fatal();return;}
  const item=pending.get(reply.id);if(!item)return;
  pending.delete(reply.id);clearTimeout(item.timer);
  item.send(200,{nonce:item.nonce,result:reply.result});
});
child.on('close',(code,signal)=>{
  childClosed=true;
  if(!stopping||code!==0||signal!==null||pending.size)fatal();
  finishStop();
});
child.on('error',fatal);
child.stdin.on('error',fatal);
const server=http.createServer((req,res)=>{
  if(req.method!=='POST'||req.url!=='/rpc'){res.writeHead(404);res.end();return;}
  let size=0,chunks=[];
  req.on('data',chunk=>{size+=chunk.length;if(size>16*1024*1024){req.destroy();return;}chunks.push(chunk);});
  req.on('end',()=>{
    const bytes=Buffer.concat(chunks);let input;
    try{input=JSON.parse(bytes);}catch{res.writeHead(400);res.end();return;}
    if(!input||typeof input!=='object'||Array.isArray(input)){res.writeHead(400);res.end();return;}
    const node=Object.hasOwn(config.nodes,input.node)?config.nodes[input.node]:null;
    if(!node||!equal(req.headers['x-shard-signature'],signature(node.key,bytes))||
       !Number.isSafeInteger(input.at)||Math.abs(Date.now()-input.at)>30000||
       typeof input.nonce!=='string'||! /^[a-f0-9]{32}$/.test(input.nonce)) {
      res.writeHead(401);res.end();return;
    }
    const send=(status,value)=>{
      if(res.destroyed||res.writableEnded)return;
      const body=JSON.stringify(value);res.writeHead(status,{'Content-Type':'application/json',
        'Cache-Control':'no-store','X-Shard-Signature':signature(node.key,body)});res.end(body);
    };
    const now=Date.now();for(const [key,at]of nonces)if(now-at>60000)nonces.delete(key);
    if(node.role==='backup'&&input.request?.op!=='snapshot') {
      send(403,{nonce:input.nonce,error:'snapshot_only'});return;
    }
    if(node.role==='supervisor'&&input.request?.op!=='snapshot'&&
       (input.request?.op!=='recover_stopped'||(node.scopeShard&&input.request.stoppedShard!==node.scopeShard))) {
      send(403,{nonce:input.nonce,error:'supervisor_scope'});return;
    }
    const nonceKey=input.node+'/'+input.nonce;
    if(nonces.has(nonceKey)){send(409,{nonce:input.nonce,error:'replayed_envelope'});return;}
    if(nonces.size>=10000||pending.size>=256){send(503,{nonce:input.nonce,error:'busy'});return;}
    nonces.set(nonceKey,now);
    if(stopping||failed){send(503,{nonce:input.nonce,error:'coordinator_unavailable'});return;}
    const id=crypto.randomBytes(16).toString('hex');
    const timer=setTimeout(()=>{pending.delete(id);send(504,{nonce:input.nonce,error:'commit_outcome_unknown'});},input.request?.op==='snapshot'?120000:15000);
    pending.set(id,{send,timer,nonce:input.nonce});
    // Role comes exclusively from the operator's key configuration.
    child.stdin.write(JSON.stringify({id,node:input.node,boot:input.boot,
      supervisor:node.role==='supervisor',request:input.request})+'\n');
  });
});
server.listen(config.port||19090,'127.0.0.1');
server.on('error',fatal);
for(const signal of ['SIGINT','SIGTERM'])process.on(signal,stop);
