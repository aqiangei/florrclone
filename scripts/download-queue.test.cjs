'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {DownloadQueue,createServer}=require('./download-queue.cjs');
test('FIFO, fixed concurrency, duplicate release, expiry and heartbeat',()=>{
  let now=0;const q=new DownloadQueue({limit:2,ttl:100,now:()=>now});
  const a=q.join(),b=q.join(),c=q.join(),d=q.join();
  assert.equal(a.state,'ready');assert.equal(b.state,'ready');assert.equal(c.state,'waiting');assert.equal(d.ahead,1);
  q.release(b.ticket);assert.equal(q.status(c.ticket).state,'ready');assert.equal(q.status(d.ticket).state,'waiting');
  q.release(b.ticket);assert(!q.authorized(b.ticket));assert(!q.authorized('fake'));
  now=90;q.status(c.ticket);q.status(d.ticket);now=101;q.sweep();
  assert(!q.authorized(a.ticket));assert(q.authorized(c.ticket));assert(q.authorized(d.ticket));
  now=201;q.sweep();assert.equal(q.rows.size,0);
});
test('HTTP admission enforces active tickets and releases slots',async()=>{
  const server=createServer({queue:new DownloadQueue({limit:1}),assetBase:'http://127.0.0.1:8088/client-assets/test/'});
  await new Promise(r=>server.listen(0,'127.0.0.1',r));const base='http://127.0.0.1:'+server.address().port;
  try{
    const join=async()=>await(await fetch(base+'/download-queue/join',{method:'POST'})).json();
    const a=await join(),b=await join();assert.equal(b.state,'waiting');
    const auth=id=>fetch(base+'/authorize',{headers:{'X-Original-URI':'/bundle.wasm.bin?ticket='+id}});
    assert.equal((await auth(a.ticket)).status,204);assert.equal((await auth(b.ticket)).status,403);
    await fetch(base+'/download-queue/release?ticket='+a.ticket,{method:'POST'});
    assert.equal((await auth(b.ticket)).status,204);
    assert.equal((await fetch(base+'/download-queue/status?ticket=bad')).status,410);
  }finally{await new Promise(r=>server.close(r));}
});
test('retrying an admission request keeps exactly one FIFO place',()=>{
  const q=new DownloadQueue({limit:1,max:2});
  const active=q.join();
  const request='1'.repeat(48);
  const waiting=q.join(request);
  assert.deepEqual(q.join(request),waiting);
  assert.equal(q.rows.size,2);
  q.release(active.ticket);
  assert.equal(q.join(request).state,'ready');
});
