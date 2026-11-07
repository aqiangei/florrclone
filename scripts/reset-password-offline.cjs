'use strict';
const fs=require('node:fs'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const [input,output,credentials]=process.argv.slice(2);
const original=JSON.parse(fs.readFileSync(input)),db=structuredClone(original);
const requests=[JSON.parse(fs.readFileSync(credentials))].flat();
assert(requests.length && new Set(requests.map(r=>r.username)).size===requests.length,
  'Expected distinct account reset requests');
const results=requests.map(request=>{
const entries=Object.entries(db.users||{}).filter(([,u])=>u.username===request.username);
assert.equal(entries.length,1,'Exact account not found');
assert(typeof request.password==='string'&&request.password.length>0,'Password required');
const [key,user]=entries[0],salt=crypto.randomBytes(16).toString('hex');
if(request.userId)assert.equal(user.id,request.userId,'Account identity changed');
user.password=crypto.scryptSync(request.password,salt,64).toString('hex');
user.salt=salt;user.isPlainText=false;user.passwordChosen=true;delete user.passwordHash;
let revoked=0;
for(const [token,session] of Object.entries(db.sessions||{}))if(session.userId===user.id){delete db.sessions[token];++revoked;}
assert.equal(crypto.scryptSync(request.password,user.salt,64).toString('hex'),user.password);
return {key,username:user.username,userId:user.id,revokedSessions:revoked,passwordVerified:true};
});
const compare=structuredClone(db);
for(const {key} of results)compare.users[key]=structuredClone(original.users[key]);
compare.sessions=structuredClone(original.sessions);
assert.deepEqual(compare,original);
fs.writeFileSync(output,JSON.stringify(db));
const report=results.map(({key,...result})=>result);
console.log(JSON.stringify(report.length===1?report[0]:report));
