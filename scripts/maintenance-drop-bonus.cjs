'use strict';
// Only the deployment runner calls this, after a verified stopped-save backup.
// Normal disconnects and ordinary service restarts never enter this path.
const assert=require('node:assert/strict');
function apply(database,{id,stoppedAt,resumedAt}){
  assert(typeof id==='string'&&/^[a-zA-Z0-9_-]{1,100}$/.test(id),'Invalid maintenance identity');
  assert(Number.isSafeInteger(stoppedAt)&&stoppedAt>0,'Invalid stop timestamp');
  assert(Number.isSafeInteger(resumedAt)&&resumedAt>=stoppedAt,'Invalid resume timestamp');
  assert(database?.players&&typeof database.players==='object'&&!Array.isArray(database.players),'Missing players');
  const duration=resumedAt-stoppedAt;
  let extended=0;
  for(const player of Object.values(database.players)){
    const bonus=player?.dailyDropBonus;
    if(!bonus||typeof bonus!=='object'||!Number.isSafeInteger(bonus.until))continue;
    const previous=bonus.maintenanceCredit;
    const credited=previous?.id===id?previous.milliseconds:0;
    assert(Number.isSafeInteger(credited)&&credited>=0,'Invalid previous maintenance credit');
    // Retrying the same update only adds its newly elapsed downtime. An expired
    // bonus is never revived, and daily claim/streak/multiplier stay untouched.
    if(bonus.until-credited<=stoppedAt||duration<=credited)continue;
    const until=bonus.until+duration-credited;
    assert(Number.isSafeInteger(until),'Bonus deadline overflow');
    bonus.until=until;
    bonus.maintenanceCredit={id,milliseconds:duration};
    extended++;
  }
  return {id,stoppedAt,resumedAt,durationMs:duration,extended};
}
module.exports={apply};
if(require.main===module){
  const fs=require('node:fs');
  const [file,id,stop]=process.argv.slice(2);
  const db=JSON.parse(fs.readFileSync(file,'utf8'));
  const report=apply(db,{id,stoppedAt:Number(stop),resumedAt:Date.now()});
  const temporary=file+'.bonus-next';
  const stat=fs.statSync(file);
  const fd=fs.openSync(temporary,'wx',stat.mode&0o777);
  try{
    fs.writeFileSync(fd,JSON.stringify(db));
    if(process.platform!=='win32')fs.fchownSync(fd,stat.uid,stat.gid);
    fs.fsyncSync(fd);
  }finally{fs.closeSync(fd);}
  fs.renameSync(temporary,file);
  console.log(JSON.stringify(report));
}
