'use strict';
const fs = require('node:fs');
const assert = require('node:assert/strict');
function readConfig(file) {
  assert(file, 'Set FLORR_DEPLOY_ADMIN_FILE to a private administrator credentials JSON file before deployment');
  const c = JSON.parse(fs.readFileSync(file, 'utf8'));
  assert(typeof c.username === 'string' && c.username && typeof c.password === 'string' && c.password, 'Deployment administrator credentials are incomplete');
  assert(Number.isInteger(c.protocolVersion) && c.protocolVersion > 0 && c.protocolVersion <= 65535, 'Supply the CURRENT LIVE protocolVersion');
  assert(Number.isInteger(c.contentHash) && c.contentHash >= 0 && c.contentHash <= 0xffffffff, 'Supply the CURRENT LIVE contentHash');
  const u = new URL(c.url || 'ws://154.219.105.161:8088/ws');
  assert(['ws:', 'wss:'].includes(u.protocol), 'Invalid announcement WebSocket URL');
  return {...c, url: u.href};
}
function str(text) {
  const b = Buffer.from(text); const n = Buffer.alloc(2); n.writeUInt16LE(b.length); return Buffer.concat([n,b]);
}
function readString(b, at) {
  if(at+2>b.length) throw Error('Invalid announcement response');
  const n=b.readUInt16LE(at); at+=2;
  if(at+n>b.length) throw Error('Invalid announcement response');
  return [b.toString('utf8',at,at+n),at+n];
}
async function connect(c, WebSocketClass = globalThis.WebSocket) {
  assert(WebSocketClass, 'Deployment notices require Node with WebSocket support');
  const ws = new WebSocketClass(c.url, 'binary'); ws.binaryType='arraybuffer';
  let pending=null, closed=false, buffer=Buffer.alloc(0);
  const rejectPending=()=>{closed=true;if(pending){clearTimeout(pending.timer);pending.reject(Error('Announcement connection closed'));pending=null;}};
  ws.addEventListener('close',rejectPending);ws.addEventListener('error',rejectPending);
  ws.addEventListener('message',event=>{
    try {
      buffer=Buffer.concat([buffer,Buffer.from(event.data)]);
      while(buffer.length>=4){const n=buffer.readUInt32LE(0);if(n>16*1024*1024)throw Error('Invalid announcement response');if(buffer.length<4+n)break;
        const b=buffer.subarray(4,4+n);buffer=buffer.subarray(4+n);
        if(pending && pending.accept(b)){const p=pending;pending=null;clearTimeout(p.timer);p.resolve(b);}
      }
    } catch {if(pending){clearTimeout(pending.timer);pending.reject(Error('Invalid announcement response'));pending=null;}ws.close();}
  });
  function request(body,accept) {
    return new Promise((resolve,reject)=>{
      if(closed)return reject(Error('Announcement connection closed'));
      const timer=setTimeout(()=>{pending=null;reject(Error('No server confirmation of maintenance announcement'));},10000);
      pending={resolve,reject,accept,timer};const header=Buffer.alloc(4);header.writeUInt32LE(body.length);
      ws.send(Buffer.concat([header,body]));
    });
  }
  try {
    await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('Announcement connection timed out')),10000);
      ws.addEventListener('open',()=>{clearTimeout(timer);resolve();},{once:true});
      ws.addEventListener('error',()=>{clearTimeout(timer);reject(Error('Announcement connection failed'));},{once:true});
    });
    const hello=Buffer.alloc(7);hello[0]=1;hello.writeUInt16LE(c.protocolVersion,1);hello.writeUInt32LE(c.contentHash,3);
    const welcome=await request(hello,b=>b[0]===1);
    assert(welcome.length>=4 && welcome[3]===1,'Live protocol/content mismatch; no shutdown performed');
    const auth=await request(Buffer.concat([Buffer.from([3]),str(c.username),str(c.password)]),b=>b[0]===2);
    assert(auth[1]===0,'Deployment administrator login rejected');
    const [,tokenEnd]=readString(auth,2);
    const [authenticatedName]=readString(auth,tokenEnd);
    return {
      async broadcast(message) {
        assert(Buffer.byteLength(message)<=120,'Announcement exceeds server limit');
        await request(Buffer.concat([Buffer.from([12]),str('/admin announce '+message)]),b=>{
          if(b[0]!==6 || b[1]!==7)return false;
          const [author,at]=readString(b,2);const [text]=readString(b,at);
          return author===authenticatedName && text===message;
        });
      },
      close(){ws.close();}
    };
  }catch(error){ws.close();throw error;}
}
async function countdown(broadcast,{sleep=ms=>new Promise(r=>setTimeout(r,ms)),log=console.log}={}) {
  for(const [seconds,wait] of [[120,60],[60,30],[30,20],[10,10]]) {
    const message=`【维护通知】${seconds}秒后停服更新，请提前结束战斗；预计几十秒后恢复，完成检查后开放。`;
    await broadcast(message);
    log(`已确认全服广播：${seconds}秒后维护`);
    await sleep(wait*1000);
  }
}
module.exports={readConfig,connect,countdown};
