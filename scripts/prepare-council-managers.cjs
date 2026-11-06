'use strict';
const assert=require('node:assert/strict');
function prepareCouncilManagers(database,ids) {
  assert(database?.users && database?.players,'Invalid production save');
  assert(Array.isArray(ids)&&ids.length>0,'Explicit immutable account IDs required');
  const next=structuredClone(database);
  if(next.councilManagers==null)next.councilManagers={};
  assert(typeof next.councilManagers==='object'&&!Array.isArray(next.councilManagers));
  for(const id of ids) {
    assert(typeof id==='string'&&id.length>0,'Invalid account ID');
    const accounts=Object.values(database.users).filter(a=>a.id===id);
    assert.equal(accounts.length,1,'Account ID must identify exactly one existing account');
    assert(!accounts[0].guest&&!accounts[0].adminBanned,'Ineligible council manager');
    next.councilManagers[id]=true;
  }
  const unchanged=structuredClone(next);
  if(database.councilManagers===undefined)delete unchanged.councilManagers;
  else unchanged.councilManagers=database.councilManagers;
  assert.deepEqual(unchanged,database,'Council delegation must not alter players or other account permissions');
  return next;
}
module.exports={prepareCouncilManagers};
