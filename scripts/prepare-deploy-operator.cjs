"use strict";
const fs=require("node:fs"),crypto=require("node:crypto"),assert=require("node:assert/strict");
function prepare(db, config) {
  assert(db.users && db.players, "Invalid database");
  assert(!Object.values(db.users).some(u=>u.admin), "Bootstrap is only for a server without full administrators");
  const name=config.username.toLowerCase();
  assert(/^[a-z0-9_]{3,20}$/.test(name) && config.password.length>=24, "Dedicated random operator credentials required");
  assert(!db.users[name], "Operator must be a new account");
  const next=structuredClone(db),id=crypto.randomUUID(),salt=crypto.randomBytes(16).toString("hex");
  next.users[name]={id,username:name,password:crypto.scryptSync(config.password,salt,64).toString("hex"),salt,isPlainText:false,passwordChosen:true,admin:true,adminTier:4,createdAt:Date.now(),lastActiveAt:0};
  next.players[id]={};
  const unchanged=structuredClone(next);delete unchanged.users[name];delete unchanged.players[id];
  assert.deepEqual(unchanged,db,"Existing accounts must remain unchanged");
  return {next,username:name,userId:id};
}
if(require.main===module){
  const [input,configFile,output]=process.argv.slice(2);
  const {next,username,userId}=prepare(JSON.parse(fs.readFileSync(input,"utf8")),JSON.parse(fs.readFileSync(configFile,"utf8")));
  fs.writeFileSync(output,JSON.stringify(next));
  console.log(JSON.stringify({operatorCreated:true,username,userId,existingAccountsUnchanged:true}));
}
module.exports={prepare};
