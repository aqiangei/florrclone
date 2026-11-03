'use strict';
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(path.join(__dirname,'../third_party/cpp_canvas/window.cpp'),'utf8');
const marker='EM_JS(double, web_canvas_coordinate, (const char* element, double client, int axis), {';
const start=source.indexOf(marker),end=source.indexOf('\n});',start);
assert(start>=0&&end>start);
let rect={left:44,top:20,width:936,height:728};
const node={clientWidth:936,clientHeight:728,getBoundingClientRect:()=>rect};
const convert=vm.runInNewContext('(function(element,client,axis){'+source.slice(start+marker.length,end)+'})',
  {document:{getElementById:()=>node},UTF8ToString:x=>x});
// One touch has a canvas-relative start and window-relative end in Emscripten;
// client coordinates remain identical and must yield identical design points.
assert.equal(convert('canvas',144,0),100);assert.equal(convert('canvas',120,1),100);
assert.equal(convert('canvas',144,0),convert('canvas',144,0));
assert.equal(convert('canvas',40,0),-4); // outside release is retained
rect={left:60,top:30,width:468,height:364};
assert.equal(convert('canvas',160,0),200);assert.equal(convert('canvas',130,1),200);
rect={left:0,top:-25,width:936,height:728};
assert.equal(convert('canvas',144,0),144);assert.equal(convert('canvas',75,1),100);
const touch=source.slice(source.indexOf('static EM_BOOL onTouch('),source.indexOf('static EM_BOOL onClick('));
assert(touch.includes('point.clientX'));assert(touch.includes('point.clientY'));
assert(!touch.includes('point.targetX'));
console.log('Canvas input coordinates: safe-area offset, scaled canvas, browser viewport movement and outside release passed');
