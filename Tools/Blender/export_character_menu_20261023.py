"""Bake the artist's CHARACTER_MENU.blend into a private menu mesh and loop.

Run in a separate background Blender with the source file open. The artist
file and interactive Blender sessions are never saved or modified.
"""
import bpy, json, math, hashlib
from pathlib import Path
from mathutils import Matrix, Vector
from mathutils.kdtree import KDTree

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/UI/CharacterMenu20261023'
OUT.mkdir(parents=True,exist_ok=True)
scene=bpy.context.scene
source=bpy.data.objects['rig']; brush_rig=bpy.data.objects['Armature']
body=[o for o in scene.objects if o.type=='MESH' and len(o.data.vertices) and any(m.type=='ARMATURE' and m.object==source for m in o.modifiers)]
brush=[o for o in scene.objects if o.type=='MESH' and len(o.data.vertices) and any(m.type=='ARMATURE' and m.object==brush_rig for m in o.modifiers)]
assert len(body)==5 and len(brush)==1
if bpy.context.object and bpy.context.object.mode!='OBJECT':bpy.ops.object.mode_set(mode='OBJECT')
scene.frame_set(1);bpy.context.view_layer.update()
deps=bpy.context.evaluated_depsgraph_get()
# Keep artist poses in world space, but put the runtime origin at the soles.
pts=[o.matrix_world@v.co for o in body for v in o.evaluated_get(deps).data.vertices]
anchor=Vector(((min(p.x for p in pts)+max(p.x for p in pts))*.5,(min(p.y for p in pts)+max(p.y for p in pts))*.5,min(p.z for p in pts)))
offset=Matrix.Translation(-anchor)
weighted={g.name for o in body for v in o.data.vertices for w in v.groups if w.weight>0 for g in [o.vertex_groups[w.group]]}
names={b.name for b in source.data.bones if b.use_deform}|{n for n in weighted if n in source.data.bones}
assert all(n in source.data.bones for n in weighted),weighted-names
entries=[(source,b,b.name) for b in source.data.bones if b.name in names]+[(brush_rig,b,'MenuBrush_'+b.name) for b in brush_rig.data.bones]
name_map={(r.name,b.name):n for r,b,n in entries}
data=bpy.data.armatures.new('MenuCharacter_ArtistRig')
target=bpy.data.objects.new('MenuCharacter',data);scene.collection.objects.link(target)
bpy.ops.object.select_all(action='DESELECT');target.select_set(True);bpy.context.view_layer.objects.active=target
bpy.ops.object.mode_set(mode='EDIT')
for rig,b,name in entries:
    eb=data.edit_bones.new(name)
    eb.matrix=offset@rig.matrix_world@b.matrix_local
    eb.length=max(b.length,.01)
    eb.use_deform=True
for rig,b,name in entries:
    p=b.parent
    while p and (rig.name,p.name) not in name_map:p=p.parent
    parent=name_map.get((rig.name,p.name)) if p else ('hand.l' if rig==brush_rig else None)
    if parent: data.edit_bones[name].parent=data.edit_bones[parent]
bpy.ops.object.mode_set(mode='OBJECT')
rest_corrections={name:(offset@rig.matrix_world@b.matrix_local).inverted()@data.bones[name].matrix_local for rig,b,name in entries}
clones=[]
for o in body+brush:
    clone=o.copy();clone.data=o.data.copy();clone.animation_data_clear();scene.collection.objects.link(clone)
    clone.parent=None;clone.matrix_world=Matrix.Identity(4)
    clone.data.transform(offset@o.matrix_world,shape_keys=True)
    for m in list(clone.modifiers):clone.modifiers.remove(m)
    mod=clone.modifiers.new('Menu baked deformation','ARMATURE');mod.object=target
    if o in brush:
        for g in clone.vertex_groups:
            if (brush_rig.name,g.name) in name_map:g.name=name_map[(brush_rig.name,g.name)]
    if clone.data.shape_keys:clone.data.shape_keys.animation_data_clear()
    clone.hide_set(False);clone.hide_viewport=False;clone.hide_render=False
    clones.append(clone)
bpy.ops.object.select_all(action='DESELECT')
for o in clones:o.select_set(True)
bpy.context.view_layer.objects.active=clones[0]
bpy.ops.object.join()
mesh=clones[0];mesh.name='SK_CharacterMenu';mesh.data.name='SK_CharacterMenu'
mesh.parent=target;mesh.matrix_parent_inverse=Matrix.Identity(4)
for p in target.pose.bones:p.rotation_mode='QUATERNION'
target.animation_data_create();action=bpy.data.actions.new('A_CharacterMenu_Cleaning_Loop');target.animation_data.action=action
for k in mesh.data.shape_keys.key_blocks:k.value=0
samples=[];shape_values=[];max_shape_mismatch=0
for frame in range(1,221):
    scene.frame_set(frame);bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get()
    samples.append({name:offset@rig.evaluated_get(deps).matrix_world@rig.evaluated_get(deps).pose.bones[b.name].matrix@rest_corrections[name] for rig,b,name in entries})
    values={k.name:k.value for k in body[0].data.shape_keys.key_blocks if k.name!='Basis'}
    for o in body:
        for k in o.data.shape_keys.key_blocks:
            if k.name in values:max_shape_mismatch=max(max_shape_mismatch,abs(k.value-values[k.name]))
    shape_values.append(values)
assert max_shape_mismatch<1e-5,max_shape_mismatch
def difference(a,b):
    return {'translation_cm':(a.translation-b.translation).length*100,'rotation_deg':math.degrees(a.to_quaternion().rotation_difference(b.to_quaternion()).angle)}
