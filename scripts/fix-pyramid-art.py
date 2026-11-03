"""Restore Pyramid floor/wall art without touching any collision polygons."""
import json, re, hashlib
from pathlib import Path
root = Path(__file__).resolve().parent.parent
map_path = root / 'maps/pyramid.tmj'
collision_path = root / 'maps/pyramid_collision.tsj'
m = json.loads(map_path.read_text(encoding='utf-8'))
c = json.loads(collision_path.read_text(encoding='utf-8'))
wall = (root / 'maps/tiles/pyramid_wall_c_0.svg').read_text(encoding='utf-8')
wall = re.sub(r'^.*?<svg[^>]*>', '', wall, flags=re.S).rsplit('</svg>', 1)[0]
assets = set()
for tile in c['tiles']:
    paths = []
    for obj in tile['objectgroup']['objects']:
        points = [(round(obj['x']+v['x'],4),round(obj['y']+v['y'],4)) for v in obj['polygon']]
        paths.append('M'+'L'.join(f'{x:g} {y:g}' for x,y in points)+'Z')
    d = ''.join(paths)
    digest = hashlib.sha256(d.encode()).hexdigest()[:16]
    name = f'pyramid_wall_clip_{digest}.svg'
    svg = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256"><defs><clipPath id="wall"><path d="{d}"/></clipPath></defs><g clip-path="url(#wall)">{wall}</g></svg>\n'
    (root / 'maps/tiles' / name).write_text(svg, encoding='utf-8')
    tile['image'] = 'tiles/'+name
    assets.add(name)
for layer in m['layers']:
    if layer['name']=='source_collision': layer['visible']=True
    if layer['name']=='bg': layer['data']=[2000+(i*1103515245+12345)%4 for i in range(len(layer['data']))]
if not any(t.get('source')=='pyramid_floor.tsj' for t in m['tilesets']):
    m['tilesets'].append({'firstgid':2000,'source':'pyramid_floor.tsj'})
props = {p['name']:p for p in m['properties']}
props['minimap']={'name':'minimap','type':'bool','value':False}
m['properties']=list(props.values())
floor={'type':'tileset','name':'pyramid_floor','tilewidth':256,'tileheight':256,'tilecount':4,'columns':0,
       'tiles':[{'id':i,'image':f'tiles/pyramid_floor_c_{i}.svg','imagewidth':256,'imageheight':256} for i in range(4)]}
(root/'maps/pyramid_floor.tsj').write_text(json.dumps(floor,separators=(',',':'))+'\n',encoding='utf-8')
collision_path.write_text(json.dumps(c,separators=(',',':'))+'\n',encoding='utf-8')
map_path.write_text(json.dumps(m,separators=(',',':'),ensure_ascii=False)+'\n',encoding='utf-8')
print(f'Pyramid restored: 4 floors, {len(assets)} clipped wall SVGs, collision objects unchanged')
