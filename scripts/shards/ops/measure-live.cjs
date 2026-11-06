'use strict';
// Observe server telemetry while a dedicated operator account stays in lobby.
// Never logs credentials, session tokens, player names or chat contents.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
function ids(name){
  const text=fs.readFileSync(path.resolve(__dirname,'../../../cpp/shared/net/protocol.h'),'utf8').replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
  const body=text.match(new RegExp('enum class '+name+'[^\\{]*\\{([\\s\\S]*?)\\};'))?.[1];assert(body,'Protocol enum missing');
  let next=0;const out={};for(const part of body.split(',')){const m=/^\s*(\w+)(?:\s*=\s*(\d+))?\s*$/.exec(part);if(!m){assert(!part.trim(),'Unsupported protocol enum');continue;}if(m[2])next=Number(m[2]);out[m[1]]=next++;}return out;
}
const str=text=>{const body=Buffer.from(text),head=Buffer.alloc(2);assert(body.length<=65535);head.writeUInt16LE(body.length);return Buffer.concat([head,body]);};
const stats=values=>{if(!values.length)return null;const sorted=[...values].sort((a,b)=>a-b);return {count:values.length,mean:values.reduce((a,b)=>a+b,0)/values.length,p50:sorted[Math.floor((sorted.length-1)*.5)],p95:sorted[Math.floor((sorted.length-1)*.95)],max:sorted.at(-1)};};
async function measure(config,seconds=30){
  assert(Number.isInteger(seconds)&&seconds>=10&&seconds<=120);const C=ids('ClientMessage'),S=ids('ServerMessage');
  const url=new URL(config.url);assert(['ws:','wss:'].includes(url.protocol)&&!url.username&&!url.password&&!url.search&&!url.hash);
  const samples=[],pings=[],roster=[],pending=new Map();let bytes=0,authenticated=false,buffer=Buffer.alloc(0),began,interval,finished=false;
  const ws=new WebSocket(url,'binary');ws.binaryType='arraybuffer';
  return new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>finish(Error('Telemetry probe timeout')),seconds*1000+20000);
    function finish(error){if(finished)return;finished=true;clearTimeout(timer);clearInterval(interval);ws.close();
      if(!error&&samples.length<Math.floor(seconds*.7))error=Error('Insufficient live telemetry samples');
      if(error){reject(error);return;}
      resolve({endpoint:url.href,startedAt:began,finishedAt:new Date().toISOString(),durationSeconds:seconds,
        tickAverageMs:stats(samples.map(s=>s.avg)),tickPeakMs:stats(samples.map(s=>s.max)),rttMs:stats(pings),
        residentBytes:stats(samples.map(s=>s.resident)),heapBytes:stats(samples.map(s=>s.heap)),lobbyReceivedBytes:bytes,
        rosterEntries:roster,rosterCap:64,notes:'Lobby observer; roster may include bots and is capped at 64. Different player populations are not a controlled performance comparison.'});
    }
    function send(body){const head=Buffer.alloc(4);head.writeUInt32LE(body.length);ws.send(Buffer.concat([head,body]));}
    ws.addEventListener('open',()=>{const body=Buffer.alloc(7);body[0]=C.Hello;body.writeUInt16LE(config.protocolVersion,1);body.writeUInt32LE(config.contentHash,3);send(body);});
    ws.addEventListener('message',event=>{try{
      const incoming=Buffer.from(event.data);if(authenticated)bytes+=incoming.length;buffer=Buffer.concat([buffer,incoming]);
      while(buffer.length>=4){const n=buffer.readUInt32LE(0);assert(n<=16*1024*1024);if(buffer.length<4+n)break;const b=buffer.subarray(4,4+n);buffer=buffer.subarray(4+n);
        if(b[0]===S.Welcome){assert(b.length>=4&&b[3]===1,'Live protocol/content mismatch');send(Buffer.concat([Buffer.from([C.Login]),str(config.username),str(config.password)]));}
        else if(b[0]===S.AuthResult){assert.equal(b[1],0,'Dedicated observer login rejected');assert(!authenticated);authenticated=true;began=new Date().toISOString();
          const start=performance.now();interval=setInterval(()=>{if(performance.now()-start>=seconds*1000){finish();return;}const stamp=BigInt(Date.now()),body=Buffer.alloc(9);body[0]=C.Ping;body.writeBigUInt64LE(stamp,1);pending.set(stamp.toString(),performance.now());send(body);},1000);
        }else if(authenticated&&b[0]===S.DebugStats){assert.equal(b.length,25);samples.push({resident:b.readDoubleLE(1),heap:b.readDoubleLE(9),avg:b.readFloatLE(17),max:b.readFloatLE(21)});}
        else if(authenticated&&b[0]===S.Pong&&b.length>=17){const key=b.readBigUInt64LE(1).toString();if(pending.has(key)){pings.push(performance.now()-pending.get(key));pending.delete(key);}}
        else if(authenticated&&b[0]===S.OnlineRoster&&b.length>=2){if(roster.at(-1)!==b[1])roster.push(b[1]);}
      }
    }catch(error){finish(error);}});
    ws.addEventListener('error',()=>finish(Error('Telemetry connection failed')));
    ws.addEventListener('close',()=>{if(!finished)finish(Error('Telemetry connection closed early'));});
  });
}
if(require.main===module){const config=require('../../deploy-announcement.cjs').readConfig(process.argv[2]);measure(config,Number(process.argv[3]||30)).then(result=>{
  if(process.argv[4])fs.writeFileSync(process.argv[4],JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result));
}).catch(error=>{console.error(error.message);process.exitCode=1;});}
module.exports={measure,stats,ids};
