"""Inspect joined facial shells without changing the file."""
import bpy,json
from pathlib import Path
from mathutils import Vector
obj=next(o for o in bpy.data.objects if o.type=='MESH')
weld={};ids=[];points=[]
for v in obj.data.vertices:
    key=tuple(round(c,3) for c in v.co)
    if key not in weld:weld[key]=len(weld);points.append(v.co.copy())
    ids.append(weld[key])
adj=[set() for _ in weld];edge_faces={}
for polygon in obj.data.polygons:
    verts=[ids[i] for i in polygon.vertices]
    for a,b in zip(verts,verts[1:]+verts[:1]):
        adj[a].add(b);adj[b].add(a);edge=tuple(sorted((a,b)));edge_faces[edge]=edge_faces.get(edge,0)+1
components=[];left=set(range(len(adj)))
while left:
    first=min(left);left.remove(first);group={first};queue=[first]
    while queue:
        for neighbor in adj[queue.pop()]:
            if neighbor in left:left.remove(neighbor);group.add(neighbor);queue.append(neighbor)
    indices=[v.index for v in obj.data.vertices if ids[v.index] in group]
    bones={}
    for i in indices:
        for g in obj.data.vertices[i].groups:
            name=obj.vertex_groups[g.group].name;bones[name]=bones.get(name,0)+g.weight
    boundary=[e for e,count in edge_faces.items() if count==1 and e[0] in group]
    components.append(dict(id=len(components),welded=len(group),vertices=len(indices),indices=indices,
        minimum=[min(points[i][axis] for i in group) for axis in range(3)],maximum=[max(points[i][axis] for i in group) for axis in range(3)],
        weights=sorted(bones.items(),key=lambda p:-p[1])[:12],boundary=[[list(points[i]) for i in e] for e in boundary]))
result=dict(source=bpy.data.filepath,vertices=[list(v.co) for v in obj.data.vertices],components=components,
    materials=[m.name for m in obj.data.materials])
path=Path(__file__).resolve().parents[2]/'Saved/MouthTopology.json';path.write_text(json.dumps(result),encoding='utf8')
for c in components:print('SHELL',c['id'],c['welded'],c['vertices'],c['minimum'],c['maximum'],'BOUNDARY',len(c['boundary']),'WEIGHTS',c['weights'][:5])
print('MOUTH_AUDIT_PASS',path)
