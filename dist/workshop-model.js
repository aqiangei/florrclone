'use strict';

(function () {
const textures = {
    wall:'#68452d', voidWall:'#1e1e1e', arcticWall:'#a3d8cd', jungleWall:'#32a852',
    desertWall:'#d9c598', oceanWall:'#cd7c92', sewersWall:'#666632', bioWall:'#e29f6a',
    helWall:'#742a2a', dinoWall:'#6f6f6f', dirt:'#99724c', grass:'#1ea660', jungle:'#34854e',
    ice:'#c9ece5', bio:'#ddc599', arrows:'#8a9297', desert:'#dfd2b0', hel:'#8d3838',
    void:'#1a1a1a', dino:'#583214', ocean:'#608bb7', sewers:'#343434', matrix:'#172b1a'
};

function createMap(size = 50) {
    return {name:'', creator:'', desc:'', mapSize:50000, wallOpacity:1, wallColor:'#68452d',
        wallOutlineColor:'#503523', overlayColor:'#ff0000', overlayOpacity:0, texture:'grass',
        tiles:Array.from({length:size}, () => Array(size).fill(1)), spawnPoints:[], respawnPoints:[], regions:[]};
}

const base64 = bytes => btoa(Array.from(bytes, byte => String.fromCharCode(byte)).join(''));
const bytesOf = value => Uint8Array.from(atob(value), char => char.charCodeAt(0));
const viewOf = bytes => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

function packGrid(grid) {
    const size = grid.length;
    const bytes = new Uint8Array(4 + Math.ceil(size * size / 8));
    viewOf(bytes).setUint32(0, size);
    for (let i = 0; i < size * size; i++)
        if (grid[Math.floor(i / size)][i % size]) bytes[4 + (i >> 3)] |= 1 << (i % 8);
    return base64(bytes);
}

function unpackGrid(code) {
    const bytes = bytesOf(code), size = viewOf(bytes).getUint32(0);
    if (size < 1 || size > 100 || bytes.length !== 4 + Math.ceil(size * size / 8)) throw Error('Invalid tile grid');
    return Array.from({length:size}, (_, y) => Array.from({length:size}, (_, x) => {
        const i = y * size + x;
        return (bytes[4 + (i >> 3)] >> (i % 8)) & 1;
    }));
}

function packPoints(points) {
    const bytes = new Uint8Array(4 + points.length * 4), view = viewOf(bytes);
    view.setUint32(0, points.length);
    points.forEach(([x, y], i) => {
        view.setUint16(4 + i * 4, x);
        view.setUint16(6 + i * 4, y);
    });
    return base64(bytes);
}

function unpackPoints(code) {
    const bytes = bytesOf(code), view = viewOf(bytes), count = view.getUint32(0);
    if (count > 10000 || bytes.length !== 4 + count * 4) throw Error('Invalid point list');
    return Array.from({length:count}, (_, i) => [view.getUint16(4 + i * 4), view.getUint16(6 + i * 4)]);
}

function transform(bytes, seed, encode) {
    function swap() {
        for (let i = 0; i + 1 < bytes.length; i += 2) [bytes[i], bytes[i + 1]] = [bytes[i + 1], bytes[i]];
    }
    const next = () => {
        seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
        return Math.floor(seed / 4294967296 * 255);
    };
    if (encode) swap();
    for (let i = 0; i < bytes.length; i++) bytes[i] ^= next() ^ next();
    if (!encode) swap();
}

function encodeMap(map, seed = Math.floor(Math.random() * 4294967296)) {
    const json = JSON.stringify(map, (key, value) => {
        if (key === 'tiles') return packGrid(value);
        if (/points$/i.test(key)) return packPoints(value);
        return value;
    });
    const payload = new TextEncoder().encode(json);
    transform(payload, seed, true);
    const bytes = new Uint8Array(payload.length + 8);
    viewOf(bytes).setUint32(0, 1);
    viewOf(bytes).setUint32(4, seed);
    bytes.set(payload, 8);
    return base64(bytes);
}

function decodeMap(code) {
    if (typeof code !== 'string' || code.length > 1500000) throw Error('Map code is too large');
    const bytes = bytesOf(code.trim()), view = viewOf(bytes);
    if (bytes.length < 8) throw Error('Invalid map code');
    if (view.getUint32(0) !== 1) throw Error('Unsupported map code version');
    const payload = bytes.subarray(8);
    transform(payload, view.getUint32(4), false);
    const data = JSON.parse(new TextDecoder('utf-8', {fatal:true}).decode(payload), (key, value) => {
        if (key === 'tiles') return unpackGrid(value);
        if (/points$/i.test(key)) return unpackPoints(value);
        return value;
    });
    if (!data || !Array.isArray(data.tiles) || !Array.isArray(data.regions) || data.regions.length > 100)
        throw Error('Invalid map');
    const size = data.tiles.length;
    const points = input => {
        if (!Array.isArray(input) || input.some(point => point.some(n => n >= size))) throw Error('Point is outside the map');
        return [...new Map(input.map(point => [point.join(','), point])).values()];
    };
    const text = (value, max) => typeof value === 'string' ? value.slice(0, max) : '';
    const number = (value, low, high, fallback) => Number.isFinite(value) ? Math.max(low, Math.min(high, value)) : fallback;
    const color = (value, fallback) => /^#(?:[0-9a-f]{3}|[0-9a-f]{6})$/i.test(value)
        ? value.length === 4 ? '#' + [...value.slice(1)].map(c => c + c).join('') : value : fallback;
    const map = {...createMap(1), tiles:data.tiles,
        name:text(data.name, 100), creator:text(data.creator, 100), desc:text(data.desc, 1000),
        mapSize:number(data.mapSize, 1000, 500000, 50000),
        wallOpacity:number(data.wallOpacity, 0, 1, 1), wallColor:color(data.wallColor, '#68452d'),
        wallOutlineColor:color(data.wallOutlineColor, '#503523'), overlayColor:color(data.overlayColor, '#ff0000'),
        overlayOpacity:number(data.overlayOpacity, 0, 1, 0),
        texture:Object.prototype.hasOwnProperty.call(textures, data.texture) ? data.texture : 'grass',
        spawnPoints:points(data.spawnPoints ?? []), respawnPoints:points(data.respawnPoints ?? [])};
    map.regions = data.regions.map(region => {
        if (!validMobList(region.mobs)) throw Error('Invalid monster weights');
        return {name:text(region.name, 100), desc:text(region.desc, 1000), points:points(region.points),
            mobs:region.mobs.map(([id, weight]) => [id, weight]), density:number(region.density, 0, 5, .5),
            difficulty:number(region.difficulty, -100, 200, 30)};
    });
    return map;
}

function validMobList(list) {
    return Array.isArray(list) && list.length <= 100 && list.every(row =>
        Array.isArray(row) && row.length === 2 && typeof row[0] === 'string' &&
        row[0].length <= 100 && Number.isFinite(row[1]) && row[1] >= 0);
}

function regionRates(difficulty) {
    const tiers = ['Common', 'Unusual', 'Rare', 'Epic', 'Legendary', 'Mythic', 'Ultra', 'Super', 'Omega'];
    const exponent = 1.25 ** difficulty;
    let previous = 0;
    const rates = tiers.flatMap((tier, index) => {
        const cumulative = 1 - 18 ** -(index + 1);
        const chance = Number(((cumulative ** exponent - previous ** exponent) * 100).toFixed(2));
        previous = cumulative;
        return chance > .1 ? [[tier, chance]] : [];
    });
    const total = rates.reduce((sum, [, chance]) => sum + chance, 0);
    return rates.length ? rates.map(([tier, chance]) => [tier, chance * 100 / total]) : [['Unique', 0]];
}

function previewMap(map, catalog) {
    const size = map.tiles.length;
    const tile = 256;
    const layer = (id, name, type) => ({id, name, type, opacity:1, visible:true, x:0, y:0});
    const prop = (name, type, value) => ({name, type, value});
    const object = (id, x, y) => ({id, x, y, width:0, height:0, rotation:0, visible:true, type:''});
    const cells = map.tiles.flat();
    const firstOpen = cells.indexOf(1);
    if (firstOpen < 0) throw Error('测试地图需要至少一个可走格。');
    const open = ([x,y]) => map.tiles[y][x] === 1;
    const spawn = map.spawnPoints.filter(open);
    const respawn = map.respawnPoints.filter(open);
    const doors = spawn.length ? spawn : [[firstOpen % size, Math.floor(firstOpen / size)]];
    const ground = {grass:6, desert:1, dirt:25, ocean:10, sewers:54, hel:61, jungle:66,
        dino:25, matrix:95, void:20, bio:1, ice:50}[map.texture] || 6;
    const regions = [];
    const missing = new Set();
    let id = 1;
    const marker = (point, name, pickable) => {
        const [x,y] = point;
        return {...object(id++, x * tile + 64, y * tile + 64), width:128, height:128, name,
            properties:[prop('spawnId','string',name), prop('pickable','bool',pickable)]};
    };
    const start = Math.floor(Math.random() * doors.length);
    [doors[0], doors[start]] = [doors[start], doors[0]];
    const entries = doors.map((point, index) => marker(point,
        index ? 'workshop_start_' + index : 'workshop_preview_entry', index === 0));
    entries.push(...respawn.map((point,index) => marker(point, 'workshop_respawn_' + index, false)));
    for (const region of map.regions) {
        if (region.density === 0 || region.points.length === 0) continue;
        const mobs = region.mobs.filter(([slug, share]) => {
            if (share > 0 && !catalog[slug]?.spawnable) missing.add(slug);
            return share > 0 && catalog[slug]?.spawnable;
        });
        if (!mobs.length) continue;
        // Compact each contiguous row into one rectangle. No band covers walls
        // or holes in the original painted region.
        const byRow = new Map();
        for (const [x,y] of region.points.filter(open)) {
            if (!byRow.has(y)) byRow.set(y, []);
            byRow.get(y).push(x);
        }
        for (const [y, points] of byRow) {
            points.sort((a,b) => a-b);
            for (let i = 0; i < points.length;) {
                const first = points[i++];
                let last = first;
                while (points[i] === last + 1) last = points[i++];
                regions.push({...object(id++, first * tile, y * tile), width:(last-first+1)*tile, height:tile,
                    properties:[prop('difficulty','float',region.difficulty), prop('density','float',region.density),
                        prop('mobs','string',mobs.map(([mob,weight]) => mob+':'+weight).join(' '))]});
            }
        }
    }
    if (missing.size) throw Error('试玩找不到这些怪物：' + [...missing].join('、'));
    const tiles = (id,name,collision,data) => ({...layer(id,name,'tilelayer'), width:size,height:size,data,
        properties:[prop('has_collision','bool',collision)]});
    return {type:'map',version:'1.10',orientation:'orthogonal',renderorder:'right-down',infinite:false,
        width:size,height:size,tilewidth:tile,tileheight:tile,nextlayerid:5,nextobjectid:id,
        tilesets:[{firstgid:1,source:'tileset.tsj'}],
        properties:[prop('displayName','string',map.name || 'Unnamed'),prop('biome','string','workshop_preview'),
            prop('backgroundColor','color',textures[map.texture])],
        layers:[tiles(1,'ground',false,cells.map(() => ground)),tiles(2,'walls',true,cells.map(cell => cell ? 0 : 90)),
            {...layer(3,'player_spawns','objectgroup'),objects:entries},
            {...layer(4,'spawns','objectgroup'),objects:regions}]};
}

const model = {textures, createMap, encodeMap, decodeMap, validMobList, regionRates, previewMap};
if (typeof module !== 'undefined' && module.exports) module.exports = model;
else globalThis.WorkshopModel = model;
})();
