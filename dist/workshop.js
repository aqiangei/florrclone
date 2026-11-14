'use strict';

const {textures, createMap, encodeMap, decodeMap, validMobList, regionRates, previewMap} = WorkshopModel;
const $ = id => document.getElementById(id);
const canvas = $('map'), context = canvas.getContext('2d');
const key = 'ashflor/map-editor';
let map = createMap(), region = null, mode = '', panel = '', solid = true;
let zoom = 1, offsetX = 0, offsetY = 0, pointer = null, saveTimer = null, catalog = {};
let statusTimer;
let codeEdited = false;

function message(text) {
    $('status').textContent = text;
    clearTimeout(statusTimer);
    statusTimer = setTimeout(() => { $('status').textContent = ''; }, 4000);
}
function number(input, fallback) {
    const value = input.valueAsNumber;
    return Number.isFinite(value) ? Math.min(Number(input.max), Math.max(Number(input.min), value)) : fallback;
}
function viewport() {
    const {width,height} = canvas.getBoundingClientRect();
    const size = Math.min(width, height) * .60 * zoom;
    return {width,height,size,x:(width-size)/2+offsetX,y:(height-size)/2+offsetY,unit:size/map.tiles.length};
}
function regionColor(value) {
    const colors = ['#78df62','#ffe452','#5785ec','#b66ffa','#e45753','#49dce2','#f270d9','#7eeeef'];
    return colors[Math.min(colors.length-1, Math.max(0, Math.floor(value/25)))];
}
function draw() {
    const view = viewport(), ratio = window.devicePixelRatio || 1;
    const {width,height,size,x,y,unit} = view;
    if (canvas.width !== Math.round(width*ratio) || canvas.height !== Math.round(height*ratio)) {
        canvas.width = Math.round(width*ratio);
        canvas.height = Math.round(height*ratio);
    }
    context.setTransform(ratio,0,0,ratio,0,0);
    context.fillStyle = '#176039';
    context.fillRect(0,0,width,height);
    context.strokeStyle = '#123e292e';
    context.beginPath();
    for (let i=0;i<width;i+=50) {context.moveTo(i,0);context.lineTo(i,height);}
    for (let i=0;i<height;i+=50) {context.moveTo(0,i);context.lineTo(width,i);}
    context.stroke();
    context.fillStyle = textures[map.texture];
    context.fillRect(x,y,size,size);
    context.fillStyle = '#0005';
    context.fillRect(x,y,size,size);
    context.save();
    context.globalAlpha = map.wallOpacity;
    context.fillStyle = map.wallColor;
    context.strokeStyle = map.wallOutlineColor;
    map.tiles.forEach((row,j) => row.forEach((tile,i) => {
        if (tile) return;
        context.fillRect(x+i*unit,y+j*unit,unit,unit);
        context.strokeRect(x+i*unit,y+j*unit,unit,unit);
    }));
    context.restore();
    function points(list,color,alpha) {
        context.save();
        context.globalAlpha = alpha;
        context.fillStyle = color;
        for (const [i,j] of list) context.fillRect(x+i*unit,y+j*unit,unit,unit);
        context.restore();
    }
    const hide = $('hideOthers').checked && mode === 'region';
    for (const band of map.regions) {
        if (hide && band !== region) continue;
        points(band.points,regionColor(band.difficulty),.52);
        if ($('simulate').checked && band.density > 0 && band.mobs.some(([,share]) => share > 0)) {
            context.fillStyle = '#0007';
            const step = Math.max(1,Math.round(5/band.density));
            for (let i=0;i<band.points.length;i+=step) {
                const [px,py] = band.points[i];
                context.beginPath();
                context.arc(x+(px+.5)*unit,y+(py+.5)*unit,unit*.24,0,Math.PI*2);
                context.fill();
            }
        }
    }
    if (!hide) {
        points(map.spawnPoints,'#ff4949',.7);
        points(map.respawnPoints,'#ffa443',.7);
    }
    context.save();
    context.globalAlpha = map.overlayOpacity;
    context.fillStyle = map.overlayColor;
    context.fillRect(x,y,size,size);
    context.globalAlpha = number($('gridOpacity'),.1);
    context.lineWidth = number($('gridSize'),.35);
    context.strokeStyle = $('gridColor').value;
    context.beginPath();
    for (let i=0;i<=map.tiles.length;i++) {
        context.moveTo(x+i*unit,y);context.lineTo(x+i*unit,y+size);
        context.moveTo(x,y+i*unit);context.lineTo(x+size,y+i*unit);
    }
    context.stroke();
    context.restore();
}
function writeCode() {
    clearTimeout(saveTimer);
    saveTimer = null;
    const code = encodeMap(map);
    if (!codeEdited) $('code').value = code;
    try { localStorage.setItem(key, code); } catch {}
    return code;
}
function changed() {
    draw();
    clearTimeout(saveTimer);
    saveTimer = setTimeout(writeCode, 60);
}
function titles() {
    $('mapTitle').textContent = map.name || 'Unnamed';
    $('mapCreator').textContent = 'by ' + (map.creator || 'Unnamed');
    $('mapDesc').textContent = map.desc;
}
function showPanel(name) {
    panel = panel === name ? '' : name;
    for (const id of ['settings','regions','io']) $(id).hidden = id !== panel;
    for (const button of document.querySelectorAll('[data-panel]'))
        button.setAttribute('aria-pressed', button.dataset.panel === panel);
    if (panel === 'io') writeCode();
}
function edit(modeName, label) {
    mode = mode === modeName ? '' : modeName;
    $('brush').hidden = !mode;
    $('painting').textContent = 'Editing: ' + label;
    canvas.style.cursor = mode ? 'crosshair' : 'grab';
    draw();
}
function refreshRegions() {
    const search = $('search').value.trim().toLowerCase();
    $('regionsList').replaceChildren(...map.regions.flatMap((item,index) => {
        if (search && !item.name.toLowerCase().includes(search)) return [];
        const li = document.createElement('li'), button = document.createElement('button');
        button.textContent = (index+1) + '. ' + (item.name || 'Unnamed');
        button.onclick = () => selectRegion(item);
        li.append(button);
        return [li];
    }));
}
function selectRegion(item) {
    region = item;
    mode = '';
    edit('region', region.name || 'Region');
    refreshRegion();
}
function refreshRegion() {
    $('regionEdit').hidden = !region;
    if (!region) return;
    for (const [id,name] of [['regionName','name'],['regionDesc','desc'],['density','density'],['difficulty','difficulty']])
        $(id).value = region[name];
    $('mobList').value = JSON.stringify(region.mobs);
    $('rarityRates').textContent = regionRates(region.difficulty)
        .map(([tier, chance]) => tier + ' ' + chance.toFixed(2) + '%').join(' · ');
    const total = region.mobs.reduce((sum, [, share]) => sum + share, 0);
    $('weights').replaceChildren(...region.mobs.map(([mob,share],index) => {
        const li = document.createElement('li'), name = document.createElement('span'), remove = document.createElement('button');
        name.textContent = (catalog[mob]?.name ?? mob) + ' · ' + share +
            ' (' + (total ? share * 100 / total : 0).toFixed(2) + '%)';
        remove.textContent = '×';
        remove.setAttribute('aria-label','Remove ' + mob);
        remove.onclick = () => { region.mobs.splice(index,1); refreshRegion(); changed(); };
        li.append(name,remove);
        return li;
    }));
}
function refresh() {
    region = null;
    mode = '';
    $('brush').hidden = true;
    $('tileCount').value = map.tiles.length;
    for (const input of document.querySelectorAll('[data-setting]')) input.value = map[input.dataset.setting];
    codeEdited = false;
    titles();refreshRegions();refreshRegion();writeCode();draw();
}
function paint(event) {
    const v = viewport(), size = map.tiles.length;
    const brush = Math.round(number($('brushSize'),1));
    const left = Math.round((event.clientX-v.x)/v.unit-brush/2);
    const top = Math.round((event.clientY-v.y)/v.unit-brush/2);
    const wall = solid !== event.shiftKey;
    const list = mode === 'spawn' ? map.spawnPoints : mode === 'respawn' ? map.respawnPoints : region?.points;
    const points = new Map((list || []).map(point => [point.join(','),point]));
    for (let y=Math.max(0,top);y<Math.min(size,top+brush);y++) {
        for (let x=Math.max(0,left);x<Math.min(size,left+brush);x++) {
            if (mode === 'terrain') map.tiles[y][x] = wall ? 0 : 1;
            else if (!wall) points.delete(x+','+y);
            else if (map.tiles[y][x]) points.set(x+','+y,[x,y]);
        }
    }
    if (mode !== 'terrain' && list) list.splice(0,list.length,...points.values());
    changed();
}
canvas.onpointerdown = event => {
    if (pointer || event.button !== 0) return;
    canvas.setPointerCapture(event.pointerId);
    pointer = {id:event.pointerId,x:event.clientX,y:event.clientY};
    if (mode && !event.ctrlKey) paint(event);
};
canvas.onpointermove = event => {
    if (pointer?.id !== event.pointerId) return;
    if (mode && !event.ctrlKey) paint(event);
    else {
        offsetX += event.clientX-pointer.x;offsetY += event.clientY-pointer.y;
        draw();
    }
    pointer.x=event.clientX;pointer.y=event.clientY;
};
canvas.onlostpointercapture = () => { pointer=null; };
canvas.onwheel = event => {
    event.preventDefault();
    const previous = viewport();
    zoom = Math.max(.2,Math.min(12,zoom*Math.exp(-event.deltaY/600)));
    const after = viewport();
    offsetX += event.clientX-after.x-(event.clientX-previous.x)*after.unit/previous.unit;
    offsetY += event.clientY-after.y-(event.clientY-previous.y)*after.unit/previous.unit;
    draw();
};
window.addEventListener('resize',draw);
window.addEventListener('pagehide',() => { if (saveTimer !== null) writeCode(); });
$('resetView').onclick = () => { zoom=1;offsetX=offsetY=0;draw(); };
for (const id of ['wall','air']) $(id).onclick = () => {
    solid = id==='wall';
    $('wall').setAttribute('aria-pressed',solid);
    $('air').setAttribute('aria-pressed',!solid);
};
for (const id of ['simulate','hideOthers','gridColor','gridOpacity','gridSize']) $(id).oninput=draw;
for (const button of document.querySelectorAll('[data-panel]')) button.onclick=()=>showPanel(button.dataset.panel);
$('editMap').onclick=()=>edit('terrain','Map');
$('editSpawn').onclick=()=>edit('spawn','Spawn Points');
$('editRespawn').onclick=()=>edit('respawn','Respawn Points');
$('paintRegion').onclick=()=>edit('region',region.name || 'Region');
$('search').oninput=refreshRegions;
$('newMap').onclick=()=>{ map=createMap();refresh(); };
function fill(size,value) {
    map.tiles=Array.from({length:size},()=>Array(size).fill(value));
    const fits=([x,y])=>x<size&&y<size;
    map.spawnPoints=map.spawnPoints.filter(fits);
    map.respawnPoints=map.respawnPoints.filter(fits);
    for (const item of map.regions) item.points=item.points.filter(fits);
    changed();
}
$('tileCount').onchange=()=>fill(Math.round(number($('tileCount'),map.tiles.length)),1);
$('fillWall').onclick=()=>fill(map.tiles.length,0);
$('fillAir').onclick=()=>fill(map.tiles.length,1);
for (const input of document.querySelectorAll('[data-setting]')) input.oninput=()=>{
    const name=input.dataset.setting;
    map[name]=input.type==='number'?number(input,map[name]):input.value;
    titles();changed();
};
$('newRegion').onclick=()=>{
    if (map.regions.length>=100) return message('Region limit reached.');
    region={name:'',desc:'',points:[],mobs:[],density:.5,difficulty:30};
    map.regions.push(region);refreshRegions();selectRegion(region);changed();
};
$('removeRegion').onclick=()=>{
    map.regions.splice(map.regions.indexOf(region),1);
    region=null;
    if (mode==='region') edit('region','');
    refreshRegions();refreshRegion();changed();
};
for (const [id,name] of [['regionName','name'],['regionDesc','desc'],['density','density'],['difficulty','difficulty']]) {
    const input=$(id);
    input.oninput=()=>{
        region[name]=input.type==='number'?number(input,region[name]):input.value;
        if (id === 'difficulty') {
            $('rarityRates').textContent = regionRates(region.difficulty)
                .map(([tier, chance]) => tier + ' ' + chance.toFixed(2) + '%').join(' · ');
        }
        refreshRegions();changed();
    };
}
$('addMob').onclick=()=>{
    const id=$('mob').value;
    if (!id) return;
    const item=region.mobs.find(([mob])=>mob===id), share=number($('share'),10);
    if (item) item[1]=share;
    else if (region.mobs.length<100) region.mobs.push([id,share]);
    refreshRegion();changed();
};
$('mobList').onchange=()=>{
    try {
        const list = JSON.parse($('mobList').value);
        if (!validMobList(list) || list.some(([, share]) => share === 0)) throw Error();
        region.mobs = list;
        refreshRegion();changed();
    } catch {
        $('mobList').value = JSON.stringify(region.mobs);
        message('Invalid mob list.');
    }
};
function importCode(code) {
    try {map=decodeMap(code);refresh();message('Imported!');}
    catch(error) {message('Failed to import: '+error.message);}
}
$('code').oninput=()=>{codeEdited=true;};
$('import').onclick=()=>importCode($('code').value);
$('browse').onclick=()=>$('file').click();
$('file').onchange=async()=>{
    const file=$('file').files[0];
    if (!file) return;
    $('file').value='';
    if (file.size>1500000) return message('Map code is too large.');
    try {importCode(await file.text());$('filename').value=file.name.replace(/\.txt$/i,'');}
    catch {message('Could not read file.');}
};
$('copy').onclick=async()=>{
    const input=$('code');
    try {await navigator.clipboard.writeText(input.value);message('Copied!');}
    catch {input.select();if (document.execCommand('copy')) message('Copied!');}
};
$('save').onclick=()=>{
    const link=document.createElement('a');
    link.href=URL.createObjectURL(new Blob([$('code').value],{type:'text/plain'}));
    link.download=($('filename').value.trim() || String(Date.now()))+'.txt';
    link.click();
    setTimeout(()=>URL.revokeObjectURL(link.href),1000);
};
$('testMap').onclick=()=>{
    try {
        const payload={type:'ashflor-map-test', map:previewMap(map,catalog)};
        const frame=document.createElement('iframe');
        frame.title='Test Map';
        frame.sandbox='allow-scripts';
        frame.src='/workshop-preview.html?workshop';
        frame.onload=()=>frame.contentWindow.postMessage(payload,'*');
        $('preview').prepend(frame);
        $('preview').showModal();
    } catch(error) {message(error.message);}
};
$('preview').onclose=()=>{$('preview').querySelector('iframe')?.remove();};

$('texture').replaceChildren(...Object.keys(textures).map(name=>new Option(name,name)));
try {const saved=localStorage.getItem(key);if (saved) map=decodeMap(saved);} catch {}
refresh();
fetch('/workshop-mobs.json').then(response=>{
    if (!response.ok) throw Error();
    return response.json();
}).then(data=>{
    catalog=data;
    $('mob').replaceChildren(...Object.entries(catalog).filter(([,mob])=>mob.spawnable)
        .map(([id,mob])=>new Option(mob.name,id)));
}).catch(()=>message('Could not load monsters.'));
try {
    const token=localStorage.getItem('florrCouncilToken');
    if (token) fetch('/api/workshop-access',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},
        body:JSON.stringify({action:'workshop-access'})}).then(response=>response.json()).then(data=>{
        $('testMap').hidden=data.canTest!==true;
    }).catch(()=>{});
} catch {}
