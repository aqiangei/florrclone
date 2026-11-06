'use strict';
// Local integration fixture ONLY. Each fixture owns its own synthetic file.
// This is not a coordinator and must never be used for shared production data.
const fs = require('node:fs');
const path = require('node:path');
if (process.env.SHARD_TEST_MODE !== '1') throw new Error('test fixture disabled');
const file = process.env.SHARD_TEST_STORE;
if (!file || !path.isAbsolute(file)) throw new Error('absolute synthetic store required');
module.exports = {
  snapshot() { return fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : '{}'; },
  commit(text) {
    const started = process.hrtime.bigint();
    let renamed = false;
    const tmp = file + '.pending';
    try {
      const fd = fs.openSync(tmp, 'w', 0o600);
      try { fs.writeFileSync(fd, text); fs.fsyncSync(fd); }
      finally { fs.closeSync(fd); }
      fs.renameSync(tmp, file); renamed = true;
      const dir = fs.openSync(path.dirname(file), 'r');
      try { fs.fsyncSync(dir); } finally { fs.closeSync(dir); }
      try {
        fs.appendFileSync(file + '.metrics', JSON.stringify({bytes:Buffer.byteLength(text),
          hostCommitMillis:Number(process.hrtime.bigint()-started)/1e6})+'\n');
      } catch (_) { /* Observation must never change the durability outcome. */ }
      return fs.existsSync(file + '.unknown') ? 'unknown' : 'committed';
    } catch (_) { return renamed ? 'unknown' : 'rejected'; }
  },
};
if(process.env.SHARD_TEST_ASYNC==='1') {
  let pending=null;
  module.exports.commitDelta=delta=>{
    const key=JSON.stringify(delta);
    if(pending) {
      if(pending.key!==key)return 'unknown';
      if(!pending.outcome)return 'pending';
      const result=pending.outcome;pending=null;return result;
    }
    pending={key,outcome:null};const operation=pending;
    setTimeout(()=>{
      try {
        const state=JSON.parse(module.exports.snapshot());
        for(const table of ['users','players','sessions'])for(const [id,value]of Object.entries(delta[table]||{})) {
          state[table]??={};if(value===null)delete state[table][id];else state[table][id]=value;
        }
        for(const [table,value]of Object.entries(delta.globals||{}))state[table]=value;
        operation.outcome=module.exports.commit(JSON.stringify(state));
      } catch (_) {operation.outcome='unknown';}
    },Number(process.env.SHARD_TEST_DELAY_MS||80));
    return 'pending';
  };
}
