"""Separate the artist's tongue material section without editing the source asset.
One SIMPLE subdivision supplies enough vertices for a broad travelling wave.
Both exports retain the original origin, UVs and coordinate system.
"""
import bpy, bmesh, json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
out=root/'ArtSource/ArenaGameplay'; out.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system='METRIC'; bpy.context.scene.unit_settings.scale_length=.01
bpy.ops.import_scene.fbx(filepath=str(root/'Saved/ArenaIntegration/SM_Location.fbx'))
source=next(o for o in bpy.context.selected_objects if o.type=='MESH')
if len(source.material_slots)!=2: raise RuntimeError('Inspect new art: expected mouth + tongue material sections')
report=[]
for section,name in ((0,'SM_MouthShell'),(1,'SM_TongueSurface')):
    mesh=source.data.copy(); bm=bmesh.new(); bm.from_mesh(mesh)
    bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.material_index!=section],context='FACES')
    bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context='VERTS')
    for f in bm.faces: f.material_index=0
    bm.to_mesh(mesh); bm.free(); mesh.materials.clear(); mesh.materials.append(source.material_slots[section].material)
    obj=bpy.data.objects.new(name,mesh); bpy.context.collection.objects.link(obj)
    obj.matrix_world=source.matrix_world.copy()
    bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active=obj
    if section==1:
        modifier=obj.modifiers.new('Wave resolution (shape preserved)','SUBSURF'); modifier.subdivision_type='SIMPLE'; modifier.levels=1
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    bpy.ops.export_scene.fbx(filepath=str(out/(name+'.fbx')),use_selection=True,object_types={'MESH'},
        apply_unit_scale=True,axis_forward='-Y',axis_up='Z',bake_anim=False,add_leaf_bones=False,mesh_smooth_type='FACE')
    report.append(dict(mesh=name,vertices=len(obj.data.vertices),faces=len(obj.data.polygons)))
bpy.data.objects.remove(source,do_unlink=True)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(out/'Tongue.blend'))
(out/'tongue.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('MC_TONGUE_PREPARED',report)
