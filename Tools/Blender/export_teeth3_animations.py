"""Bake the animator's control rig onto the existing gameplay skeleton. Source stays untouched.
blender --background --factory-startup "Teeth2 (3).blend" --python this.py
"""
import bpy, json, math
from pathlib import Path
from mathutils import Matrix, Vector

if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/CharacterAnimation/Teeth3'
OUT.mkdir(parents=True, exist_ok=True)
source = bpy.data.objects['rig']
source.animation_data.use_nla = False
# Sparse actions do not key every rig option. Preserve the source file's neutral
# pole parents, IK/FK switches and stretch settings rather than inheriting the
# last frame of the previously exported action (which can flip a knee 180 degrees).
initial_properties = {pb.name: {key: pb[key] for key in pb.keys()
    if isinstance(pb[key], (int, float, bool))} for pb in source.pose.bones}
CLIPS = ('dance1','dance2','emo_happy','emo_sad','emo_shock','grab_left','grab_right','hello1','hello2','highfive1','highfive2','push','tired')
for a in CLIPS:
    action = bpy.data.actions[a]
    print('SLOTS', a, [(s.identifier, sum(len(st.channelbag(s).fcurves) if st.channelbag(s) else 0 for l in action.layers for st in l.strips)) for s in action.slots])

with bpy.data.libraries.load(str(ROOT / 'ArtSource/CharacterGameplay/TeethGameplay.blend'), link=False) as (available, loaded):
    loaded.objects = available.objects
loaded_objects = [o for o in loaded.objects if o]
target = next(o for o in loaded_objects if o.type == 'ARMATURE')
for o in loaded_objects:
    bpy.context.scene.collection.objects.link(o)
bpy.context.view_layer.update()
target.animation_data_clear()
target_world = target.matrix_world.copy()
target.parent = None
target.matrix_world = target_world
# The old export has a few connected helper bones. Their locked translations
# prevent an independent baked transform (notably the left calf twist) and
# create asymmetric kinks. Keep the rest matrices and hierarchy, unlock offsets.
bpy.ops.object.select_all(action='DESELECT')
target.hide_set(False)
target.select_set(True)
bpy.context.view_layer.objects.active = target
bpy.ops.object.mode_set(mode='EDIT')
for bone in target.data.edit_bones:
    bone.use_connect = False
bpy.ops.object.mode_set(mode='OBJECT')
for pb in target.pose.bones:
    pb.rotation_mode = 'QUATERNION'
    pb.matrix_basis = Matrix.Identity(4)
# Only suffixes change; underscores inside controller names are significant.
mapping = {b.name: b.name[:-2] + '.' + b.name[-1] if b.name.endswith(('_l', '_r', '_x')) else b.name for b in target.data.bones}
for name, src in list(mapping.items()):
    if src not in source.data.bones and 'c_' + src in source.data.bones:
        mapping[name] = 'c_' + src
missing = [n for n, s in mapping.items() if s not in source.data.bones]
if missing:
    raise RuntimeError('Missing source bones: ' + str(missing))
