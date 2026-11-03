"""Generate tier-sized mound interiors, with matching vector wall art and collision."""
import copy,json,hashlib,math
from pathlib import Path
from shapely.geometry import Polygon,box
from shapely.ops import unary_union,triangulate
from shapely.affinity import scale,translate
ROOT=Path(__file__).resolve().parent.parent
canonical=ROOT/'scripts/fixtures/termite-mound-original.tmj'
if not canonical.exists():
 canonical.parent.mkdir(exist_ok=True);canonical.write_bytes((ROOT/'maps/termite_mound.tmj').read_bytes())
base=json.loads(canonical.read_text(encoding='utf-8'))
sets=json.loads((ROOT/'maps/tileset.tsj').read_text(encoding='utf-8'))
tiles={t['id']+1:t for t in sets['tiles']}
shapes=[]
for layer in base['layers']:
 if layer.get('type')!='tilelayer' or layer['name']=='background':continue
 for i,gid in enumerate(layer['data']):
  if not gid:continue
  tile=tiles[gid&0x1fffffff]
  for o in tile.get('objectgroup',{}).get('objects',[]):
   points=o.get('polygon',[{'x':0,'y':0},{'x':o['width'],'y':0},{'x':o['width'],'y':o['height']},{'x':0,'y':o['height']}]) if 'polygon' not in o else o['polygon']
   out=[]
   for q in points:
    x,y=q['x']+o.get('x',0),q['y']+o.get('y',0)
    if gid&0x20000000:x,y=y,x
    if gid&0x80000000:x=256-x
    if gid&0x40000000:y=256-y
    out.append((x+(i%32)*256,y+(i//32)*256))
   shapes.append(Polygon(out).buffer(0))
wall=unary_union(shapes)
manifest=json.loads((ROOT/'maps/maps.json').read_text(encoding='utf-8'))
manifest['maps']=[e for e in manifest['maps'] if not (isinstance(e,dict) and e.get('file','').startswith('termite_mound_t'))]
records=[]
for tier in range(12):
 side=16+4*tier;factor=side/32;shape=scale(wall,xfact=factor,yfact=factor,origin=(0,0))
 collision=[];images=[];data=[]
 for y in range(side):
  for x in range(side):
   piece=shape.intersection(box(x*256,y*256,(x+1)*256,(y+1)*256))
   if piece.is_empty or piece.area<.01:data.append(0);continue
   local=translate(piece,xoff=-x*256,yoff=-y*256)
   polygons=[local] if local.geom_type=='Polygon' else [p for p in getattr(local,'geoms',[]) if p.geom_type=='Polygon']
   objects=[];paths=[]
   for poly in polygons:
    for ring in [poly.exterior,*poly.interiors]:
     coords=list(ring.coords)[:-1];paths.append('M'+'L'.join(f'{a:.4f} {b:.4f}' for a,b in coords)+'Z')
    pieces=[poly] if not poly.interiors else [t.intersection(poly) for t in triangulate(poly)]
    for part in pieces:
     if part.geom_type!='Polygon' or part.area<.01:continue
     pts=[{'x':round(a,4),'y':round(b,4)} for a,b in list(part.exterior.coords)[:-1]]
     objects.append({'id':len(objects)+1,'x':0,'y':0,'width':0,'height':0,'rotation':0,'visible':True,'polygon':pts})
   d=''.join(paths);digest=hashlib.sha256(d.encode()).hexdigest()[:16];name=f'mound_wall_{digest}.svg'
   if name not in images:
    marks=''.join(f'<path d="M{px} {py}l8 -2 3 7 -7 5z" fill="#62432e"/>' for py in range(12,256,64) for px in range(12,256,64) if local.covers(box(px-1,py-3,px+12,py+11)))
    svg=f'<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256"><path d="{d}" fill="#745137" fill-rule="evenodd" stroke="#513b29" stroke-width="5" stroke-linejoin="round"/>{marks}</svg>\n' 
    (ROOT/'maps/tiles'/name).write_text(svg,encoding='utf-8');images.append(name)
    collision.append({'id':len(collision),'image':'tiles/'+name,'imagewidth':256,'imageheight':256,'objectgroup':{'type':'objectgroup','objects':objects}})
   data.append(images.index(name)+2000)
 tsname=f'termite_mound_t{tier}_collision.tsj'
 ts={'type':'tileset','version':'1.10','name':f'mound_t{tier}','tilewidth':256,'tileheight':256,'tilecount':len(collision),'columns':0,'grid':{'orientation':'orthogonal','width':1,'height':1},'tiles':collision}
 (ROOT/'maps'/tsname).write_text(json.dumps(ts,separators=(',',':')),encoding='utf-8')
 m=copy.deepcopy(base);m['width']=m['height']=side;m['tilesets'].append({'firstgid':2000,'source':tsname})
 bg=m['layers'][0];bg['width']=bg['height']=side;bg['data']=[28+((x*17+y*23)%2) for y in range(side) for x in range(side)]
 m['layers']=[bg,{'id':99,'name':'mound_walls','properties':[{'name':'has_collision','type':'bool','value':True}],'type':'tilelayer','width':side,'height':side,'x':0,'y':0,'visible':True,'opacity':1,'data':data}]+[l for l in m['layers'] if l.get('type')=='objectgroup']
 for l in m['layers']:
  for o in l.get('objects',[]):
   for k in ['x','y','width','height']:
    if k in o:o[k]*=factor
 m['properties']=[p for p in m.get('properties',[]) if p['name']!='moundRarity']+[{'name':'moundRarity','type':'int','value':tier}]
 filename='termite_mound.tmj' if tier==0 else f'termite_mound_t{tier}.tmj'
 (ROOT/'maps'/filename).write_text(json.dumps(m,separators=(',',':')),encoding='utf-8')
 if tier:manifest['maps'].append({'file':filename,'copies':8})
 records.append({'rarityIndex':tier,'tiles':side,'worldSize':side*256,'wallTiles':len(collision)})
(ROOT/'maps/maps.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
(ROOT/'docs/termite-interior-tier-audit.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
print(json.dumps({'tiers':12,'copiesPerTier':8,'sizes':records}))
