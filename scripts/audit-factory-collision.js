const fs=require('fs');
const biome=process.argv.find(a=>a.startsWith('--map='))?.slice(6)||'Factory';
const slugs={'Garden':'garden','Desert':'desert','Ocean':'ocean','Jungle':'jungle','Hel':'hel','Ant Hell':'ant_hell','Sewers':'sewers','Factory':'computer','Worm':'worm','Pyramid':'pyramid','Super':'super','Swarm':'swarm','dps test':'dps_test'};
const map=JSON.parse(fs.readFileSync('maps/'+slugs[biome]+'.tmj')),ts=JSON.parse(fs.readFileSync('maps/tileset.tsj')),src=JSON.parse(fs.readFileSync('docs/florrclone-collision-contours.json'))[biome];
for(const ref of map.tilesets.filter(t=>t.source!=='tileset.tsj')){const extra=JSON.parse(fs.readFileSync('maps/'+ref.source));ts.tiles.push(...extra.tiles.map(t=>({...t,id:t.id+ref.firstgid-1})));}
function winding(r,x,y){let w=0;for(let i=0;i<r.length;i++){const a=r[i],b=r[(i+1)%r.length],c=(b[0]-a[0])*(y-a[1])-(x-a[0])*(b[1]-a[1]);if(a[1]<=y&&b[1]>y&&c>0)w++;if(a[1]>y&&b[1]<=y&&c<0)w--;}return w;}
if(process.argv.includes('--before'))for(const t of ts.tiles){
if(t.image.endsWith('factory_wall_l_0.svg'))t.objectgroup.objects=[{x:0,y:0,polygon:[[0,0],[130,0],[130,64],[140,86],[137,113],[113,137],[86,140],[64,130],[0,130]].map(([x,y])=>({x,y}))}];
if(/factory_wall_(tl_0|tli)\.svg$/.test(t.image))t.objectgroup.objects=[{x:0,y:0,width:256,height:256}];
}
const tiles=new Map(ts.tiles.map(t=>[t.id+1,t]));const layers=map.layers.filter(l=>l.data);const examples=[];const differences={};let mismatch=0,total=0;
for(let cy=0;cy<map.height;cy++)for(let cx=0;cx<map.width;cx++)for(const fy of [.21,.51,.81])for(const fx of [.21,.51,.81]){
const x=(cx+fx)/map.width*65536,y=(cy+fy)/map.height*65536;const sourceFree=[src.floor,src.shortcut].some(rings=>rings.reduce((n,r)=>n+winding(r,x,y),0)!==0);let blocked=false,last='none';
for(const l of layers){const raw=l.data[cy*map.width+cx]>>>0,t=tiles.get(raw&0x1fffffff);if(!t)continue;const props=Object.fromEntries((l.properties||[]).map(p=>[p.name,p.value]));if(props.negate_collision){blocked=false;continue;}if(!props.has_collision)continue;let u=fx,v=fy;if(raw&0x80000000)u=1-u;if(raw&0x40000000)v=1-v;if(raw&0x20000000)[u,v]=[v,u];for(const o of t.objectgroup?.objects||[]){let inside;if(o.polygon)inside=winding(o.polygon.map(p=>[p.x+(o.x||0),p.y+(o.y||0)]),u*256,v*256)!==0;else inside=u*256>=o.x&&u*256<=o.x+o.width&&v*256>=o.y&&v*256<=o.y+o.height;if(inside){blocked=true;last=t.image;}}}
total++;if(blocked===sourceFree){mismatch++;if(examples.length<12)examples.push({cx,cy,fx,fy,sourceFree,last});differences[last]=(differences[last]||0)+1;}}
console.log({total,mismatch,percent:mismatch/total*100,differences,examples});
fs.writeFileSync('docs/'+slugs[biome]+'-collision-audit.json',JSON.stringify({total,mismatch,percent:mismatch/total*100,differences},null,2));
