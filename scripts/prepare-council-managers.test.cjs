'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const {prepareCouncilManagers}=require('./prepare-council-managers.cjs');
test('council-only delegation uses ID, preserves all data and is idempotent',()=>{
 const db={users:{renamed:{id:'verified-id',username:'renamed',admin:false}},players:{'verified-id':{inventory:{saved:7},totalXp:900}},councilPolls:{old:{votes:{p:'yes'}}}};
 const before=structuredClone(db),next=prepareCouncilManagers(db,['verified-id']);
 assert.deepEqual(db,before);assert.deepEqual(next.users,db.users);assert.deepEqual(next.players,db.players);
 assert.deepEqual(next.councilPolls,db.councilPolls);assert.deepEqual(next.councilManagers,{'verified-id':true});
 assert.deepEqual(prepareCouncilManagers(next,['verified-id']),next);
 assert.throws(()=>prepareCouncilManagers(db,['renamed']));
 assert.throws(()=>prepareCouncilManagers({...db,users:{x:{id:'verified-id',guest:true}}},['verified-id']));
});
