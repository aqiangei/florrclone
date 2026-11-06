'use strict';
// The C++ caller freezes simulation and drains its DB before ready(), and
// installs delta before thaw(). Only the coordinator can settle materials.
module.exports=function hyperClient(io){
  let status=null,poll=null,next=0,failed=false,job=null,begin=null;
  const owned=()=>Object.entries(status?.accounts||{});
  const accept=s=>{if(!s?.ok){failed=true;return;}status=s;};
  async function refresh(){accept(await io.read({op:'hyper_status'}));}
  function run(key,fn){
    if(job&&job.key!==key&&job.result)job=null;
    if(job){if(job.key!==key)return {pending:true};if(!job.result)return {pending:true};const result=job.result;job=null;return result;}
    const task=job={key,result:null};
    io.mutate(fn).then(r=>task.result=r,()=>{failed=true;task.result={ok:false,error:'transport_unknown'};});return {pending:true};
  }
  return {
    begin(input){
      if(begin){if(begin.key!==input.attempt)return {ok:false,error:'hyper_busy'};if(!begin.result)return {pending:true};const r=begin.result;begin=null;return r;}
      const task=begin={key:input.attempt,result:null};
      io.mutate(()=>io.write('hyper_begin',input.request)).then(r=>{task.result=r;next=0;},()=>{failed=true;task.result={ok:false,error:'transport_unknown'};});return {pending:true};
    },
    service(){
      if(failed)return {ok:false,error:'transport_unknown'};
      if(!poll&&Date.now()>=next){
        poll=refresh().then(async()=>{
          const operator=status?.operatorJob;
          if(io.operator&&operator?.status==='pending'&&(!operator.maintenanceGate||!require('node:fs').existsSync(operator.maintenanceGate))&&
             (!operator.readyFile||require('node:fs').existsSync(operator.readyFile))) {
            const begun=await io.mutate(()=>io.write('hyper_operator_begin',{operatorRequest:operator.id}));
            if(begun.ok)await refresh();
          }
          const tx=status?.transaction;
          if(tx&&tx.stage!=='complete') {
            const result=await io.mutate(()=>io.write('hyper_progress',{transaction:tx.id}));
            if(!result.ok&&!['hyper_waiting','hyper_transaction_changed'].includes(result.error))throw Error(result.error);
            if(result.ok)await refresh();
          }
        }).catch(error=>{console.error('[authority] H barrier failed:',error.message);failed=true;}).finally(()=>{poll=null;next=Date.now()+100;});
      }
      const tx=status?.transaction,accounts=owned();
      const reply={ok:true,freeze:accounts.length>0,transaction:tx};
      if(accounts.length&&tx?.stage==='settled'){
        reply.delta={players:Object.fromEntries(accounts.map(([id,a])=>[id,a.record])),globals:{hyperForges:status.hyperForges}};
        // Update adapter fences only while C++ is frozen. No ordinary commits
        // are admitted until the caller applies this delta and confirms thaw.
        for(const [id,a]of accounts)io.install(id,a);
        io.installForges(status.hyperForges);
      }
      return reply;
    },
    ready(id){return run('ready/'+id,async()=>{
      await refresh();
      for(const [account,a]of owned())if(status.transaction?.id===id&&status.transaction.stage==='freezing'&&!a.hyperLock.ready){
        const result=await io.write('hyper_ready',{transaction:id,account,...io.lease(account)});
        if(!result.ok&&!['hyper_already_settled','hyper_fence_changed'].includes(result.error))throw Error(result.error);
      }
      await refresh();return {ok:true};
    });},
    thaw(id){return run('thaw/'+id,async()=>{
      // The immutable status used to install records is the fence to thaw.
      for(const [account,a]of owned())if(status.transaction?.id===id){
        const result=await io.write('hyper_thaw',{transaction:id,account,epoch:a.epoch,revision:a.revision});
        if(!result.ok)throw Error(result.error);
      }
      await refresh();return {ok:true};
    });},
    busy(){return !!poll||!!job||!!begin||owned().length>0;},
  };
};
