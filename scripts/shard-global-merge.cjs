'use strict';
const {isDeepStrictEqual:equal}=require('node:util');
const clone=v=>v===undefined?undefined:structuredClone(v);
const object=v=>v!==null&&typeof v==='object'&&!Array.isArray(v);
function merge(base,intended,current,path='') {
  if(equal(base,intended))return clone(current);
  if(equal(base,current)||equal(intended,current))return clone(intended);
  // Two shards can create different children of a previously absent map.
  // Merge those additions; competing writes to the same leaf still fail.
  if((object(base)||base===undefined||base===null)&&object(intended)&&object(current)) {
    base=object(base)?base:{};
    const result=clone(current);
    for(const key of new Set([...Object.keys(base),...Object.keys(intended)])) {
      const value=merge(base[key],intended[key],current[key],path+'/'+key);
      if(value===undefined)delete result[key];else result[key]=value;
    }
    return result;
  }
  throw Error('global data changed concurrently at '+path);
}
module.exports={merge};
