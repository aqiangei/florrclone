'use strict';
// One instance per authenticated node/boot/role, shared by sync and async RPC.
// A request is immutable across retries: ackThrough is part of its digest.
const FINAL_REJECTIONS=new Set([
  'not_sent','busy','snapshot_only','bad_node','bad_node_or_request','bad_request_id',
  'request_id_reused','request_journal_full','identity_changed','session_changed',
  'bad_transaction','counter_exhausted','save_failed','global_conflict',
  'account_creation_conflict','account_conflict','hyper_owner_not_locked',
  'unknown_account','supervisor_required','recovery_conflict','account_busy',
  'invalid_transfer','unknown_operation','bad_record','bad_target_or_transfer','transfer_reused',
  'bad_request_serial','request_acknowledged','bad_acknowledgement','transaction_preparation_rejected',
  'bad_global_patch','bad_identity_query','bad_global_query',
]);
class RequestSequence {
  constructor(){this.serial=0;this.ackThrough=0;this.completed=new Set();this.issued=new WeakSet();}
  request(op,fields={}){
    if(this.serial>=9007199254739998)throw Error('request sequence exhausted');
    const serial=++this.serial;
    const request=structuredClone({...fields,op,requestId:'r'+serial,serial,ackThrough:this.ackThrough});
    const freeze=value=>{if(value&&typeof value==='object'){for(const v of Object.values(value))freeze(v);Object.freeze(value);}};
    freeze(request);this.issued.add(request);return request;
  }
  complete(request,reply){
    if(!this.issued.has(request))throw Error('request belongs to another actor');
    // Unknown, missing, malformed and future error kinds stay unacknowledged.
    if(!(reply?.ok===true||(reply?.ok===false&&FINAL_REJECTIONS.has(reply.error))))return reply;
    if(request.serial>this.ackThrough)this.completed.add(request.serial);
    while(this.completed.delete(this.ackThrough+1))++this.ackThrough;
    return reply;
  }
}
module.exports={RequestSequence};
