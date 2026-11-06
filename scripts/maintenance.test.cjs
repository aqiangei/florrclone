const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
function fixture(dpr=2){
 const listeners={},elements={};let frame; const on=(k,f)=>{const previous=listeners[k];listeners[k]=e=>{if(previous)previous(e);f(e);};};
 const context2d=new Proxy({setTransform(...a){this.transform=a;}},{get(t,k){return k in t?t[k]:(()=>{});}});
 const canvas={width:440,height:320,getContext:()=>context2d,getBoundingClientRect:()=>({left:10,top:20,width:584,height:584*320/440}),focus(){document.activeElement=canvas;},setPointerCapture(){},hasPointerCapture(){return true;},releasePointerCapture(){}};
 elements.game=canvas;for(const id of ['score','pause','restart','status','enter','heading','ready-banner'])elements[id]={textContent:'',hidden:['enter','ready-banner'].includes(id)};
 const document={hidden:false,activeElement:canvas,getElementById:id=>elements[id],addEventListener:on};
 const sandbox={document,devicePixelRatio:dpr,matchMedia:()=>({matches:false}),ResizeObserver:class{constructor(f){this.f=f}observe(){this.f()}},addEventListener:on,requestAnimationFrame:f=>frame=f,setInterval(){},setTimeout,clearTimeout,AbortController,fetch:async()=>({ok:false}),location:{reload(){}},Math};
 const ctx=vm.createContext(sandbox);const source=fs.readFileSync(__dirname+'/maintenance.html','utf8').match(/<script>([\s\S]*?)<\/script>/)[1];vm.runInContext(source,ctx);
 return {canvas,context2d,document,listeners,elements,ctx,run:s=>vm.runInContext(s,ctx),frame:t=>frame(t)};
}
test('backing store follows displayed size and DPR while pointer stays in game coordinates',()=>{
 const f=fixture(2);assert.equal(f.canvas.width,1168);assert.equal(f.canvas.height,849);
 assert.equal(f.context2d.transform[0],1168/440);
 f.canvas.onpointerdown({clientX:302,clientY:20+584*160/440,pointerId:1});
 assert.equal(f.run('player.x'),220);assert(Math.abs(f.run('player.y')-160)<1e-9);
 f.run('devicePixelRatio=3');f.frame(16);assert.equal(f.canvas.width,1752);assert.equal(f.run('player.x'),220);
});
test('diagonal movement has the same speed and pauses do not accumulate game time',()=>{
 const f=fixture();f.run('player={x:220,y:160};keys.add("ArrowRight");');f.frame(100);f.frame(120);
 const straight=f.run('player.x-220');f.run('reset();player={x:220,y:160};keys.add("ArrowRight");keys.add("ArrowDown");');f.frame(100);f.frame(120);
 assert(Math.abs(f.run('Math.hypot(player.x-220,player.y-160)')-straight)<1e-9);
 f.document.hidden=true;f.listeners.visibilitychange();const before=f.run('score');f.frame(9000);assert.equal(f.run('score'),before);
 f.elements.pause.onclick();f.frame(12000);assert.equal(f.run('score'),before);f.frame(12016);assert(f.run('score')>before);
});

test('WASD matches arrows and clicking the canvas resumes without teleporting',()=>{
 const f=fixture();f.run('player={x:220,y:160}');f.listeners.keydown({code:'KeyD',key:'d',preventDefault(){}});f.frame(100);f.frame(120);assert(f.run('player.x')>220);
 f.listeners.keyup({code:'KeyD',key:'d'});assert.equal(f.run('keys.size'),0);
 f.run('pause(true)');const before=f.run('player.x');f.canvas.onpointerdown({clientX:50,clientY:60,pointerId:1});assert.equal(f.run('paused'),false);assert.equal(f.run('player.x'),before);
});
test('restored public health updates the title and prominent completion banner',async()=>{
 const f=fixture();await new Promise(r=>setImmediate(r));
 f.run("fetch=async(url,options)=>({ok:url==='/api/health'&&options.cache==='no-store',json:async()=>({ok:true,implementation:'cpp-wasm'})})");
 await f.run('checkReady()');assert.equal(f.run('ready'),true);assert.equal(f.elements.enter.hidden,false);assert.equal(f.elements['ready-banner'].hidden,false);assert.match(f.elements.heading.textContent,/更新完毕/);
});
