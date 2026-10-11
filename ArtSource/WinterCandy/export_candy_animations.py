"""Bake the saved artist's object actions and paired morph into a one-bone game rig."""
import bpy,json,hashlib,math
from pathlib import Path
from collections import Counter
from mathutils import Matrix,Vector
root=Path('D:/PROJECT/GAME/WaifuTD/MessControl/ArtSource/WinterCandy/Animations')
root.mkdir(parents=True,exist_ok=True)
source_path=Path(bpy.data.filepath);digest=hashlib.sha256(source_path.read_bytes()).hexdigest()
scene=bpy.context.scene;obj=bpy.data.objects['SM_CandyMan'];keys=obj.data.shape_keys
obj.animation_data_clear();keys.animation_data_clear()
for key in keys.key_blocks:key.value=0
fps=scene.render.fps/scene.render.fps_base
clips=[('idle','Idle'),('Atack_MiniCandy','MiniCandy'),('Floor_Hit','FloorHit'),('Scream_Atack','Scream')]
samples={}
for action_name,role in clips:
    action=bpy.data.actions[action_name]
    obj.animation_data_create();obj.animation_data.action=action
    obj.animation_data.action_slot=next(s for s in action.slots if s.target_id_type=='OBJECT')
    keys.animation_data_clear()
    for key in keys.key_blocks:key.value=0
    slot=next((s for s in action.slots if s.target_id_type=='KEY'),None)
    if slot:
        keys.animation_data_create();keys.animation_data.action=action;keys.animation_data.action_slot=slot
    first,last=map(int,action.frame_range)
    records=[]
    for frame in range(first,last+1):
        scene.frame_set(frame);bpy.context.view_layer.update()
        morphs={k.name:float(k.value) for k in keys.key_blocks if k.name!='Basis'}
        if role=='FloorHit':morphs['floor_hit']=max(0.,1-abs(frame-18)/5.)
        records.append({'matrix':obj.matrix_world.copy(),'morphs':morphs})
    samples[role]={'records':records,'frames':last-first,'first':first,'action':action_name}
obj.animation_data_clear();keys.animation_data_clear()
for key in keys.key_blocks:key.value=0
base=samples['Idle']['records'][0]['matrix']
points=[base@v.co for v in keys.key_blocks[0].data]
minimum=Vector([min(v[i] for v in points) for i in range(3)])
maximum=Vector([max(v[i] for v in points) for i in range(3)])
center=(minimum+maximum)*.5
# Bake both geometry and bone motion in centimetres so FBX unit conversion
# cannot scale only the mesh while leaving root animation tracks in metres.
centimetres=Matrix.Scale(100.,4)
mesh=obj.data.copy();mesh.transform(centimetres@Matrix.Translation(-center)@base,shape_keys=True)
mesh.materials.clear()
for role in ('Body','Face'):mesh.materials.append(bpy.data.materials.new('WinterCandy'+role))
attribute=mesh.color_attributes[0]
main=Counter(tuple(v.color) for v in attribute.data).most_common(1)[0][0]
for polygon in mesh.polygons:
    polygon.material_index=0 if all(sum(abs(attribute.data[i].color[j]-main[j]) for j in range(3))<.02 for i in polygon.loop_indices) else 1
mesh.color_attributes.render_color_index=0
game=bpy.data.objects.new('CandyGame',mesh);scene.collection.objects.link(game)
game.data.shape_keys.animation_data_clear()
for key in game.data.shape_keys.key_blocks:key.value=0
rig=bpy.data.objects.new('Armature',bpy.data.armatures.new('CandyGameRig'));scene.collection.objects.link(rig)
for old in scene.objects:old.select_set(False)
rig.select_set(True);bpy.context.view_layer.objects.active=rig
bpy.ops.object.mode_set(mode='EDIT')
bone=rig.data.edit_bones.new('root');bone.head=(base.translation-center)*100.;bone.tail=bone.head+Vector((0,0,50.))
bpy.ops.object.mode_set(mode='OBJECT')
group=game.vertex_groups.new(name='root');group.add(list(range(len(mesh.vertices))),1.,'REPLACE')
modifier=game.modifiers.new('GameRig','ARMATURE');modifier.object=rig
game.parent=rig
game.select_set(True)
scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=.01
common=dict(use_selection=True,object_types={'ARMATURE','MESH'},axis_forward='-Y',axis_up='Z',
    apply_scale_options='FBX_SCALE_ALL',apply_unit_scale=True,add_leaf_bones=False,
    use_armature_deform_only=True,use_mesh_modifiers=False,mesh_smooth_type='FACE',colors_type='SRGB',
    bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,bake_anim_simplify_factor=0.,bake_anim_step=1.)
bpy.ops.export_scene.fbx(filepath=str(root/'SK_WinterCandy.fbx'),bake_anim=False,**common)
bone_rest=rig.data.bones['root'].matrix_local.copy()
report={'source':str(source_path),'source_sha256':digest,'fps':fps,
    'mesh':'SK_WinterCandy.fbx','dimensions_cm':list((maximum-minimum)*100),
    'morphs':[k.name for k in game.data.shape_keys.key_blocks if k.name!='Basis'],'clips':[]}
for role,spec in samples.items():
    rig.animation_data_clear();game.data.shape_keys.animation_data_clear()
    for key in game.data.shape_keys.key_blocks:key.value=0
    scene.frame_start=0;scene.frame_end=spec['frames']
    for frame,record in enumerate(spec['records']):
        scene.frame_set(frame)
        pose=rig.pose.bones['root'];pose.rotation_mode='QUATERNION'
        delta=centimetres@Matrix.Translation(-center)@record['matrix']@base.inverted()@Matrix.Translation(center)@centimetres.inverted()
        pose.matrix=delta@bone_rest
        for channel in ('location','rotation_quaternion','scale'):pose.keyframe_insert(channel,frame=frame)
        for name,value in record['morphs'].items():
            key=game.data.shape_keys.key_blocks[name];key.value=value;key.keyframe_insert('value',frame=frame)
    filename='A_WinterCandy_'+role+'.fbx'
    rig.animation_data.action.name='A_WinterCandy_'+role
    bpy.ops.export_scene.fbx(filepath=str(root/filename),bake_anim=True,**common)
    report['clips'].append({'role':role,'file':filename,'duration':spec['frames']/fps,
        'source_action':spec['action'],'scream_peak':max(r['morphs'].get('scream',0) for r in spec['records']),
        'morph_samples':{name:[float(r['morphs'][name]) for r in spec['records']] for name in report['morphs']}})
assert report['clips'][-1]['scream_peak']>.6
assert hashlib.sha256(source_path.read_bytes()).hexdigest()==digest
report['source_preserved']=True
(root/'ExportReport.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('CANDY_ANIMATION_EXPORT',json.dumps(report))