fps = bpy.context.scene.render.fps / bpy.context.scene.render.fps_base
report = {'fps': fps, 'source': bpy.data.filepath, 'target_bones': len(mapping), 'clips': []}
for name in CLIPS:
    source.animation_data.action = None
    for pb in source.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
        for key, value in initial_properties[pb.name].items():
            pb[key] = value
    source.update_tag()
    action = bpy.data.actions[name]
    source.animation_data.action = action
    counts = [(sum(len(st.channelbag(s).fcurves) if st.channelbag(s) else 0 for l in action.layers for st in l.strips), s) for s in action.slots]
    source.animation_data.action_slot = max(counts, key=lambda p: p[0])[1]
    target.animation_data_create()
    baked = bpy.data.actions.new('A_Teeth_' + name.title())
    target.animation_data.action = baked
    start, end = map(int, action.frame_range)
    bpy.context.scene.frame_set(start)
    bpy.context.view_layer.update()
    root_offset = (source.evaluated_get(bpy.context.evaluated_depsgraph_get()).pose.bones['root.x'].matrix.translation
                   - source.data.bones['root.x'].matrix_local.translation).copy()
    bpy.context.scene.frame_start, bpy.context.scene.frame_end = start, end
    maximum_motion = 0
    maximum_endpoint_error = 0
    previous_rotations = {}
    for frame in range(start, end + 1):
        bpy.context.scene.frame_set(frame)
        bpy.context.view_layer.update()
        evaluated = source.evaluated_get(bpy.context.evaluated_depsgraph_get())
        desired = {}
        for bone in target.data.bones:
            src_name = mapping[bone.name]
            rest_s = source.data.bones[src_name].matrix_local
            pose_s = evaluated.pose.bones[src_name].matrix
            rest_t = bone.matrix_local
            # Preserve the exported rest orientation and centimetre scale.
            src_pose = pose_s.copy(); src_pose.translation *= 100.0
            src_rest = rest_s.copy(); src_rest.translation *= 100.0
            desired[bone.name] = src_pose @ src_rest.inverted() @ rest_t
            desired[bone.name].translation -= root_offset * 100.0
            maximum_motion = max(maximum_motion, (desired[bone.name].translation-rest_t.translation).length)
        actual = {}
        for bone in target.data.bones:
            pb = target.pose.bones[bone.name]
            # Use the decomposed parent actually stored by Blender/FBX. A desired
            # parent can contain shear that cannot fit in a game's TRS transform;
            # using it directly accumulates centimetres of wrist/ankle drift.
            parent_matrix = actual[bone.parent.name] if bone.parent else Matrix.Identity(4)
            parent_rest = bone.parent.matrix_local if bone.parent else Matrix.Identity(4)
            pb.matrix_basis = bone.convert_local_to_pose(desired[bone.name], bone.matrix_local,
                parent_matrix=parent_matrix, parent_matrix_local=parent_rest, invert=True)
            if bone.name in previous_rotations and pb.rotation_quaternion.dot(previous_rotations[bone.name]) < 0:
                pb.rotation_quaternion.negate()
            previous_rotations[bone.name] = pb.rotation_quaternion.copy()
            actual[bone.name] = bone.convert_local_to_pose(pb.matrix_basis, bone.matrix_local,
                parent_matrix=parent_matrix, parent_matrix_local=parent_rest)
            maximum_endpoint_error = max(maximum_endpoint_error,
                (actual[bone.name].translation-desired[bone.name].translation).length)
            for prop in ('location', 'rotation_quaternion', 'scale'):
                pb.keyframe_insert(prop, frame=frame, group=bone.name)
    bpy.ops.object.select_all(action='DESELECT')
    target.hide_set(False)
    target.select_set(True)
    bpy.context.view_layer.objects.active = target
    path = OUT / ('A_Teeth_' + name.title() + '.fbx')
    bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={'ARMATURE'},
        add_leaf_bones=False, use_armature_deform_only=False, bake_anim=True,
        bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False, bake_anim_simplify_factor=0)
    baked.use_fake_user = True
    if maximum_endpoint_error > .01:
        raise RuntimeError(f'{name}: baked bone endpoint error {maximum_endpoint_error:.4f} cm')
    report['clips'].append({'name': name, 'frames': [start,end], 'max_motion_cm': maximum_motion,
        'max_endpoint_error_cm': maximum_endpoint_error, 'file': str(path)})
    print('BAKED', name, maximum_motion, flush=True)

# A clean, editable gameplay preview file with all baked actions.
for obj in list(bpy.data.objects):
    if obj not in loaded_objects:
        bpy.data.objects.remove(obj, do_unlink=True)
for action in list(bpy.data.actions):
    if not action.name.startswith('A_Teeth_'):
        bpy.data.actions.remove(action)
for obj in loaded_objects:
    obj.hide_set(False)
    obj.hide_render = False
    if obj.type == 'MESH' and bpy.data.materials.get('Material'):
        for slot in obj.material_slots:
            slot.material = bpy.data.materials['Material']
bpy.context.scene.frame_set(20)
bpy.context.scene['README'] = '13 artist clips baked from Teeth2 (3).blend. Gameplay skeleton preserved. Grab and push clips are layered below procedural contact IK in Unreal. mixamo.com belongs to a separate reference skeleton and is not a tooth action.'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'Teeth3GameplayAnimations.blend'))
(OUT / 'BakeReport.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf8')
print('MC_TEETH3_BAKE_PASS', flush=True)
