
'use strict';
// Pure deployment preflight helper. No network calls and no default target.
// Do not invoke deploy-with-backup.cjs for server two: it is still one-host-only.
const assert=require('node:assert/strict');
function resolveTarget(input,targets){
  assert(input&&typeof input==='object','Explicit deployment target required');
  assert(targets&&typeof targets==='object','Operator-verified target configuration required');
  assert(['one','two'].includes(input.id),'Target must explicitly be one or two');
  assert(Object.hasOwn(targets,input.id),'Target must explicitly be one or two');
  const target=targets[input.id];
  assert.match(target.host||'',/^[a-zA-Z0-9][a-zA-Z0-9.-]*$/,'Invalid host');
  for(const name of ['sshPort','publicPort','internalPort'])assert(Number.isInteger(target[name])&&target[name]>=1&&target[name]<=65535,'Explicit valid '+name+' required');
  assert.equal(input.expectedHost,target.host,'Host does not match selected target');
  assert.equal(input.expectedSshPort,target.sshPort,'SSH port does not match selected target');
  assert.equal(input.expectedPublicPort,target.publicPort,'Public game port does not match selected target');
  assert.match(input.release||'',/^\/opt\/florrclone\/releases\/[a-zA-Z0-9_-]+$/,'Invalid release path');
  assert.equal(input.manifestTarget,input.id,'Release manifest belongs to another target');
  assert.equal(input.expectedInternalPort,target.internalPort,'Internal listener port does not match selected target');
  return Object.freeze({...target,id:input.id,release:input.release});
}
module.exports={resolveTarget};
