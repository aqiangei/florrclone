#!/usr/bin/env node
// Rebuild the three rotation layouts from the shipped Pyramid artwork and collision.
// The primary pyramid.tmj is the authored source and is never overwritten.
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const maps = path.join(__dirname, '..', 'maps');
const source = JSON.parse(fs.readFileSync(path.join(maps, 'pyramid.tmj'), 'utf8'));
const {width, height, tilewidth, tileheight} = source;
const collision = source.layers.find(layer => layer.name === 'source_collision').data;

// Each shortcut copies an intact junction from elsewhere on the primary map
// with the same openings along the patch boundary. Coordinates are in tiles.
const northShortcut = {x: 14, y: 3, fromX: 33, fromY: 3, size: 3};
const westShortcut = {x: 4, y: 33, fromX: 17, fromY: 33, size: 4};
const eastShortcut = {x: 31, y: 23, fromX: 37, fromY: 23, size: 4};
const layouts = [
    {id: 'pyramid_west', horizontal: false, vertical: true, shortcuts: [westShortcut, eastShortcut]},
    {id: 'pyramid_east', horizontal: true, vertical: true, shortcuts: [northShortcut, eastShortcut]},
];

execFileSync(process.execPath, [path.join(__dirname, 'generate-pyramid-maze.js'), '20261108', 'alternate'], { stdio: 'inherit' });

for (const {id, horizontal, vertical, shortcuts} of layouts) {
    const map = structuredClone(source);
    const wallTiles = collision.slice();
    for (const {x, y, fromX, fromY, size} of shortcuts) {
        for (let row = 0; row < size; ++row) {
            for (let col = 0; col < size; ++col) {
                wallTiles[(y + row) * width + x + col] =
                    collision[(fromY + row) * width + fromX + col];
            }
        }
    }

    const flip = (horizontal ? 0x80000000 : 0) | (vertical ? 0x40000000 : 0);
    for (const layer of map.layers) {
        if (layer.type === 'tilelayer') {
            const tiles = layer.name === 'source_collision' ? wallTiles : layer.data;
            layer.data = tiles.map((_, index) => {
                const x = index % width, y = Math.floor(index / width);
                const sourceX = horizontal ? width - 1 - x : x;
                const sourceY = vertical ? height - 1 - y : y;
                const gid = tiles[sourceY * width + sourceX];
                return gid ? (gid ^ flip) >>> 0 : 0;
            });
        } else if (layer.name === 'player_spawns' || layer.name === 'teleporters') {
            for (const object of layer.objects) {
                if (horizontal) object.x = width * tilewidth - object.x - object.width;
                if (vertical) object.y = height * tileheight - object.y - object.height;
                if (layer.name === 'player_spawns') {
                    object.name = id + '-entry';
                    const pickable = object.properties.find(property => property.name === 'pickable');
                    if (pickable) pickable.value = false;
                    else object.properties.push({name: 'pickable', type: 'bool', value: false});
                }
            }
        }
        // The primary map's only spawn band covers the full map, so it
        // remains in place with the same roster, density and difficulty.
    }
    delete map.florrcloneSource;
    fs.writeFileSync(path.join(maps, id + '.tmj'), JSON.stringify(map, null, 2) + '\n');
}
