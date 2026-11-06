'use strict';
// Ordering shared by first migration and coordinated upgrades. All host actions
// live in the validated transport adapter; this module is fault-injection tested.
const assert=require('node:assert/strict');
async function deploy(ops){
  let phase='preflight',locked=false,gates=false,opened=false,backup,failure,result;
  const step=async(name,...args)=>{phase=name;if(process.env.FLIX_DEPLOY_PROGRESS==='1')console.error('[deploy]',new Date().toISOString(),name);return await ops[name](...args);};
  try{
    await step('preflight');
    // lock() is responsible for tracking partial acquisition so unlock() can
    // release only locks owned by this invocation, including on second-host failure.
    locked=true;await step('lock');await step('preflight');
    await step('announce');
    gates=true;await step('closeGates');
    await step('stopGames');await step('stopCoordinator');
    backup=await step('backup');
    assert(Array.isArray(backup?.files)&&backup.files.length>0&&backup.files.every(f=>/^[a-f0-9]{64}$/.test(f.sha256)),
      'No complete verified stopped backup');
    // No installed release, bootstrap file, game config or unit may be replaced
    // before both stopped snapshots and every local SHA check above succeed.
    await step('install',backup);
    await step('startCoordinator');
    // This supervisor-only operation is allowed only because both old game
    // processes were confirmed stopped. No heartbeat timeout may substitute.
    await step('recoverStoppedOwners');
    await step('startGames');await step('verifyPrivate');
    opened=true;await step('openGates');await step('verifyPublic');
    await step('recordSuccess',backup);
    result={status:'deployed',backup};return result;
  }catch(error){
    const failedPhase=phase;
    if(gates){
      try{await ops.closeGates();}catch(gateError){error.gateError=gateError.message;}
    }
    // Never restore a pre-cutover snapshot automatically: a new coordinator or
    // either reopened game may already have persisted legitimate newer progress.
    error.deployment={phase:failedPhase,opened,backup};failure=error;throw error;
  }finally{
    if(locked)try{await ops.unlock();}catch(error){
      if(failure)failure.lockCleanupError=error.message;
      else if(result)result.warnings=['Deployment succeeded, but release lock cleanup failed: '+error.message];
      else throw error;
    }
  }
}
module.exports={deploy};
