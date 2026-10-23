import bpy,json,collections
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
with bpy.data.libraries.load(str(ROOT/'ArtSource/CharacterCurrent/CharacterGameplay.blend'),link=False) as (src,dst):dst.objects=['SK_CharacterGameplay','root']
for o in dst.objects:bpy.context.scene.collection.objects.link(o)
bpy.context.view_layer.update()
body=next(o for o in dst.objects if o.type=='MESH');sums=collections.defaultdict(float);count=0;rows=[]
for v in body.data.vertices:
    p=body.matrix_world@v.co
    if p.z>.95 and abs(p.x)<.42 and abs(p.y)<.4:
        count+=1
        for w in v.groups:sums[body.vertex_groups[w.group].name]+=w.weight
        if len(rows)<12:rows.append({'position':list(p),'weights':[(body.vertex_groups[w.group].name,w.weight) for w in v.groups]})
r={'crown_vertices':count,'mean_weights':{k:v/count for k,v in sums.items()},'samples':rows}
(ROOT/'Saved/HeadCosmetics20261024/CrownWeights.json').write_text(json.dumps(r,indent=2));print('CROWN_WEIGHTS',json.dumps({k:r[k] for k in ['crown_vertices','mean_weights']}))
