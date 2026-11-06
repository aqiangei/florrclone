'use strict';
const {parentPort,workerData}=require('node:worker_threads');
const crypto=require('node:crypto');
const {endpoint,node,boot,key}=workerData;
const url=new URL(endpoint);
if(url.protocol!=='http:'||!['127.0.0.1','localhost','[::1]'].includes(url.hostname)||url.pathname!=='/rpc')
  throw Error('RPC requires a loopback SSH tunnel endpoint');
const sign=body=>crypto.createHmac('sha256',key).update(body).digest('hex');
parentPort.on('message',async({buffer,request,id})=>{
  const header=new Int32Array(buffer,0,2),out=new Uint8Array(buffer,8);
  let value;
  try {
    const nonce=crypto.randomBytes(16).toString('hex');
    const body=JSON.stringify({node,boot,at:Date.now(),nonce,request});
    const response=await fetch(endpoint,{method:'POST',body,headers:{'Content-Type':'application/json','X-Shard-Signature':sign(body)},signal:AbortSignal.timeout(request.op==='snapshot'?120000:12000)});
    const chunks=[];let bytes=0;
    for await(const chunk of response.body){bytes+=chunk.length;if(bytes>out.length)throw Error('oversize response');chunks.push(Buffer.from(chunk));}
    const text=Buffer.concat(chunks).toString('utf8'),signature=response.headers.get('x-shard-signature');
    if(!signature||!/^[a-f0-9]{64}$/.test(signature)||!crypto.timingSafeEqual(Buffer.from(signature,'hex'),Buffer.from(sign(text),'hex')))
      throw Error('unauthenticated response');
    const parsed=JSON.parse(text);if(parsed.nonce!==nonce)throw Error('response mismatch');
    value=response.ok?parsed.result:{ok:false,error:parsed.error||'transport_unknown'};
  } catch (_) {value={ok:false,error:'transport_unknown'};}
  let result=Buffer.from(JSON.stringify(value));
  if(result.length>out.length)result=Buffer.from('{"ok":false,"error":"transport_unknown"}');
  out.set(result);Atomics.store(header,1,result.length);Atomics.store(header,0,1);Atomics.notify(header,0);
  parentPort.postMessage({id});
});
