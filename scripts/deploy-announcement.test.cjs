'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const {connect,countdown}=require('./deploy-announcement.cjs');
function string(s){const b=Buffer.from(s),h=Buffer.alloc(2);h.writeUInt16LE(b.length);return Buffer.concat([h,b]);}
function message(b){const h=Buffer.alloc(4);h.writeUInt32LE(b.length);return Buffer.concat([h,b]);}
class FakeSocket extends EventTarget {
  static allow=true;
  constructor(){super();queueMicrotask(()=>this.dispatchEvent(new Event('open')));}
  reply(b){const e=new Event('message');e.data=message(b);queueMicrotask(()=>this.dispatchEvent(e));}
  send(frame){const b=frame.subarray(4);
    if(b[0]===1){assert.equal(b.readUInt16LE(1),92);assert.equal(b.readUInt32LE(3),123);this.reply(Buffer.from([1,92,0,1,0,0]));}
    else if(b[0]===3)this.reply(Buffer.concat([Buffer.from([2,0]),string('token'),string('operator'),string('')]));
    else {assert.equal(b[0],12);const text=b.subarray(3).toString();assert(text.startsWith('/admin announce '));
      if(!FakeSocket.allow){this.close();return;}
      this.reply(Buffer.concat([Buffer.from([6,7]),string('operator'),string(text.slice(16)),Buffer.alloc(4)]));
    }
  }
  close(){queueMicrotask(()=>this.dispatchEvent(new Event('close')));}
}
test('announcements are confirmed before waiting; two minutes precede shutdown',async()=>{
 const c=await connect({url:'ws://localhost/ws',username:'Operator',password:'test',protocolVersion:92,contentHash:123},FakeSocket);
 const order=[];await countdown(async text=>{assert(text.includes("预计几十秒后恢复"));assert(!text.includes("几分钟"));await c.broadcast(text);order.push(text.match(/(\d+)秒/)[1]);},{sleep:async ms=>order.push(ms),log:()=>{}});c.close();
 assert.deepEqual(order,['120',60000,'60',30000,'30',20000,'10',10000]);
});
test('failed broadcast aborts before countdown or shutdown',async()=>{
 FakeSocket.allow=false;const c=await connect({url:'ws://localhost/ws',username:'operator',password:'test',protocolVersion:92,contentHash:123},FakeSocket);
 let waited=false;await assert.rejects(countdown(t=>c.broadcast(t),{sleep:async()=>{waited=true;},log:()=>{}}));assert.equal(waited,false);c.close();FakeSocket.allow=true;
});
