'use strict';
// Real WebSocket invitation flow against an isolated server/database only.
const fs=require('fs'),path=require('path'),Module=require('module');
const file=path.resolve(__dirname,'smoke-auth.js');let source=fs.readFileSync(file,'utf8').split('(async () => {')[0];
source+=`
const assert=require('assert');const clients=[];const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function until(fn){for(let i=0;i<200;i++){if(fn())return;await pause(20);}throw Error('timed out');}
async function connect(name){const sock=await wsConnect();const c={sock,name,buf:Buffer.alloc(0),messages:[],squad:null,invite:null};clients.push(c);
sock.on('data',chunk=>{c.buf=Buffer.concat([c.buf,chunk]);const p=parseFrames(c.buf);c.buf=p.rest;for(const m of p.frames){c.messages.push(m);if(m[0]===ServerMessage.SquadInviteReceived)c.invite=decStr(m,1).value;if(m[0]===ServerMessage.SquadUpdate){if(!m[1]){c.squad=null;continue;}let id=decStr(m,2);c.squad={id:id.value,count:m[id.next+1]};}}});
const hello=Buffer.alloc(7);hello[0]=ClientMessage.Hello;hello.writeUInt16LE(ver,1);hello.writeUInt32LE(CONTENT_HASH,3);send(sock,hello);await until(()=>c.messages.some(m=>m[0]===ServerMessage.Welcome));assert(parseAuth(c.messages).welcome.accepted);
send(sock,Buffer.concat([Buffer.from([ClientMessage.Register]),encStr(name),encStr('isolated-test-password')]));await until(()=>parseAuth(c.messages).auth);assert.equal(parseAuth(c.messages).auth.status,0);return c;}
const chat=(c,s)=>send(c.sock,Buffer.concat([Buffer.from([ClientMessage.Chat]),encStr(s)]));
(async()=>{try{const suffix=crypto.randomBytes(3).toString('hex');const a=await connect('qa_a_'+suffix),b=await connect('qa_b_'+suffix),c=await connect('qa_c_'+suffix),d=await connect('qa_d_'+suffix);
chat(a,'/squad-invite '+b.name);await until(()=>b.invite===a.name);assert(a.squad);chat(b,'/squad-accept');await until(()=>b.squad?.id===a.squad.id&&a.squad.count===2);
chat(c,'/squad-invite '+d.name);await until(()=>d.invite===c.name);chat(d,'/squad-accept');await until(()=>d.squad?.id===c.squad.id&&c.squad.count===2);const old=d.squad.id;
d.invite=null;chat(b,'/squad-invite '+d.name);await until(()=>d.invite===b.name);assert.equal(d.squad.id,old);chat(d,'/squad-decline');await pause(200);assert.equal(d.squad.id,old);
d.invite=null;chat(b,'/squad-invite '+d.name);await until(()=>d.invite===b.name);chat(d,'/squad-accept');await until(()=>d.squad?.id===a.squad.id&&a.squad.count===3&&c.squad.count===1);console.log('PASS real WebSocket: auto-create, non-leader invitation, in-party invitation popup event, decline keeps party, accept switches and updates both parties');}finally{for(const c of clients)c.sock.destroy();}})().catch(e=>{console.error(e);process.exitCode=1});`;
const mod=new Module(file);mod.filename=file;mod.paths=module.paths;mod._compile(source,file);
