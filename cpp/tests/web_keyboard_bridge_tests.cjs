'use strict';
// Exercises the actual EM_JS bridge without a browser dependency.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const assert=require('node:assert/strict');
const source=fs.readFileSync(path.join(__dirname,'../third_party/cpp_canvas/window.cpp'),'utf8');
function body(name,args){
  const marker=`EM_JS(void, ${name}, (${args}), {`;
  const start=source.indexOf(marker);assert(start>=0);
  const end=source.indexOf('\n});',start);assert(end>start);
  return source.slice(start+marker.length,end);
}
function fixture(){
  const nodes=new Map(),pageEvents={};
  const document={activeElement:null,getElementById:id=>nodes.get(id)||null};
  document.createElement=()=>({style:{},value:'',listeners:{},focusCalls:0,
    setAttribute(){},addEventListener(type,fn){this.listeners[type]=fn;},
    focus(){++this.focusCalls;document.activeElement=this;},
    blur(){if(document.activeElement===this)document.activeElement=null;}});
  document.body={appendChild:node=>nodes.set(node.id,node)};
  const context={Module:{},document,window:{},performance:{now:()=>1000},
    addEventListener:(type,fn)=>{pageEvents[type]=fn;}};
  vm.createContext(context);
  vm.runInContext(`function keyboard(wanted){${body('web_soft_keyboard','int wanted')}}\n`+
    `function clipboard(){${body('web_clipboard_init','')}}`,context);
  context.keyboard(2);
  return {context,node:nodes.get('__flix_soft_keyboard'),pageEvents,document};
}
{
  const {context,node,document}=fixture();const calls=node.focusCalls;
  context.keyboard(1);assert.equal(node.focusCalls,calls+1);
  assert.equal(document.activeElement,node);
  node.__composing=true;node.value=' composing';context.keyboard(2);
  assert.equal(node.value,' composing');assert.equal(node.__composing,true);
  context.keyboard(0);assert.equal(document.activeElement,null);
}
{
  const {context,node}=fixture();
  node.listeners.compositionstart();node.value='汉';
  node.listeners.input({inputType:'insertCompositionText',data:'汉'});
  context.Module.cppSoftText='';context.Module.cppSoftErase=0;
  node.value='汉字';node.listeners.compositionend();
  assert.equal(node.value,'汉字','Safari final value must survive compositionend');
  node.listeners.input({inputType:'insertText',data:'字'});
  assert.equal(context.Module.cppSoftText,'字');
  assert.equal(context.Module.cppSoftErase,0);
}
{
  const {context,node}=fixture();node.value=' pasted';
  node.listeners.input({inputType:'insertFromPaste',data:'pasted'});
  assert.equal(context.Module.cppSoftText,'pasted','paste without clipboard event must work');
}
{
  const {context,node,pageEvents}=fixture();context.clipboard();
  pageEvents.paste({clipboardData:{getData:()=> 'once'}});
  node.value=' once';node.listeners.input({inputType:'insertFromPaste',data:'once'});
  assert.equal(context.Module.cppSoftText,'','page-handled paste must not be inserted twice');
  assert.equal(context.Module.cppCanvasClipboard,'once');
  assert.equal(context.Module.cppCanvasPasteSeq,1);
}
{
  const {context,node}=fixture();node.__last=' abc';node.value=' ';
  node.listeners.input({inputType:'deleteContentBackward',data:null});
  assert.equal(context.Module.cppSoftErase,3);
}
console.log('5 web keyboard bridge regression groups passed');
