'use strict';
// Restore the original common-to-apex jungle practice row, aligned to the
// imported map's spawn centre. NPCs are deliberately not ambient spawn bands.
const fs=require('fs'),path=require('path');
const file=path.join(__dirname,'../maps/jungle.tmj');const map=JSON.parse(fs.readFileSync(file));
const spawn=map.layers.find(l=>l.name==='player_spawns').objects[0];
const offset={x:spawn.x+spawn.width/2-3195.83333333333,y:spawn.y+spawn.height/2-3070.833333333335};
const row=[['common',1776,1152],['uncommon',1312,1880],['rare',1352,2960],['epic',1400,4304],['legendary',2352,5024],['mythic',3688,5128],['ultra',4768,5088],['super',5352,4376],['unique',5492.66666666667,2876],['apex',5988.66666666667,1395.33333333333]];
let layer=map.layers.find(l=>l.name==='npcs');if(!layer){layer={draworder:'topdown',id:map.nextlayerid++,name:'npcs',objects:[],opacity:1,type:'objectgroup',visible:true,x:0,y:0};map.layers.push(layer);}
layer.objects=layer.objects.filter(o=>!(o.properties||[]).some(p=>p.name==='npc'&&p.value==='target_dummy'));
for(const [rarity,x,y] of row)layer.objects.push({height:0,id:map.nextobjectid++,name:'target_dummy',point:true,properties:[{name:'npc',type:'string',value:'target_dummy'},{name:'rarity',type:'string',value:rarity}],rotation:0,type:'npcs',visible:true,width:0,x:x+offset.x,y:y+offset.y});
fs.writeFileSync(file,JSON.stringify(map));console.log('Restored 10 jungle practice NPCs (common through apex)');
