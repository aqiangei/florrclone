'use strict';
const {Worker}=require('node:worker_threads');
const path=require('node:path');
class ShardRpc {
  constructor(config) {
    if(!config||typeof config.key!=='string'||config.key.length<32)throw Error('node credential required');
    // Bootstrap includes all accounts and retained history. Keep this separate
    // from the small recurring async transaction bridge below.
    this.buffer=new SharedArrayBuffer(128*1024*1024+8);
    this.header=new Int32Array(this.buffer,0,2);
    this.worker=new Worker(path.join(__dirname,'shard-rpc-worker.cjs'),{workerData:config});
    this.failed=false;
    this.worker.on('error',()=>{this.failed=true;});
    this.worker.unref();
  }
  call(request) {
    if(this.failed)return {ok:false,error:'transport_unknown'};
    Atomics.store(this.header,0,0);
    this.worker.postMessage({buffer:this.buffer,request});
    // Full startup snapshots can exceed the transaction deadline on a cold host.
    // This is a read-only bootstrap; uncertain writes retain their short deadline.
    if(Atomics.wait(this.header,0,0,request.op==='snapshot'?125000:15000)==='timed-out') {
      this.failed=true;this.worker.terminate();return {ok:false,error:'transport_unknown'};
    }
    try{return JSON.parse(Buffer.from(this.buffer,8,Atomics.load(this.header,1)).toString('utf8'));}
    catch{this.failed=true;return {ok:false,error:'transport_unknown'};}
  }
  close(){this.failed=true;return this.worker.terminate();}
}
// Gameplay commits use this queue. The blocking bridge above is only suitable
// for startup/offline tools, never the simulation's recurring save path.
class AsyncShardRpc {
  constructor(config) {
    if(!config||typeof config.key!=='string'||config.key.length<32)throw Error('node credential required');
    this.buffer=new SharedArrayBuffer(32*1024*1024+8);
    this.header=new Int32Array(this.buffer,0,2);
    this.queue=[];this.active=null;this.sequence=0;this.failed=false;
    this.worker=new Worker(path.join(__dirname,'shard-rpc-worker.cjs'),{workerData:config});
    this.worker.on('error',()=>this.fail());
    this.worker.on('message',({id})=>{
      if(!this.active||this.active.id!==id)return;
      const item=this.active;this.active=null;clearTimeout(item.timer);
      try{item.resolve(JSON.parse(Buffer.from(this.buffer,8,Atomics.load(this.header,1)).toString('utf8')));}
      catch{item.resolve({ok:false,error:'transport_unknown'});this.fail();}
      this.pump();
    });
    this.worker.unref();
  }
  call(request) {
    if(this.failed)return Promise.resolve({ok:false,error:'not_sent'});
    if(this.queue.length>=256)return Promise.resolve({ok:false,error:'busy'});
    return new Promise(resolve=>{this.queue.push({request,resolve,id:++this.sequence});this.pump();});
  }
  pump() {
    if(this.failed||this.active||!this.queue.length)return;
    const item=this.active=this.queue.shift();
    item.timer=setTimeout(()=>{this.fail();this.worker.terminate();},15000);
    Atomics.store(this.header,0,0);
    this.worker.postMessage({id:item.id,buffer:this.buffer,request:item.request});
  }
  fail() {
    this.failed=true;
    if(this.active){clearTimeout(this.active.timer);this.active.resolve({ok:false,error:'transport_unknown'});this.active=null;}
    for(const item of this.queue)item.resolve({ok:false,error:'not_sent'});
    this.queue=[];
  }
  close(){this.fail();return this.worker.terminate();}
}
module.exports={ShardRpc,AsyncShardRpc};
