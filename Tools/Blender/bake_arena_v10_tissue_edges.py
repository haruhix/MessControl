"""Calculate surface distance from welded open borders; no Blender file edits."""
import json,heapq,math
from pathlib import Path

OUT=Path('D:/PROJECT/GAME/WaifuTD/MessControl/Artifacts/ArenaEdges_20261017')
data=json.loads((OUT/'Before.json').read_text())
result=[]
for row in data['rows']:
 weld={};unique=[];indices=[]
 for p in row['vertices']:
  key=tuple(round(v,2) for v in p)
  if key not in weld:weld[key]=len(unique);unique.append(p)
  indices.append(weld[key])
 edges={};neighbors=[{} for _ in unique]
 tris=row['triangles']
 for k in range(0,len(tris),3):
  tri=[indices[i] for i in tris[k:k+3]]
  for a,b in zip(tri,tri[1:]+tri[:1]):
   if a==b:continue
   key=tuple(sorted([a,b]));edges[key]=edges.get(key,0)+1
   length=math.dist(unique[a],unique[b]);neighbors[a][b]=length;neighbors[b][a]=length
 border=[pair for pair,count in edges.items() if count==1]
 assert border,('No open border',row['label'])
 boundary={v for pair in border for v in pair}
 distance=[float('inf')]*len(unique);queue=[]
 for i in boundary:distance[i]=0;heapq.heappush(queue,(0,i))
 while queue:
  d,i=heapq.heappop(queue)
  if d>distance[i]:continue
  for j,length in neighbors[i].items():
   candidate=d+length
   if candidate<distance[j]:distance[j]=candidate;heapq.heappush(queue,(candidate,j))
 # Every disconnected surface island must have a supported border.
 assert all(math.isfinite(d) for d in distance),(row['label'],'closed disconnected island')
 samples=[dict(local=p,distance_cm=min(200.,distance[i])) for p,i in zip(row['vertices'],indices)]
 result.append(dict(actor=row['actor'],mesh=row['mesh'],label=row['label'],samples=samples,
                    boundary_edges=len(border),boundary_vertices=len(boundary),max_distance_cm=max(distance)))
(OUT/'EdgeBake.json').write_text(json.dumps(result),encoding='utf-8')
(OUT/'EdgeBakeSummary.json').write_text(json.dumps([{k:v for k,v in r.items() if k!='samples'} for r in result],indent=2),encoding='utf-8')
print('ARENA_TISSUE_EDGES_BAKED',[(r['label'],r['boundary_edges'],round(r['max_distance_cm'],1)) for r in result])
