'use strict';
// Imported assembler-only regions are service areas, never combat spawn bands.
// Keep the source polygons and its common-to-super regional tier allocation.
const fs=require('fs'),path=require('path');
const file=path.join(__dirname,'../maps/computer.tmj');
const map=JSON.parse(fs.readFileSync(file));
let npcs=map.layers.find(l=>l.name==='npcs');
if(!npcs){npcs={id:map.nextlayerid++,name:'npcs',type:'objectgroup',draworder:'topdown',objects:[],opacity:1,visible:true,x:0,y:0};map.layers.push(npcs);}
const tiers=['common','uncommon','rare','epic','legendary','mythic','ultra','super'];
let converted=0;
for(const layer of map.layers.filter(l=>l.name==='spawns')){
  layer.objects=layer.objects.filter(o=>{
    const props=Object.fromEntries((o.properties||[]).map(p=>[p.name,p.value]));
    if(props.mobs!=='assembler:1')return true;
    const tier=Number(props.difficulty)/10;
    if(!Number.isInteger(tier)||!tiers[tier])throw Error('Unexpected assembler region difficulty');
    npcs.objects.push({...o,name:'assembler',type:'npcs',properties:[
      {name:'npc',type:'string',value:'assembler'},
      {name:'rarity',type:'string',value:tiers[tier]}]});
    ++converted;return false;
  });
}
fs.writeFileSync(file,JSON.stringify(map));
console.log('Restored '+converted+' factory assembler service regions');
