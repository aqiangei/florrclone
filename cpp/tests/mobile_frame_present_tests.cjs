'use strict';
// Run with Playwright installed, e.g. NODE_PATH pointing to its node_modules.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {webkit}=require('playwright');
const source=fs.readFileSync(path.join(__dirname,'../third_party/cpp_canvas/canvas.cpp'),'utf8');
function body(name){const a=source.indexOf('EM_JS(',source.indexOf(name)-20),b=source.indexOf('), {',a)+5;return source.slice(b,source.indexOf('\n});',b));}
(async()=>{
 const browser=await webkit.launch({headless:true});
 try{for(const [mobile,fallback]of [[false,false],[true,false],[true,true]]){
  const page=await browser.newPage();await page.setContent('<canvas id="game" width="64" height="64"></canvas>');
  await page.addScriptTag({content:`window.Module={};window.UTF8ToString=s=>s;window.matchMedia=()=>({matches:${mobile}});${fallback?'window.OffscreenCanvas=undefined;':''}window.make=function(element,width,height,virtualCanvas,desynchronized){${body('c2d_create')}};window.publish=function(id,target){${body('c2d_present')}};`});
  const result=await page.evaluate(()=>{
   const id=make('game',64,64,0,1),item=Module.cppCanvasContexts[id],screen=document.querySelector('#game');
   const read=()=>Array.from(screen.getContext('2d').getImageData(20,20,1,1).data).slice(0,3).join(',');
   item.ctx.fillStyle='#00ff00';item.ctx.fillRect(0,0,64,64);publish(id,'game');const before=read();
   let resets=0;for(const key of ['width','height']){const d=Object.getOwnPropertyDescriptor(HTMLCanvasElement.prototype,key);Object.defineProperty(screen,key,{get:()=>d.get.call(screen),set:v=>{resets++;d.set.call(screen,v);}});}
   item.ctx.fillStyle='#000000';item.ctx.fillRect(0,0,64,64);const middle=read();
   item.ctx.fillStyle='#ffffff';item.ctx.fillRect(0,0,64,64);const white=read();
   item.ctx.fillStyle='#ff0000';item.ctx.fillRect(0,0,64,64);publish(id,'game');
   return {buffered:item.surface!==screen,before,middle,white,after:read(),resets};
  });
  assert.equal(result.buffered,mobile);assert.equal(result.before,'0,255,0');assert.equal(result.after,'255,0,0');assert.equal(result.resets,0);
  if(mobile){assert.equal(result.middle,'0,255,0');assert.equal(result.white,'0,255,0');}
  await page.close();
 }}finally{await browser.close();}
 console.log('PASS: mobile black/white intermediate frames stay offscreen; desktop and fallback present without resetting the visible canvas.');
})().catch(error=>{console.error(error);process.exitCode=1;});
