"""Build exact collision tiles from florrclone's normalized walkable contours.
Requires shapely. Artwork and gameplay annotations are preserved.
"""
import json, math, sys
from pathlib import Path
from shapely.geometry import Polygon, LineString, box
from shapely.ops import unary_union, polygonize, triangulate
ROOT=Path(__file__).resolve().parents[1]
source=json.loads((ROOT/'docs/florrclone-collision-contours.json').read_text())
files={'Garden':'garden','Desert':'desert','Ocean':'ocean','Jungle':'jungle','Hel':'hel','Ant Hell':'ant_hell','Sewers':'sewers','Factory':'computer','Pyramid':'pyramid','Worm':'worm','Swarm':'swarm','Super':'super','dps test':'dps_test'}
ts_path=ROOT/'maps/tileset.tsj'
ts=json.loads(ts_path.read_text())
# Preserve generated shape IDs so individual maps can be updated safely.
next_id=max(t['id'] for t in ts['tiles'])+1
cache={}
for tile in ts['tiles']:
    if tile.get('image')=='tiles/collision_empty.svg':
        shapes=[[(p['x'],p['y']) for p in o['polygon']] for o in tile['objectgroup']['objects']]
        cache[json.dumps(shapes,separators=(',',':'))]=tile['id']+1
only=set(sys.argv[1:])
def winding(r,x,y):
    n=0
    for a,b in zip(r,r[1:]+r[:1]):
        cross=(b[0]-a[0])*(y-a[1])-(x-a[0])*(b[1]-a[1])
        if a[1]<=y<b[1] and cross>0:n+=1
        elif b[1]<=y<a[1] and cross<0:n-=1
    return n
def floor_geometry(rings,w,h):
    scaled=[[(x*w/65536,y*h/65536) for x,y in r] for r in rings if len(r)>=3]
    lines=unary_union([LineString(r+[r[0]]) for r in scaled])
    parts=[]
    for p in polygonize(lines):
        pt=p.representative_point()
        if sum(winding(r,pt.x,pt.y) for r in scaled)!=0:parts.append(p)
    return unary_union(parts)
def polygons(g):
    if g.is_empty:return []
    if g.geom_type=='Polygon':
        if not g.interiors:return [g]
        return [p for tri in triangulate(g) for p in polygons(tri.intersection(g)) if p.area>1e-7]
    return [p for item in getattr(g,'geoms',[]) for p in polygons(item)]
reports=[]
previous=ROOT/'docs/map-collision-conversion.json'
if only and previous.exists():reports=[r for r in json.loads(previous.read_text())['maps'] if r['map'] not in only]
for name,slug in files.items():
    if name not in source or (only and name not in only):continue
    path=ROOT/f'maps/{slug}.tmj'
    m=json.loads(path.read_text())
    w,h=m['width']*256,m['height']*256
    floor=floor_geometry(source[name]['floor'],w,h)
    shortcut=floor_geometry(source[name].get('shortcut',[]),w,h)
    free=floor.union(shortcut)
    blocked=box(0,0,w,h).difference(free)
    cells=[];boundary=0
    for cy in range(m['height']):
        for cx in range(m['width']):
            x,y=cx*256,cy*256
            cell=box(x,y,x+256,y+256)
            if not blocked.intersects(cell):cells.append(0);continue
            clipped=blocked.intersection(cell)
            shapes=[]
            for poly in polygons(clipped):
                if poly.area<1e-7:continue
                points=[(round(px-x,5),round(py-y,5)) for px,py in list(poly.exterior.coords)[:-1]]
                # Canonicalize the starting vertex for shared tile geometry.
                start=min(range(len(points)),key=points.__getitem__)
                points=points[start:]+points[:start]
                shapes.append(points)
            if not shapes:cells.append(0);continue
            shapes.sort()
            key=json.dumps(shapes,separators=(',',':'))
            if key not in cache:
                tid=next_id;next_id+=1;cache[key]=tid+1
                objects=[{'id':i+1,'x':0,'y':0,'width':0,'height':0,'rotation':0,'visible':True,'polygon':[{'x':px,'y':py} for px,py in pts]} for i,pts in enumerate(shapes)]
                ts['tiles'].append({'id':tid,'image':'tiles/collision_empty.svg','imagewidth':256,'imageheight':256,'objectgroup':{'type':'objectgroup','objects':objects}})
            cells.append(cache[key])
            if clipped.area<65536-1e-5:boundary+=1
    m['layers']=[l for l in m['layers'] if l.get('name')!='source_collision']
    for l in m['layers']:
        if l.get('type')=='tilelayer':
            l['properties']=[p for p in l.get('properties',[]) if p['name'] not in ('has_collision','negate_collision')]+[{'name':'has_collision','type':'bool','value':False}]
    lid=max(l['id'] for l in m['layers'])+1
    m['layers'].append({'id':lid,'name':'source_collision','type':'tilelayer','width':m['width'],'height':m['height'],'x':0,'y':0,'opacity':1,'visible':False,'data':cells,'properties':[{'name':'has_collision','type':'bool','value':True}]})
    m['tilesets']=[t for t in m['tilesets'] if not t.get('source','').endswith('_collision.tsj')]
    m['nextlayerid']=lid+1
    path.write_text(json.dumps(m,separators=(',',':'))+'\n',encoding='utf-8')
    reports.append({'map':name,'boundaryCells':boundary,'blockedCells':sum(bool(c) for c in cells),'freeArea':free.area})
ts['tilecount']=len(ts['tiles'])
ts_path.write_text(json.dumps(ts,indent=1)+'\n',encoding='utf-8')
(ROOT/'maps/tiles/collision_empty.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256"/>\n')
(ROOT/'docs/map-collision-conversion.json').write_text(json.dumps({'maps':reports,'uniqueShapes':len(cache)},indent=2)+'\n')
print(json.dumps({'maps':reports,'uniqueShapes':len(cache)},indent=2))

import subprocess
subprocess.run([sys.executable,str(ROOT/"scripts/split-collision-tilesets.py")],check=True)
