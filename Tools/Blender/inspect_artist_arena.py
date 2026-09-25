"""Read exported artist geometry; report disconnected tooth islands and arena floor heights."""
import bpy, json, math
from pathlib import Path
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
root=Path(__file__).resolve().parents[2]; out=root/'Saved/ArenaIntegration'
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system='METRIC'; bpy.context.scene.unit_settings.scale_length=.01
report={}
for name in ('SM_Location_teeth','SM_Location'):
    bpy.ops.object.select_all(action='DESELECT')
    bpy.ops.import_scene.fbx(filepath=str(out/(name+'.fbx')))
    objs=[o for o in bpy.context.selected_objects if o.type=='MESH']
    report[name]=[]
    for obj in objs:
        verts=[obj.matrix_world@v.co for v in obj.data.vertices]
        parent=list(range(len(verts)))
        def find(i):
            while parent[i]!=i: parent[i]=parent[parent[i]]; i=parent[i]
            return i
        for e in obj.data.edges:
            a,b=map(find,e.vertices); parent[a]=b
        groups={}
        for i in range(len(verts)): groups.setdefault(find(i),[]).append(i)
        bounds=lambda vv: dict(min=[min(v[a] for v in vv) for a in range(3)],max=[max(v[a] for v in vv) for a in range(3)])
        d=dict(name=obj.name,vertices=len(verts),faces=len(obj.data.polygons),bounds=bounds(verts),islands=[dict(vertices=len(ids),**bounds([verts[i] for i in ids])) for ids in groups.values()])
        report[name].append(d)
        if name=='SM_Location':
            # UE FBX -> Blender uses opposite Y handedness; verify bounds against ArenaAudit.json.
            audit=json.loads((root/'Saved/ArenaAudit.json').read_text(encoding='utf-8'))
            a=next(x for x in audit['actors'] if x['label']=='SM_Location2')
            yaw=math.radians(84.113584); loc=Vector(a['location'])
            rot=Matrix.Rotation(yaw,4,'Z')
            world=[rot@Vector((v.x,-v.y,v.z))+loc for v in verts]
            tree=BVHTree.FromPolygons(world,[tuple(p.vertices) for p in obj.data.polygons],all_triangles=False)
            samples=[]
            for x in (-1100,-900,-700,-500,-300,0,300,600,900,1200,1500,1800):
                for y in (-650,-350,0,350,650):
                    pos,normal,index,dist=tree.ray_cast(Vector((x,y,350)),Vector((0,0,-1)),1300)
                    samples.append(dict(x=x,y=y,z=round(pos.z,2) if pos else None,normal=[round(n,3) for n in normal] if normal else None))
            d['floor_probes']=samples
(out/'geometry.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('MC_ARTIST_GEOMETRY_REPORT',str(out/'geometry.json'))
