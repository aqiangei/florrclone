'use strict';
const {parentPort,workerData}=require('node:worker_threads');
const {once}=require('node:events');
const net=require('node:net'),cp=require('node:child_process'),assert=require('node:assert/strict');
const {quote,sshProxyOptions}=require('./cluster-targets.cjs');

// Use SSH command channels so acceptance works with TCP forwarding disabled.
// Both ends are bound to loopback, and the remote destination is fixed here.
async function run({target,remotePort,localPort}){
  assert(Number.isInteger(remotePort)&&remotePort>0&&remotePort<=65535,'Invalid relay port');
  const bridge="const peer=require('node:net').connect(+process.argv[1],'127.0.0.1');"
    +"process.stdin.pipe(peer);peer.pipe(process.stdout);"
    +"peer.on('error',error=>{console.error(error.message);process.exit(1)});"
    +"peer.on('close',()=>process.stdin.destroy());";
  const node = target.host === '154.219.105.164' && remotePort === 38788
    ? '/usr/bin/node' : '/opt/florrclone/runtime/bin/node';
  const command=node+' -e '+quote(bridge)+' '+remotePort;
  const args=['-T','-i',target.key,'-p',String(target.sshPort),'-o','BatchMode=yes',
    '-o','StrictHostKeyChecking=yes','-o','ConnectTimeout=10','-o','ServerAliveInterval=15',
    '-o','ServerAliveCountMax=3',...sshProxyOptions(target),'root@'+target.host,command];
  const sockets=new Set();
  const server=net.createServer(socket=>{
    sockets.add(socket);
    const child=cp.spawn('ssh',args,{windowsHide:true});
    child.stderr.pipe(process.stderr,{end:false});
    child.on('error',()=>socket.destroy());
    child.on('close',()=>socket.destroy());
    child.stdin.on('error',()=>socket.destroy());
    socket.on('error',()=>socket.destroy());
    socket.on('close',()=>{sockets.delete(socket);child.kill();});
    socket.pipe(child.stdin);
    child.stdout.pipe(socket);
  });
  server.listen(localPort,'127.0.0.1');
  await once(server,'listening');
  parentPort.postMessage({port:server.address().port});
  parentPort.once('message',()=>{
    for(const socket of sockets)socket.destroy();
    server.close(()=>parentPort.close());
  });
}
run(workerData).catch(error=>{
  parentPort.postMessage({error:error.message});
  parentPort.close();
});
