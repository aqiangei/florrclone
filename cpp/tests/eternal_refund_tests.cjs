'use strict';
const assert=require('node:assert/strict');
const {refund}=require('../../scripts/refund-eternal-forging.cjs');
const catalog={corruption:{name:'Corruption'},fragment:{name:'Fragment'},timed_mjolnir:{name:'Timed Mjolnir'}};
function fixture(fees){return {users:{tester:{id:'id'}},players:{id:{inventory:{super:{petal_corruption:7}},titanForgeCounts:{corruption:Math.min(2,fees.length)}}},notifications:fees.map((fee,i)=>({id:String(i),type:'unique_craft',message:`The Eternal Corruption has been forged by @tester [tester] (forged x1, consumed x${fee})`}))};}
for(const [fees,expected]of [[[10],0],[[10,25],10],[[10,25,55],50],[[10,25,55,55],90]]){
  const source=fixture(fees),original=structuredClone(source),first=refund(source,catalog);
  assert.equal(first.report.superMaterialsRefunded,expected);
  assert.equal(first.database.players.id.inventory.super.petal_corruption,7+expected);
  assert.deepEqual(source,original);
  assert.equal(refund(first.database,catalog).report.superMaterialsRefunded,0);
}
const hammer=fixture([]);hammer.players.id.titanForgeCounts={fragment:2};
hammer.notifications=[{id:'hammer',type:'unique_craft',message:'The Eternal Timed Mjolnir has been forged by @tester [tester] (forged x1, consumed x5)'}];
assert.equal(refund(hammer,catalog).report.superMaterialsRefunded,0);
const duplicate=fixture([10,25,55]);duplicate.notifications.push(duplicate.notifications[2]);
assert.equal(refund(duplicate,catalog).report.superMaterialsRefunded,50);
const trimmed=fixture([10,25,55]);trimmed.notifications=trimmed.notifications.slice(-1);
assert.equal(refund(trimmed,catalog).report.superMaterialsRefunded,50);
const unknown=fixture([25]);delete unknown.users.tester;
assert.throws(()=>refund(unknown,catalog),/Cannot resolve/);
console.log('Permanent Eternal refund: first/second/third/fourth, idempotence, hammer exclusion, duplicate evidence and unresolved owner passed');
