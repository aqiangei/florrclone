'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {restoreOriginalShopLocks}=require('./restore-original-shop-locks.cjs');
const offers=Array.from({length:10},(_,i)=>({petalType:'petal_'+i,rarity:i<8?'rare':'super',discountPercent:10}));
const manifest={schemaVersion:1,id:'test',accounts:[{userId:'stable-id',rotation:123,offers}]};
test('restores only matching stable account and preserves unrelated save data',()=>{
  const save={users:{private:'untouched'},players:{'stable-id':{lockedShopRotation:123,stars:47,inventory:[1,2]},other:{lockedShopRotation:123}}};
  assert.equal(restoreOriginalShopLocks(save,manifest).restored,1);
  assert.deepEqual(save.players['stable-id'].lockedShopOffers,offers);
  assert.equal(save.players['stable-id'].lockedShopOffersRotation,123);
  assert.equal(save.players['stable-id'].stars,47);
  assert.deepEqual(save.players['stable-id'].inventory,[1,2]);
  assert.deepEqual(save.players.other,{lockedShopRotation:123});
  const after=JSON.stringify(save);
  assert.equal(restoreOriginalShopLocks(save,manifest).receipt,1);
  assert.equal(JSON.stringify(save),after);
});
test('does not restore an explicit unlock or a later rotation',()=>{
  for(const rotation of [-1,124,undefined]){
    const save={players:{'stable-id':{lockedShopRotation:rotation}}};
    const before=JSON.stringify(save);
    assert.equal(restoreOriginalShopLocks(save,manifest).unlockedOrChanged,1);
    assert.equal(JSON.stringify(save),before);
  }
});
test('does not replace an existing snapshot or recreate it after receipt',()=>{
  const save={players:{'stable-id':{lockedShopRotation:123,lockedShopOffersRotation:123,lockedShopOffers:[{petalType:'new'}]}}};
  assert.equal(restoreOriginalShopLocks(save,manifest).existing,1);
  assert.equal(save.players['stable-id'].lockedShopOffers[0].petalType,'new');
});
