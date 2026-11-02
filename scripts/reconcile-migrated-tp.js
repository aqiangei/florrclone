'use strict';
// Offline only: read the stopped live DB and write a separate reconciled copy.
// Existing talents are kept. A fixed credit covers the debt present at cutover,
// so every level earned since that snapshot awards its normal one TP.
const fs=require('fs'),path=require('path');
const [baselinePath,currentPath,outPath]=process.argv.slice(2);
if(!baselinePath||!currentPath||!outPath||path.resolve(currentPath)===path.resolve(outPath))
  throw Error('Usage: node reconcile-migrated-tp.js BASELINE CURRENT NEW_OUTPUT');
const baseline=JSON.parse(fs.readFileSync(baselinePath));
const current=JSON.parse(fs.readFileSync(currentPath));
const source=fs.readFileSync(path.join(__dirname,'../cpp/shared/game/skills.h'),'utf8');
const block=source.split('kSkillCosts = {{')[1].split('}};')[0];
const costs=Object.fromEntries([...block.matchAll(/\{\{([\d,\s]+)\}\},\s*\/\/\s*(\w+)/g)]
  .map(m=>[m[2],m[1].split(',').map(Number)]));
function level(xp){let n=1;for(;;){const need=Math.floor(15*Math.pow(1.08,n-1));if(xp<need)return n;xp-=need;++n;}}
let accounts=0,points=0;
for(const [id,p] of Object.entries(baseline.players||{})){
  const live=current.players?.[id];if(!live)continue;
  let spent=0;for(const [key,rank]of Object.entries(p.skills||{}))
    spent+=(costs[key]||[]).slice(0,Math.max(0,Number(rank))).reduce((a,b)=>a+b,0);
  const credit=Math.max(0,spent-level(Math.max(0,p.totalXP||0)));
  if(credit>(live.talentPointBonus||0)){
    points+=credit-(live.talentPointBonus||0);live.talentPointBonus=credit;
    live.talentPointReconciledAt='2026-11-03';++accounts;
  }
}
fs.writeFileSync(outPath,JSON.stringify(current));
console.log(JSON.stringify({accountsReconciled:accounts,fixedCreditsAdded:points}));
