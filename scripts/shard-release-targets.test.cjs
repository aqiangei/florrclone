
'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {resolveTarget}=require('./shard-release-targets.cjs');
const targets={one:{host:'192.0.2.10',sshPort:22,publicPort:8088,internalPort:8788},two:{host:'192.0.2.11',sshPort:2222,publicPort:41080,internalPort:8080}};
const two={id:'two',expectedHost:'192.0.2.11',expectedSshPort:2222,expectedPublicPort:41080,expectedInternalPort:8080,manifestTarget:'two',release:'/opt/florrclone/releases/test-shards95'};
test('target configuration separates SSH, public game and internal ports',()=>{const t=resolveTarget(two,targets);assert.equal(t.sshPort,2222);assert.equal(t.publicPort,41080);assert.equal(t.internalPort,8080);});
test('no implicit target or fallback',()=>{assert.throws(()=>resolveTarget());assert.throws(()=>resolveTarget(two));assert.throws(()=>resolveTarget({...two,id:'unknown'},targets));});
test('mixed-host or mixed-port deployment is rejected',()=>{for(const patch of [{expectedHost:'192.0.2.10'},{expectedSshPort:22},{expectedPublicPort:8088},{expectedInternalPort:8088}])assert.throws(()=>resolveTarget({...two,...patch},targets));});
test('wrong manifest and release traversal are rejected',()=>{assert.throws(()=>resolveTarget({...two,manifestTarget:'one'},targets));assert.throws(()=>resolveTarget({...two,release:'/opt/florrclone/releases/../current'},targets));});
