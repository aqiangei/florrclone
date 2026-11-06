const {test}=require('node:test'),assert=require('node:assert/strict'),{apply}=require('./merge-council-history.cjs');
test('history import preserves progress, credentials and existing observations and is idempotent',()=>{
 const db={users:{a:{passwordHash:'untouched'}},players:{a:{totalXP:123,inventory:{apex:{petal_rose:1}},hyperHistory:{rose:{firstSeenAt:20,lastSeenAt:60}}}},sessions:{token:'untouched'}};
 const before=structuredClone(db);const seed={version:1,from:10,to:50,snapshots:2,accounts:{a:{rose:{firstSeenAt:10,lastSeenAt:50},egg:{firstSeenAt:15,lastSeenAt:40}},missing:{rose:{firstSeenAt:10,lastSeenAt:20}}}};
 const report=apply(db,seed);assert.equal(report.accounts,1);assert.deepEqual(db.players.a.hyperHistory.rose,{firstSeenAt:10,lastSeenAt:60});assert.equal(db.players.missing,undefined);
 const once=structuredClone(db);apply(db,seed);assert.deepEqual(db,once);delete db.councilHistoryCoverage;db.players.a.hyperHistory=before.players.a.hyperHistory;assert.deepEqual(db,before);
});
test('history import rejects invalid observation times',()=>{assert.throws(()=>apply({players:{a:{}}},{version:1,accounts:{a:{rose:{firstSeenAt:30,lastSeenAt:10}}}}));});
