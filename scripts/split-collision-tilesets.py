import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=ROOT/'maps/tileset.tsj';ts=json.loads(p.read_text())
base=[t for t in ts['tiles'] if t.get('image')!='tiles/collision_empty.svg']
first=max(t['id'] for t in base)+2
by_gid={t['id']+1:t for t in ts['tiles']}
for mp in (ROOT/'maps').glob('*.tmj'):
    m=json.loads(mp.read_text())
    layer=next((l for l in m['layers'] if l.get('name')=='source_collision'),None)
    if not layer or any(t.get('source','').endswith('_collision.tsj') for t in m['tilesets']):continue
    ids=sorted(set(layer['data'])-{0});mapping={gid:first+i for i,gid in enumerate(ids)}
    tiles=[]
    for i,gid in enumerate(ids):
        tile=dict(by_gid[gid]);tile['id']=i;tiles.append(tile)
    if not ids:
        mp.write_text(json.dumps(m,separators=(',',':'))+'\n')
        continue
    filename=mp.stem+'_collision.tsj'
    subset={'type':'tileset','name':mp.stem+'_collision','tilewidth':256,'tileheight':256,'tilecount':len(tiles),'columns':0,'tiles':tiles}
    (ROOT/'maps'/filename).write_text(json.dumps(subset,separators=(',',':'))+'\n')
    m['tilesets'].append({'firstgid':first,'source':filename})
    layer['data']=[mapping.get(gid,0) for gid in layer['data']]
    mp.write_text(json.dumps(m,separators=(',',':'))+'\n')
ts['tiles']=base;ts['tilecount']=len(base)
p.write_text(json.dumps(ts,indent=1)+'\n')
print('Collision palettes split by map; shared artwork palette remains small.')
