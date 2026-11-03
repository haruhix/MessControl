import bpy,json
from pathlib import Path
from mathutils import Matrix

out=Path('C:/Users/vasil/Documents/ChatGPT/MessControl/ToolsPositionUpdate');out.mkdir(exist_ok=True)
scene=bpy.context.scene
if bpy.context.object and bpy.context.object.mode!='OBJECT':bpy.ops.object.mode_set(mode='OBJECT')
scene.frame_set(43);bpy.context.view_layer.update()
refs=[('Knife','Cube','rig'),('Pickaxe','SM_Pick','rig.001'),('Spray','SM_Spray','rig.002')]
snapshot=[]
for name,tool,rig_name in refs:
    obj=bpy.data.objects[tool];rig=bpy.data.objects[rig_name];ev=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
    hand=ev.pose.bones['hand.r']
    snapshot.append({'name':name,'tool':tool,'rig':rig_name,'tool_world':[list(r) for r in obj.matrix_world], 'tool_scale':list(obj.scale),'rig_origin':list(rig.matrix_world.translation),'hand_world':[list(r) for r in (rig.matrix_world@hand.matrix)],'hand_rest':[list(r) for r in rig.data.bones['hand.r'].matrix_local],'mesh_bounds':[[min(v.co[i] for v in obj.data.vertices),max(v.co[i] for v in obj.data.vertices)] for i in range(3)]})
with bpy.data.libraries.load('C:/Users/vasil/Documents/ChatGPT/MessControl/CharacterUpdateV2/ArtSource/CharacterGameplay/TeethGameplay.blend',link=False) as (avail,loaded):loaded.objects=avail.objects
target=next(o for o in loaded.objects if o and o.type=='ARMATURE')
scene.collection.objects.link(target);bpy.context.view_layer.update()
world=target.matrix_world.copy();target.animation_data_clear();target.parent=None;target.matrix_world=world
target.data.pose_position='POSE'
for b in target.pose.bones:b.rotation_mode='QUATERNION';b.matrix_basis=Matrix.Identity(4)
report=[]
for name,tool,rig_name in refs:
    scene.frame_set(43);bpy.context.view_layer.update()
    source=bpy.data.objects[rig_name];evaluated=source.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mapping={b.name:b.name[:-2]+'.'+b.name[-1] if b.name.endswith(('_l','_r','_x')) else b.name for b in target.data.bones}
    for n,s in list(mapping.items()):
        if s not in source.data.bones and 'c_'+s in source.data.bones:mapping[n]='c_'+s
    missing=[(n,s) for n,s in mapping.items() if s not in source.data.bones]
    assert not missing,missing
    root_offset=(evaluated.pose.bones['root.x'].matrix.translation-source.data.bones['root.x'].matrix_local.translation).copy()
    desired={}
    for bone in target.data.bones:
        src=mapping[bone.name];p=evaluated.pose.bones[src].matrix.copy();rest=source.data.bones[src].matrix_local.copy();p.translation*=100;rest.translation*=100
        d=p@rest.inverted()@bone.matrix_local;d.translation-=root_offset*100;desired[bone.name]=d
    target.animation_data_create();action=bpy.data.actions.new('A_ToolGrip_'+name);target.animation_data.action=action
    actual={}
    for bone in target.data.bones:
        p=target.pose.bones[bone.name]; parent=actual[bone.parent.name] if bone.parent else Matrix.Identity(4);pr=bone.parent.matrix_local if bone.parent else Matrix.Identity(4)
        p.matrix_basis=bone.convert_local_to_pose(desired[bone.name],bone.matrix_local,parent_matrix=parent,parent_matrix_local=pr,invert=True)
        actual[bone.name]=bone.convert_local_to_pose(p.matrix_basis,bone.matrix_local,parent_matrix=parent,parent_matrix_local=pr)
        for frame in (1,2):
            for prop in ('location','rotation_quaternion','scale'):p.keyframe_insert(prop,frame=frame,group=bone.name)
    bpy.ops.object.select_all(action='DESELECT');target.hide_set(False);target.select_set(True);bpy.context.view_layer.objects.active=target
    print('POSE_BEFORE_EXPORT',name,list(target.pose.bones['index1_r'].matrix_basis.to_quaternion()),list(target.pose.bones['thumb1_r'].matrix_basis.to_quaternion()))
    for pb in target.pose.bones:
        pb.matrix_basis=Matrix.Identity(4)
        for prop in ('location','rotation_quaternion','scale'):pb.keyframe_insert(prop,frame=0,group=pb.name)
    scene.frame_start=0;scene.frame_end=2
    scene.frame_set(0);bpy.context.view_layer.update()
    print('POSE_AT_FRAME',name,list(target.pose.bones['index1_r'].matrix_basis.to_quaternion()), 'evaluated',list(target.evaluated_get(bpy.context.evaluated_depsgraph_get()).pose.bones['index1_r'].matrix_basis.to_quaternion()), 'action',target.animation_data.action.name,'slots',[(s.identifier) for s in target.animation_data.action.slots])
    bpy.ops.export_scene.fbx(filepath=str(out/('A_ToolGrip_'+name+'.fbx')),use_selection=True,object_types={'ARMATURE'},add_leaf_bones=False,use_armature_deform_only=False,bake_anim=True,bake_anim_use_nla_strips=False,bake_anim_use_all_actions=False,bake_anim_simplify_factor=0)
    report.append({'name':name,'file':str(out/('A_ToolGrip_'+name+'.fbx'))})
    target.animation_data.action=None
(out/'reference.json').write_text(json.dumps(snapshot,indent=2),encoding='utf8')
(out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('TOOL_REFERENCE_EXPORTED',json.dumps(report))
