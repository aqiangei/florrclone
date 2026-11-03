'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(require('node:path').join(__dirname,'../client/web/shell.html'),'utf8')
  .split('// MOBILE_FULLSCREEN_BEGIN')[1].split('// MOBILE_FULLSCREEN_END')[0];
function fixture({mobile=true,supported=true,lockFails=false}={}){
  const btn={hidden:true},close={},span={},hint={hidden:true,querySelector:s=>s==='span'?span:close};
  let timer,locks=0,unlocks=0,requests=0;
  const document={fullscreenElement:null,getElementById:id=>id==='landscape-btn'?btn:hint,
    documentElement:{},exitFullscreen:async()=>{document.fullscreenElement=null;}};
  if(supported)document.documentElement.requestFullscreen=async()=>{requests++;document.fullscreenElement={};};
  const context={document,navigator:{userAgent:mobile?'iPhone':'Desktop',maxTouchPoints:mobile?5:0},
    matchMedia:()=>({matches:mobile}),innerWidth:390,innerHeight:844,
    screen:{orientation:{lock:async()=>{locks++;if(lockFails)throw Error('Not supported');},unlock:()=>unlocks++}},
    setTimeout:f=>{timer=f;return 1;},clearTimeout:()=>{},addEventListener:()=>{}};
  context.window=context;vm.runInNewContext(source,context);
  return {context,btn,hint,span,tick:()=>timer(),calls:()=>({locks,unlocks,requests})};
}
(async()=>{
  const a=fixture();assert.equal(a.btn.hidden,false);a.context.florrMobileReady();a.tick();assert.equal(a.btn.hidden,true);
  await a.context.florrToggleFullscreen();assert.deepEqual(a.calls(),{locks:1,unlocks:0,requests:1});
  await a.context.florrToggleFullscreen();assert.equal(a.calls().unlocks,1);
  const b=fixture({supported:false,lockFails:true});await b.context.florrToggleFullscreen();
  assert.equal(b.hint.hidden,false);assert.match(b.span.textContent,/添加到主屏幕/);
  const c=fixture({lockFails:true});await c.context.florrToggleFullscreen();assert.match(c.span.textContent,/将设备横过来/);
  const d=fixture({mobile:false});assert.equal(d.btn.hidden,true);d.context.florrMobileReady();assert.equal(d.btn.hidden,true);
  const e=fixture();e.btn.onclick();e.context.florrMobileReady();assert.equal(e.btn.hidden,true);
  console.log('Mobile fullscreen: loading shortcut, 10s lifetime, enter/exit, orientation fallback, desktop and dismissal passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
