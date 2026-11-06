'use strict';
// Read-only checks against one live endpoint; no player account is created.
const assert=require('node:assert/strict');
async function check(base,protocol,hash){
  const url=new URL(base);assert(['http:','https:'].includes(url.protocol));
  assert(Number.isInteger(protocol)&&protocol>0&&protocol<=65535);
  assert(Number.isInteger(hash)&&hash>=0&&hash<=0xffffffff);
  const get=async(route,options)=>{
    const response=await fetch(new URL(route,url),{...options,signal:AbortSignal.timeout(15000)});
    assert(response.ok,route+' HTTP '+response.status);return response;
  };
  const health=await (await get('/api/health')).json();assert.equal(health.ok,true);
  for(const route of ['/','/council.html','/bundle.js','/bundle.wasm','/bundle.js.bin','/bundle.wasm.bin']){
    const response=await get(route);assert(Number(response.headers.get('content-length')||1)>0);await response.body.cancel();
  }
  const council=await (await get('/api/council',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:'view'})})).json();
  assert(!council.error&&Array.isArray(council.players)&&Array.isArray(council.polls),'Council unavailable');
  if(council.players.length)assert.equal(council.players.reduce((sum,row)=>sum+row.units,0),1000000,'Council total must be exactly 100%');
  const wsURL=new URL('/ws',url);wsURL.protocol=url.protocol==='https:'?'wss:':'ws:';
  await new Promise((resolve,reject)=>{
    const ws=new WebSocket(wsURL,'binary');ws.binaryType='arraybuffer';let buffer=Buffer.alloc(0),done=false;
    const finish=error=>{if(done)return;done=true;clearTimeout(timer);ws.close();error?reject(error):resolve();};
    const timer=setTimeout(()=>finish(Error('Handshake timeout')),15000);
    ws.addEventListener('open',()=>{const frame=Buffer.alloc(11);frame.writeUInt32LE(7,0);frame[4]=1;frame.writeUInt16LE(protocol,5);frame.writeUInt32LE(hash,7);ws.send(frame);});
    ws.addEventListener('message',event=>{try{
      buffer=Buffer.concat([buffer,Buffer.from(event.data)]);assert(buffer.length<=65536,'Unexpected handshake size');
      while(buffer.length>=4){const n=buffer.readUInt32LE(0);assert(n<=65536);if(buffer.length<4+n)return;const b=buffer.subarray(4,4+n);buffer=buffer.subarray(4+n);
        if(b[0]===1){assert(b.length>=4&&b[3]===1,'Protocol/content hash rejected');finish();return;}}
    }catch(error){finish(error);}});
    ws.addEventListener('error',()=>finish(Error('Handshake connection failed')));
    ws.addEventListener('close',()=>{if(!done)finish(Error('Handshake closed'));});
  });
  return {ok:true,protocol,contentHash:hash,councilPlayers:council.players.length,health};
}
if(require.main===module)check(process.argv[2],Number(process.argv[3]),Number(process.argv[4])).then(x=>console.log(JSON.stringify(x))).catch(e=>{console.error(e.message);process.exitCode=1;});
module.exports={check};