seam={n:difference(samples[0][n],samples[-1][n]) for n in samples[0]}
source_seam={key:max(v[key] for v in seam.values()) for key in ['translation_cm','rotation_deg']}
closing_frames=8
# Ease the tiny artist endpoint discrepancy out over the existing last frames.
# Retain the original 220-frame timing, with no hold appended to the cycle.
corrections={n:samples[-1][n].inverted()@samples[0][n] for n in samples[0]}
end_shapes=dict(shape_values[-1])
for step in range(1,closing_frames+1):
    t=step/closing_frames;t=t*t*(3-2*t);index=len(samples)-closing_frames+step-1
    for name in samples[0]:
        samples[index][name]=samples[index][name]@Matrix.Identity(4).lerp(corrections[name],t)
    shape_values[index]={n:shape_values[index][n]+(shape_values[0][n]-end_shapes[n])*t for n in shape_values[0]}
samples[-1]={n:m.copy() for n,m in samples[0].items()}
shape_values[-1]=dict(shape_values[0])
max_error=0;previous={}
for i,desired in enumerate(samples):
    frame=i+1;actual={}
    for bone in data.bones:
        pb=target.pose.bones[bone.name]
        pm=actual[bone.parent.name] if bone.parent else Matrix.Identity(4)
        pr=bone.parent.matrix_local if bone.parent else Matrix.Identity(4)
        pb.matrix_basis=bone.convert_local_to_pose(desired[bone.name],bone.matrix_local,parent_matrix=pm,parent_matrix_local=pr,invert=True)
        if bone.name in previous and pb.rotation_quaternion.dot(previous[bone.name])<0:pb.rotation_quaternion.negate()
        previous[bone.name]=pb.rotation_quaternion.copy()
        actual[bone.name]=bone.convert_local_to_pose(pb.matrix_basis,bone.matrix_local,parent_matrix=pm,parent_matrix_local=pr)
        max_error=max(max_error,(actual[bone.name].translation-desired[bone.name].translation).length*100)
        for prop in ['location','rotation_quaternion','scale']:pb.keyframe_insert(prop,frame=frame,group=bone.name)
    for name,value in shape_values[i].items():
        key=mesh.data.shape_keys.key_blocks[name];key.value=value;key.keyframe_insert('value',frame=frame)
for animated in [target.animation_data.action,mesh.data.shape_keys.animation_data.action]:
    for layer in animated.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for fc in bag.fcurves:
                    for k in fc.keyframe_points:k.interpolation='LINEAR'
scene.frame_start=1;scene.frame_end=len(samples);scene.frame_set(1);bpy.context.view_layer.update()
vertex_errors={}
subsurf=[m for o in body for m in o.modifiers if m.type=='SUBSURF' and m.show_viewport]
for m in subsurf:m.show_viewport=False
for frame in [1,51,101,151]:
    scene.frame_set(frame);bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get()
    evaluated=mesh.evaluated_get(deps)
    tree=KDTree(len(evaluated.data.vertices))
    for i,v in enumerate(evaluated.data.vertices):tree.insert(v.co,i)
    tree.balance()
    vertex_errors[str(frame)]=max(tree.find(offset@o.matrix_world@v.co)[2]*100 for o in body+brush for v in o.evaluated_get(deps).data.vertices)
for m in subsurf:m.show_viewport=True
assert max(vertex_errors.values())<.02,vertex_errors
scene.frame_set(1);bpy.context.view_layer.update()
bpy.ops.object.select_all(action='DESELECT');target.select_set(True);mesh.select_set(True);bpy.context.view_layer.objects.active=target
path=OUT/'SK_CharacterMenu.fbx'
result=bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH','ARMATURE'},add_leaf_bones=False,use_armature_deform_only=False,
    axis_forward='-Y',axis_up='Z',global_scale=1.,apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',bake_space_transform=False,
    use_mesh_modifiers=False,mesh_smooth_type='FACE',bake_anim=True,bake_anim_use_nla_strips=False,bake_anim_use_all_actions=False,
    bake_anim_use_all_bones=True,bake_anim_force_startend_keying=True,bake_anim_step=1.,bake_anim_simplify_factor=0.,path_mode='AUTO')
assert 'FINISHED' in result and path.stat().st_size>10000
report={'source':bpy.data.filepath,'source_sha256':hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest(),'fbx':str(path),'fbx_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
    'fps':24,'source_frames':[1,220],'frames':[1,len(samples)],'duration_seconds':(len(samples)-1)/24,'closing_frames':closing_frames,'source_seam':source_seam,
    'loop_seam':{key:max(difference(samples[0][n],samples[-1][n])[key] for n in samples[0]) for key in source_seam},'max_bone_bake_error_cm':max_error,
    'origin_offset_m':list(anchor),'vertices':len(mesh.data.vertices),'bones':len(data.bones),'materials':[m.name if m else None for m in mesh.data.materials], 'source_vertex_errors_cm':vertex_errors,
    'morphs':[k.name for k in mesh.data.shape_keys.key_blocks if k.name!='Basis'],'max_shape_mismatch':max_shape_mismatch,
    'landmarks':{str(i):{n:list(samples[i][n].translation) for n in ['root.x','head.x','hand.l','hand.r','foot.l','foot.r','MenuBrush_brush']} for i in [0,50,100,150,219,len(samples)-1]}}
assert max_error<.01,max_error
(OUT/'ExportReport.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('CHARACTER_MENU_EXPORTED',json.dumps(report),flush=True)
