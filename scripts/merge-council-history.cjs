'use strict';
const assert=require('node:assert/strict');
function apply(db,seed) {
  assert.equal(seed.version,1);assert(seed.accounts&&typeof seed.accounts==='object');
  let accounts=0,petals=0;
  for(const [id,records]of Object.entries(seed.accounts)) {
    if(!Object.hasOwn(db.players||{},id))continue;
    const player=db.players[id];let changed=false;
    for(const [petal,observation]of Object.entries(records)) {
      assert(/^[a-z0-9_]+$/.test(petal)&&!['__proto__','constructor','prototype'].includes(petal));
      const first=observation.firstSeenAt,last=observation.lastSeenAt;
      assert(Number.isFinite(first)&&Number.isFinite(last)&&first>0&&last>=first);
      if(!player.hyperHistory)player.hyperHistory={};
      const old=player.hyperHistory[petal]||{};
      player.hyperHistory[petal]={firstSeenAt:old.firstSeenAt>0?Math.min(first,old.firstSeenAt):first,lastSeenAt:Math.max(last,old.lastSeenAt||0)};
      changed=true;petals++;
    }
    if(changed)accounts++;
  }
  db.councilHistoryCoverage={from:seed.from,to:seed.to,snapshots:seed.snapshots};
  return {accounts,petals,snapshots:seed.snapshots};
}
module.exports={apply};
