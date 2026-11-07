'use strict';
// Separate from the simulation process: admission must remain responsive when a tick is slow.
const http=require('node:http'),crypto=require('node:crypto');
class DownloadQueue {
  constructor({limit=4,ttl=120000,max=2000,now=Date.now}={}) {
    this.limit=limit;this.ttl=ttl;this.max=max;this.now=now;this.rows=new Map();
  }
  sweep(){
    const now=this.now();for(const [id,row] of this.rows)if(row.until<=now)this.rows.delete(id);
    let active=[...this.rows.values()].filter(r=>r.active).length;
    for(const row of this.rows.values())if(!row.active&&active<this.limit){row.active=true;active++;}
  }
  join(requestId){
    this.sweep();
    const id=/^[a-f0-9]{48}$/.test(requestId||'')?requestId:crypto.randomBytes(24).toString('hex');
    if(this.rows.has(id))return this.status(id);
    if(this.rows.size>=this.max)throw Error('queue_full');
    this.rows.set(id,{active:false,until:this.now()+this.ttl});
    return this.status(id);
  }
  status(id){this.sweep();const row=this.rows.get(id);if(!row)return null;row.until=this.now()+this.ttl;let ahead=0;for(const [other,r]of this.rows){if(other===id)break;if(!r.active)ahead++;}return {ticket:id,state:row.active?'ready':'waiting',ahead:row.active?0:ahead,active:[...this.rows.values()].filter(r=>r.active).length};}
  authorized(id){this.sweep();return this.rows.get(id)?.active===true;}
  release(id){this.rows.delete(id);this.sweep();}
}
function createServer({queue=new DownloadQueue(),assetBase}={}) {
  if(!/^https?:\/\/[a-zA-Z0-9.:-]+\/client-assets\/[a-zA-Z0-9_-]+\/$/.test(assetBase||''))throw Error('Invalid asset base');
  return http.createServer((req,res)=>{
    const u=new URL(req.url,'http://localhost');res.setHeader('Cache-Control','no-store');
    const json=(code,value)=>{res.writeHead(code,{'Content-Type':'application/json'});res.end(JSON.stringify(value));};
    if(u.pathname==='/health')return json(200,{ok:true});
    if(u.pathname==='/authorize') {
      const original=new URL(req.headers['x-original-uri']||'/','http://localhost');
      res.writeHead(queue.authorized(req.headers['x-download-ticket']||original.searchParams.get('ticket'))?204:403);return res.end();
    }
    if(req.method==='POST'&&u.pathname==='/download-queue/join') {
      try{return json(200,{...queue.join(u.searchParams.get('request')),assetBase});}catch{return json(503,{error:'queue_full'});}
    }
    const ticket=u.searchParams.get('ticket');
    if(req.method==='GET'&&u.pathname==='/download-queue/status') {
      const row=queue.status(ticket);return json(row?200:410,row?{...row,assetBase}:{error:'expired'});
    }
    if(req.method==='POST'&&u.pathname==='/download-queue/release'){queue.release(ticket);return json(200,{ok:true});}
    return json(404,{error:'not_found'});
  });
}
if(require.main===module){
  const queue=new DownloadQueue({limit:Number(process.env.DOWNLOAD_SLOTS||4)});
  if(!Number.isInteger(queue.limit)||queue.limit<1||queue.limit>32)throw Error('Invalid slot count');
  createServer({queue,assetBase:process.env.DOWNLOAD_ASSET_BASE}).listen(Number(process.env.PORT||8789),'127.0.0.1');
}
module.exports={DownloadQueue,createServer};
