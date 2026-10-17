"""Read-only connected-island and original weight audit; no blend save."""
import bpy
import json
import math
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Saved/Codex/WizardWeightIslandInspection.json'
mesh = bpy.data.objects['SM_NutWizard']
rig = bpy.data.objects['rig']
data = mesh.data
groups = {g.index: g.name for g in mesh.vertex_groups}
deforms = {b.name for b in rig.data.bones if b.use_deform}
coords = [mesh.matrix_world @ v.co for v in data.vertices]
weights = [{groups[g.group]: g.weight for g in v.groups if g.weight > 0} for v in data.vertices]
parents = list(range(len(data.vertices)))
def find(i):
    while parents[i] != i:
        parents[i] = parents[parents[i]]
        i = parents[i]
    return i
for edge in data.edges:
    a, b = map(find, edge.vertices)
    if a != b:
        parents[b] = a
components = defaultdict(list)
for v in data.vertices:
    components[find(v.index)].append(v.index)
island_ids = {}
islands = []
for idx, verts in enumerate(sorted(components.values(), key=len, reverse=True)):
    for vi in verts:
        island_ids[vi] = idx
    positions = [coords[vi] for vi in verts]
    center = sum(positions, positions[0] * 0) / len(positions)
    mins = [min(v[i] for v in positions) for i in range(3)]
    maxs = [max(v[i] for v in positions) for i in range(3)]
    weight_sums = defaultdict(float)
    weight_counts = Counter()
    for vi in verts:
        for n, w in weights[vi].items():
            weight_sums[n] += w
            weight_counts[n] += 1
    islands.append(dict(id=idx, vertices=len(verts), vertex_indices=verts,
        world_min=mins, world_max=maxs, centroid=list(center),
        weights={n: dict(sum=s, vertices=weight_counts[n], average=s/len(verts))
                 for n, s in sorted(weight_sums.items(), key=lambda kv: kv[1], reverse=True)},
        both_root_head=sum('root.x' in weights[v] and 'head.x' in weights[v] for v in verts),
        materials={}, polygons=0))
for polygon in data.polygons:
    island = islands[island_ids[polygon.vertices[0]]]
    mi = polygon.material_index
    material = mesh.material_slots[mi].name if mi < len(mesh.material_slots) else str(mi)
    island['materials'][material] = island['materials'].get(material, 0) + 1
    island['polygons'] += 1

z_bands = []
both = [i for i, w in enumerate(weights) if 'root.x' in w and 'head.x' in w]
z0, z1 = min(coords[i].z for i in both), max(coords[i].z for i in both)
for band in range(12):
    lo, hi = z0 + (z1-z0)*band/12, z0 + (z1-z0)*(band+1)/12
    ids = [i for i in both if lo <= coords[i].z < hi or band==11 and coords[i].z==hi]
    sums = defaultdict(float)
    for i in ids:
        for n, w in weights[i].items():
            sums[n] += w
    z_bands.append(dict(z_min=lo, z_max=hi, vertices=len(ids),
        weight_average={n:s/len(ids) for n,s in sums.items()} if ids else {}))

bone_refs = {}
for n in ('c_root_master.x','c_root.x','root.x','c_thigh_b.l','c_thigh_b.r',
          'c_neck.x','neck.x','c_head.x','head.x','c_shoulder.l','c_shoulder.r'):
    b = rig.data.bones[n]
    bone_refs[n] = dict(head=list(rig.matrix_world @ b.head_local),
                        tail=list(rig.matrix_world @ b.tail_local),
                        parent=b.parent.name if b.parent else None)

result = dict(file=bpy.data.filepath, mesh=mesh.name, vertices=len(data.vertices),
    polygons=len(data.polygons), connected_island_count=len(islands),
    materials=[s.name for s in mesh.material_slots], islands=islands,
    root_head_overlap=len(both), root_head_z_bands=z_bands, bone_refs=bone_refs,
    scene_objects=[dict(name=o.name,type=o.type,parent=o.parent.name if o.parent else None,
                        parent_type=o.parent_type,parent_bone=o.parent_bone)
                   for o in bpy.data.objects if not o.name.startswith(('cs_', 'WGT', 'cs.'))])
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(json.dumps(result,indent=2),encoding='utf8')
print('WIZARD_WEIGHT_ISLAND_INSPECTION '+str(OUT),flush=True)
for island in islands:
    print(json.dumps({k:v for k,v in island.items() if k!='vertex_indices'}),flush=True)
