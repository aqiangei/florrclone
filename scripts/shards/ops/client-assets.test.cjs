'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const test=require('node:test');
const {assetNames,cacheAssetPath,configure,readAssetHashes}=require('./client-assets.cjs');

const base=fs.readFileSync(path.join(__dirname,'nginx-game.conf'),'utf8');
const release='/opt/florrclone/releases/cpp-first';
const next='/opt/florrclone/releases/cpp-next';
const assetHost='example.com';
const hashes=Object.fromEntries(assetNames.map((name,i)=>[name,String(i).repeat(64)]));

test('the four public asset digests come from the staged release bytes',()=>{
  const sum=assetNames.map(name=>hashes[name]+'  '+name).join('\n');
  assert.deepEqual(readAssetHashes(command=>{
    assert.equal(command,'cd '+release+' && sha256sum -- '+assetNames.join(' '));
    return sum;
  },release),hashes);
  assert.throws(()=>readAssetHashes(()=>sum.replace('bundle.js','inventory.json'),release));
  assert.throws(()=>cacheAssetPath('inventory.json',hashes));
});

test('a new release shares only the unchanged client files',()=>{
  const config=configure(base,{release,assetHost,assetHashes:hashes});
  const updated={...hashes,'bundle.js':'a'.repeat(64),'bundle.js.bin':'b'.repeat(64)};
  const newConfig=configure(base,{release:next,assetHost,assetHashes:updated});
  for(const asset of ['bundle.wasm','bundle.wasm.bin']){
    const stable=cacheAssetPath(asset,hashes);
    assert(config.includes('return 302 '+stable+'$is_args$args;'));
    assert(newConfig.includes('return 302 '+stable+'$is_args$args;'));
    assert(newConfig.includes('alias '+next+'/'+asset+';'));
  }
  for(const asset of ['bundle.js','bundle.js.bin']){
    assert(!newConfig.includes(cacheAssetPath(asset,hashes)));
    assert(newConfig.includes('return 302 '+cacheAssetPath(asset,updated)+'$is_args$args;'));
  }
});

test('the stable route still checks queue tickets and allows Range requests',()=>{
  const config=configure(base,{release,assetHost,downloadQueue:true,assetHashes:hashes});
  for(const name of assetNames){
    const stable=cacheAssetPath(name,hashes);
    const start=config.indexOf('location = '+stable+' {');
    assert(start>=0);
    const block=config.slice(start,config.indexOf('\n    }',start));
    assert(block.includes('auth_request /__download_authorize;'));
    assert(block.includes('X-Download-Ticket, Range'));
    assert(block.includes('immutable'));
  }
  assert(configure(base,{release,assetHost}).includes('alias '+release+'/$1;'));
});

test('direct mode advertises immutable assets and a separately pinned compressed WASM mirror',()=>{
  const assetMirror='http://mirror.example:8088';
  const opts={release,assetHost,assetPort:10000,assetMirror,assetHashes:hashes};
  const config=configure(base,opts);
  const reply=JSON.parse(config.match(/return 200 '(\{"ok":true,"ticket":"direct-download".*?\})';/)[1]);
  assert.equal(reply.assetBase,'http://example.com:10000/client-assets/cpp-first/');
  for(const name of assetNames)assert.equal(reply.assets[name],cacheAssetPath(name,hashes));
  assert.equal(reply.wasmMirror,assetMirror+cacheAssetPath('bundle.wasm.bin',hashes));
  assert.throws(()=>configure(base,{...opts,downloadQueue:true}));
  assert.throws(()=>configure(base,{...opts,assetMirror:'http://mirror.example/extra'}));
  assert.throws(()=>configure(base,{...opts,assetHashes:undefined}));
});
