'use strict';
// Run only against an isolated QA database containing qa_crafter.
const fs=require('fs'),path=require('path'),Module=require('module');
const file=path.resolve(__dirname,'smoke-auth.js');
let source=fs.readFileSync(file,'utf8').split('(async () => {')[0];
source+=`
const assert=require('assert');
(async()=>{const sock=await wsConnect();try{
let messages=[],buf=Buffer.alloc(0);
sock.on('data',c=>{buf=Buffer.concat([buf,c]);const p=parseFrames(buf);buf=p.rest;messages.push(...p.frames);});
const until=async fn=>{for(let i=0;i<250;i++){if(fn())return;await new Promise(r=>setTimeout(r,20));}throw Error('timeout');};
const hello=Buffer.alloc(7);hello[0]=ClientMessage.Hello;hello.writeUInt16LE(ver,1);hello.writeUInt32LE(CONTENT_HASH,3);send(sock,hello);
await until(()=>parseAuth(messages).welcome);assert(parseAuth(messages).welcome.accepted);
send(sock,Buffer.concat([Buffer.from([ClientMessage.Login]),encStr('qa_crafter'),encStr('isolated-test-password')]));
await until(()=>parseAuth(messages).auth);assert.equal(parseAuth(messages).auth.status,0);
const rose=Object.keys(JSON.parse(fs.readFileSync(path.join(DATA,'petals.json')))).sort().indexOf('rose');
const craft=Buffer.alloc(8);craft[0]=ClientMessage.Craft;craft.writeUInt16LE(rose,1);craft[3]=6;craft.writeUInt32LE(20000,4);send(sock,craft);
await until(()=>messages.some(m=>m[0]===ServerMessage.CraftResult));
const m=messages.find(m=>m[0]===ServerMessage.CraftResult);let at=1;const ok=m[at++];at+=3;
const made=m.readUInt32LE(at);at+=4;const returned=m[at++];const reason=decStr(m,at);at=reason.next;
const attempts=m.readUInt32LE(at),consumed=m.readUInt32LE(at+4);
assert(ok);assert(made>1);assert.equal(consumed,20000-returned);assert(attempts>=made);
await until(()=>messages.some(m=>m[0]===ServerMessage.Chat&&m.includes(Buffer.from('crafted x'+made))));
const chat=messages.find(m=>m[0]===ServerMessage.Chat&&m.includes(Buffer.from('crafted x'+made)));
assert(chat.includes(Buffer.from('consumed x'+consumed)));
console.log(JSON.stringify({pass:true,made,returned,consumed,attempts}));
}finally{sock.destroy();}})().catch(e=>{console.error(e);process.exitCode=1});`;
const mod=new Module(file);mod.filename=file;mod.paths=module.paths;mod._compile(source,file);
