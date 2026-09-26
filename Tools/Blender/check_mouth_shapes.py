"""Inspect mouth component intersections at each key and blended transitions."""
import bpy, json, itertools
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[2]
obj=bpy.data.objects['SK_TeethFace'];mesh=obj.data;keys=mesh.shape_keys.key_blocks
mesh.calc_loop_triangles()
polys={m:[tuple(t.vertices) for t in mesh.loop_triangles if mesh.polygons[t.polygon_index].material_index==m] for m in range(3)}
base=[v.co.copy() for v in keys[0].data]
report=[];count=0
poses={key.name:[v.co.copy() for v in key.data] for key in keys if key.name=='Basis' or key.name.startswith('Mouth_')}
def crosses(a,b,verts):
    # BVH overlap includes contact on a shared straight seam, even when an ngon
    # tessellator omits its collinear intermediate vertices. Test penetration
    # on both triangle planes with 0.001 cm (0.01 mm) tolerance.
    pa=[verts[i] for i in a];pb=[verts[i] for i in b]
    na=(pa[1]-pa[0]).cross(pa[2]-pa[0]).normalized()
    nb=(pb[1]-pb[0]).cross(pb[2]-pb[0]).normalized()
    da=[(p-pb[0]).dot(nb) for p in pa];db=[(p-pa[0]).dot(na) for p in pb]
    return min(da)<-.001 and max(da)>.001 and min(db)<-.001 and max(db)>.001
def cases():
    yield from poses.items()
    for a,b in itertools.combinations(poses,2):
        for step in range(1,10):
            t=step/10
            yield f'{a}/{b}/{t}',[v.lerp(w,t) for v,w in zip(poses[a],poses[b])]
for name,verts in cases():
    count+=1
    trees={m:BVHTree.FromPolygons(verts,polys[m],all_triangles=True) for m in range(3)}
    counts={}
    for a,b in [(0,1),(0,2),(1,2)]:
        counts[f'{a}-{b}']=sum(not set(polys[a][i]).intersection(polys[b][j]) and crosses(polys[a][i],polys[b][j],verts) for i,j in trees[a].overlap(trees[b]))
    for a in [1,2]:
        counts[f'{a}-self']=sum(i<j and not set(polys[a][i]).intersection(polys[a][j]) and crosses(polys[a][i],polys[a][j],verts) for i,j in trees[a].overlap(trees[a]))
    if any(counts.values()):report.append(dict(shape=name,overlaps=counts));print(name,counts)
(ROOT/'Saved/MouthIntersections.json').write_text(json.dumps(report,indent=2))
assert not report, 'Mouth intersections found; see Saved/MouthIntersections.json'
print('MC_MOUTH_INTERSECTIONS_PASS',count,'poses and transitions')
