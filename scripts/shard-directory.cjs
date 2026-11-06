'use strict';
function directory(config){
  if(config.shards===undefined)return null;
  if(!Array.isArray(config.shards)||config.shards.length!==2)throw Error('two explicit shard endpoints required');
  const ids=new Set();
  const choices=config.shards.map(row=>{
    if(!row||!['one','two'].includes(row.id)||ids.has(row.id)||
      typeof row.label!=='string'||!row.label.length||row.label.length>24||
      typeof row.host!=='string'||row.host.length>253||!/^[a-zA-Z0-9.-]+$/.test(row.host)||
      !Number.isInteger(row.port)||row.port<1||row.port>65535||typeof row.available!=='boolean')
      throw Error('invalid public shard endpoint');
    ids.add(row.id);return {id:row.id,label:row.label,host:row.host,port:row.port,available:row.available};
  });
  if(!ids.has(config.node))throw Error('current shard missing');
  return Object.freeze({current:config.node,choices});
}
module.exports={directory};
