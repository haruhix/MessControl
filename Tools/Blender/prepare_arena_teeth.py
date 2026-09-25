"""Split the five authored tooth islands, preserving geometry/UVs; also bake mirrored copies."""
import bpy, bmesh, json
from pathlib import Path
from mathutils import Vector
root=Path(__file__).resolve().parents[2]; source=root/'Saved/ArenaIntegration/SM_Location_teeth.fbx'
out=root/'ArtSource/ArenaGameplay'; out.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system='METRIC'; bpy.context.scene.unit_settings.scale_length=.01
bpy.ops.import_scene.fbx(filepath=str(source))
obj=next(o for o in bpy.context.selected_objects if o.type=='MESH')
coords=[obj.matrix_world@v.co for v in obj.data.vertices]; parent=list(range(len(coords)))
def find(i):
    while parent[i]!=i: parent[i]=parent[parent[i]]; i=parent[i]
    return i
for edge in obj.data.edges:
    a,b=map(find,edge.vertices); parent[a]=b
islands={}
for i in range(len(coords)): islands.setdefault(find(i),[]).append(i)
if len(islands)!=5: raise RuntimeError('Expected five distinct teeth; inspect the new artist mesh before regenerating')
groups=sorted(islands.values(),key=lambda ids:sum(coords[i].y for i in ids)/len(ids))
manifest=[]
for index,ids in enumerate(groups,1):
    keep=set(ids); centre=Vector([(min(coords[i][a] for i in ids)+max(coords[i][a] for i in ids))*.5 for a in range(3)])
    record={'index':index,'centre_ue':[centre.x,-centre.y,centre.z]}
    for mirrored in (False,True):
        name='SM_ArenaTooth_%02d'%index+('_Mirrored' if mirrored else '')
        mesh=obj.data.copy()
        for i,v in enumerate(mesh.vertices):
            p=coords[i]-centre
            if mirrored: p.x=-p.x
            v.co=p
        bm=bmesh.new(); bm.from_mesh(mesh); bm.verts.ensure_lookup_table()
        bmesh.ops.delete(bm,geom=[v for v in bm.verts if v.index not in keep],context='VERTS')
        if mirrored: bmesh.ops.reverse_faces(bm,faces=list(bm.faces))
        bm.to_mesh(mesh); bm.free(); mesh.update()
        tooth=bpy.data.objects.new(name,mesh); bpy.context.collection.objects.link(tooth)
        # Keep the editable workshop arranged as the original row; export a centred asset.
        bpy.ops.object.select_all(action='DESELECT'); tooth.select_set(True); bpy.context.view_layer.objects.active=tooth
        bpy.ops.export_scene.fbx(filepath=str(out/(name+'.fbx')),use_selection=True,object_types={'MESH'},
            apply_unit_scale=True,axis_forward='-Y',axis_up='Z',bake_anim=False,add_leaf_bones=False,mesh_smooth_type='FACE')
        tooth.location=centre+Vector((450 if mirrored else 0,0,0))
        record['mirror' if mirrored else 'mesh']=name
    manifest.append(record)
bpy.data.objects.remove(obj,do_unlink=True)
(out/'teeth.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(out/'ArenaTeeth.blend'))
print('MC_ARENA_TEETH_PREPARED',len(manifest),'artist shapes, UVs and mirrored geometry preserved')
