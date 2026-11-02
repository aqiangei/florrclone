'use strict';
const fs=require('node:fs'),path=require('node:path');
const {loadEngine,BUNDLE}=require('../../florrclone/server/engine');
const root=path.resolve(__dirname,'..'),site=path.resolve(root,'../florrclone/florr-local-site');
const api=loadEngine(site),text=fs.readFileSync(path.join(site,BUNDLE),'utf8');
const marker='nb=JSON.parse(`',at=text.indexOf(marker);
if(at<0)throw Error('Pinned collision table boundary changed');
const start=at+marker.length,end=text.indexOf('`)',start),geometry=JSON.parse(text.slice(start,end));
for(const biome of ['Pyramid','Super','Swarm','dps test']){
 const {width,height}=api.mapSize(biome),engine=Object.create(api.Engine.prototype);
 Object.assign(engine,{save:{biome},width,height,waveMode:false});
 const contours=engine.geometry.floorContours;
 geometry[biome]={floor:contours?contours.map(r=>r.map(p=>[p.x/width*65536,p.y/height*65536])):[[[0,0],[65536,0],[65536,65536],[0,65536]]],shortcut:[],water:[],warps:{}};
}
fs.writeFileSync(path.join(root,'docs/florrclone-collision-contours.json'),JSON.stringify(geometry));
console.log('Extracted authoritative collision for',Object.keys(geometry).length,'maps');
