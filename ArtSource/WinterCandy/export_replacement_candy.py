"""Retain the new FBX geometry/UVs/morphs and attach the existing one-bone rig."""
import bpy,json,hashlib
from pathlib import Path
from mathutils import Matrix,Vector
source=Path('D:/PROJECT/GAME/TEETH CONTROL/teeth/New Folder/SM_Candy.fbx')
root=Path('D:/PROJECT/GAME/WaifuTD/MessControl')
out=root/'ArtSource/WinterCandy/Replacement';out.mkdir(parents=True,exist_ok=True)
digest=hashlib.sha256(source.read_bytes()).hexdigest()
bpy.ops.import_scene.fbx(filepath=str(source),use_anim=False,use_image_search=False)
obj=bpy.data.objects['SM_CandyMan'];obj.animation_data_clear()
keys=obj.data.shape_keys
if keys:
    keys.animation_data_clear()
    for key in keys.key_blocks:key.value=0
bpy.context.view_layer.update()
base=obj.matrix_world.copy()
points=[base@v.co for v in obj.data.vertices]
minimum=Vector([min(p[i] for p in points) for i in range(3)])
maximum=Vector([max(p[i] for p in points) for i in range(3)])
center=(minimum+maximum)*.5
data=obj.data.copy();data.transform(Matrix.Scale(100.,4)@Matrix.Translation(-center)@base,shape_keys=True)
data.materials.clear()
game=bpy.data.objects.new('CandyGame',data);bpy.context.scene.collection.objects.link(game)
rig=bpy.data.objects.new('Armature',bpy.data.armatures.new('CandyGameRig'));bpy.context.scene.collection.objects.link(rig)
for old in bpy.context.scene.objects:old.select_set(False)
rig.select_set(True);bpy.context.view_layer.objects.active=rig
bpy.ops.object.mode_set(mode='EDIT')
bone=rig.data.edit_bones.new('root');bone.head=(base.translation-center)*100.;bone.tail=bone.head+Vector((0,0,50.))
bpy.ops.object.mode_set(mode='OBJECT')
group=game.vertex_groups.new(name='root');group.add(list(range(len(data.vertices))),1.,'REPLACE')
modifier=game.modifiers.new('GameRig','ARMATURE');modifier.object=rig;game.parent=rig
game.select_set(True)
bpy.context.scene.unit_settings.system='METRIC';bpy.context.scene.unit_settings.scale_length=.01
options=dict(use_selection=True,axis_forward='-Y',axis_up='Z',apply_scale_options='FBX_SCALE_ALL',
 apply_unit_scale=True,add_leaf_bones=False,use_armature_deform_only=True,use_mesh_modifiers=False,
 mesh_smooth_type='FACE',colors_type='SRGB',bake_anim=False)
bpy.ops.export_scene.fbx(filepath=str(out/'SK_WinterCandy.fbx'),object_types={'ARMATURE','MESH'},**options)
bpy.ops.export_scene.fbx(filepath=str(out/'SM_WinterCandy.fbx'),object_types={'MESH'},**options)
assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
report={'source':str(source),'sha256':digest,'source_preserved':True,'vertices':len(data.vertices),
 'dimensions_cm':list((maximum-minimum)*100),'center_cm':list(center*100),
 'root_head_cm':list(rig.data.bones['root'].head_local),
 'uv_layers':[layer.name for layer in data.uv_layers],
 'morphs':[key.name for key in data.shape_keys.key_blocks] if data.shape_keys else []}
(out/'ExportReport.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('NEW_CANDY_EXPORTED',json.dumps(report))
