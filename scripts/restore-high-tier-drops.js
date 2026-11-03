'use strict';
// Server-specific restoration: preserve reference item pools and all tiers
// below Super; put the removed same-tier tail back into Super rolls.
const fs=require('fs'),path=require('path');
function restoreHighTierDrops(tables){
  const report=[];
  for(const [id,table] of Object.entries(tables)){
    if(!table.referenceMob||table.highTierPolicy)continue;
    let changed=false;
    for(const row of table.drops||[])for(const key of ['rarityChances','magicRarityChances']){
      const rates=row[key];if(!rates?.[7]?.length)continue;
      const superRow=rates[7];
      if(superRow.some(([r,p])=>r>=7&&p>0))continue;
      const ultra=superRow.find(([r,p])=>r===6&&p>0);if(!ultra)continue;
      const original=ultra[1], tail=original*.005;
      ultra[1]=Number((original-tail).toFixed(10));superRow.push([7,Number(tail.toFixed(10))]);
      // E has three times the same-tier S tail. It does not award free
      // permanent Titan-tier petals or multiply item quantities.
      const eternal=superRow.map(([r,p])=>[r,p]);
      eternal.find(([r])=>r===6)[1]=Number((original-original*.015).toFixed(10));
      eternal.find(([r])=>r===7)[1]=Number((original*.015).toFixed(10));
      rates[8]=eternal;
      report.push({mob:id,item:row.itemType,table:key,superPercent:tail,eternalSuperPercent:original*.015});
      changed=true;
    }
    if(changed)table.highTierPolicy='restore-super-tail-0.5pct-eternal-1.5pct';
  }
  return report;
}
module.exports={restoreHighTierDrops};
if(require.main===module){const file=path.join(__dirname,'../data/mob_drops.json'),tables=JSON.parse(fs.readFileSync(file));const report=restoreHighTierDrops(tables);fs.writeFileSync(file,JSON.stringify(tables,null,2)+'\n');fs.writeFileSync(path.join(__dirname,'../docs/super-drop-restoration.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({restoredRows:report.length,mobs:new Set(report.map(r=>r.mob)).size}));}
