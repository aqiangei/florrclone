'use strict';
const fs=require('node:fs'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const [input,output,credentials]=process.argv.slice(2);
const original=JSON.parse(fs.readFileSync(input)),db=structuredClone(original);
const request=JSON.parse(fs.readFileSync(credentials));
const entries=Object.entries(db.users||{}).filter(([,u])=>u.username===request.username);
assert.equal(entries.length,1,'Exact account not found');
assert(typeof request.password==='string'&&request.password.length>0,'Password required');
const [key,user]=entries[0],salt=crypto.randomBytes(16).toString('hex');
user.password=crypto.scryptSync(request.password,salt,64).toString('hex');
user.salt=salt;user.isPlainText=false;user.passwordChosen=true;delete user.passwordHash;
let revoked=0;
for(const [token,session] of Object.entries(db.sessions||{}))if(session.userId===user.id){delete db.sessions[token];++revoked;}
const compare=structuredClone(db);compare.users[key]=structuredClone(original.users[key]);compare.sessions=structuredClone(original.sessions);
assert.deepEqual(compare,original);assert.equal(crypto.scryptSync(request.password,user.salt,64).toString('hex'),user.password);
fs.writeFileSync(output,JSON.stringify(db));console.log(JSON.stringify({username:user.username,userId:user.id,revokedSessions:revoked,passwordVerified:true}));
