'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {directory}=require('./shard-directory.cjs');
const config=()=>({node:'one',key:'private',shards:[
  {id:'one',label:'一服',host:'192.0.2.1',port:8088,available:true},
  {id:'two',label:'二服',host:'192.0.2.2',port:8088,available:false},
]});
test('public directory contains only explicit public endpoints',()=>{
  assert.equal(directory({node:'one'}),null);
  const value=directory(config());assert.equal(value.current,'one');assert.equal(value.choices[1].available,false);
  assert(!JSON.stringify(value).includes('private'));
});
test('directory rejects duplicate identities and URL credentials',()=>{
  for(const host of ['http://192.0.2.2','user:pass@host','host/path','host?token=x','']) {
    const input=config();input.shards[1].host=host;assert.throws(()=>directory(input));
  }
  const duplicate=config();duplicate.shards[1].id='one';assert.throws(()=>directory(duplicate));
  const port=config();port.shards[1].port=65536;assert.throws(()=>directory(port));
});
