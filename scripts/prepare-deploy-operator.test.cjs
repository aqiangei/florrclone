const {test}=require('node:test'),assert=require('node:assert/strict');
const {prepare}=require('./prepare-deploy-operator.cjs');
const config={username:'ops_test',password:'random-test-operator-secret-long-enough'};
test('bootstrap adds only a new operator and preserves all existing progress',()=>{
 const db={users:{player:{id:'p',username:'player'}},players:{p:{inventory:{basic:12},xp:456}},sessions:{original:{userId:'p'}},guilds:{keep:true}};
 const snapshot=structuredClone(db),r=prepare(db,config);
 assert.deepEqual(db,snapshot);assert.equal(r.next.users.ops_test.admin,true);
 assert.equal(r.next.users.ops_test.adminTier,4);assert.notEqual(r.next.users.ops_test.password,config.password);
 delete r.next.users.ops_test;delete r.next.players[r.userId];assert.deepEqual(r.next,db);
});
test('bootstrap refuses existing administrators or existing account names',()=>{
 assert.throws(()=>prepare({users:{a:{admin:true}},players:{}},config));
 assert.throws(()=>prepare({users:{ops_test:{}},players:{}},config));
});
