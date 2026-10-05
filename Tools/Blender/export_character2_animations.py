"""Bake Character2.blend's new control-rig actions onto the saved game skeleton.

Run in a separate background Blender with the artist file open. The source file
and any interactive Blender session stay untouched; only derived FBX/report files
under ArtSource/CharacterAnimation/Character2 are written.
"""
import bpy
import hashlib
import json
from pathlib import Path
from mathutils import Matrix

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/CharacterAnimation/Character2'
OUT.mkdir(parents=True, exist_ok=True)
SOURCE_PATH = Path(bpy.data.filepath)
source = bpy.data.objects['rig']
source.animation_data.use_nla = False
initial_properties = {pb.name: {key: pb[key] for key in pb.keys()
    if isinstance(pb[key], (int, float, bool))} for pb in source.pose.bones}
CLIPS = ('laugh1', 'laugh2', 'point', 'dance3', 'dance4', 'dance5', 'happy_jump', 'punch')

if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
with bpy.data.libraries.load(str(ROOT / 'ArtSource/CharacterGameplay/TeethGameplay.blend'), link=False) as (available, loaded):
    loaded.objects = available.objects
target = next(o for o in loaded.objects if o and o.type == 'ARMATURE')
bpy.context.scene.collection.objects.link(target)
bpy.context.view_layer.update()
target_world = target.matrix_world.copy()
target.animation_data_clear()
target.parent = None
target.matrix_world = target_world
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
mapping = {b.name: b.name[:-2] + '.' + b.name[-1] if b.name.endswith(('_l', '_r', '_x')) else b.name for b in target.data.bones}
for name, src in list(mapping.items()):
    if src not in source.data.bones and 'c_' + src in source.data.bones:
        mapping[name] = 'c_' + src
missing = [name for name, src in mapping.items() if src not in source.data.bones]
if missing:
    raise RuntimeError('Unmapped source bones: ' + str(missing))
fps = bpy.context.scene.render.fps / bpy.context.scene.render.fps_base
report = {'source': str(SOURCE_PATH), 'source_sha256': hashlib.sha256(SOURCE_PATH.read_bytes()).hexdigest(),
    'fps': fps, 'target_bones': len(mapping), 'clips': []}
for name in CLIPS:
    source.animation_data.action = None
    for pb in source.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
        for key, value in initial_properties[pb.name].items():
            pb[key] = value
    source.update_tag()
    action = bpy.data.actions[name]
    source.animation_data.action = action
    slots = [(sum(len(st.channelbag(s).fcurves) if st.channelbag(s) else 0
        for layer in action.layers for st in layer.strips), s) for s in action.slots]
    source.animation_data.action_slot = max(slots, key=lambda item: item[0])[1]
    target.animation_data_create()
    baked = bpy.data.actions.new('A_Teeth_' + name.title())
    target.animation_data.action = baked
    start, source_end = map(int, action.frame_range)
    # Point is an artist-authored single pose. Give FBX a real two-second span;
    # runtime blends into and out of that pose rather than inventing motion.
    end = start + round(fps * 2) if name == 'point' else source_end
    bpy.context.scene.frame_start, bpy.context.scene.frame_end = start, end
    bpy.context.scene.frame_set(start)
    bpy.context.view_layer.update()
    evaluated = source.evaluated_get(bpy.context.evaluated_depsgraph_get())
    root_offset = (evaluated.pose.bones['root.x'].matrix.translation - source.data.bones['root.x'].matrix_local.translation).copy()
    maximum_motion = 0
    maximum_error = 0
    previous = {}
    for frame in range(start, end + 1):
        bpy.context.scene.frame_set(frame)
        bpy.context.view_layer.update()
        evaluated = source.evaluated_get(bpy.context.evaluated_depsgraph_get())
        desired = {}
        for bone in target.data.bones:
            src = mapping[bone.name]
            pose = evaluated.pose.bones[src].matrix.copy()
            rest = source.data.bones[src].matrix_local.copy()
            pose.translation *= 100
            rest.translation *= 100
            desired[bone.name] = pose @ rest.inverted() @ bone.matrix_local
            desired[bone.name].translation -= root_offset * 100
            maximum_motion = max(maximum_motion, (desired[bone.name].translation - bone.matrix_local.translation).length)
        actual = {}
        for bone in target.data.bones:
            pb = target.pose.bones[bone.name]
            parent = actual[bone.parent.name] if bone.parent else Matrix.Identity(4)
            parent_rest = bone.parent.matrix_local if bone.parent else Matrix.Identity(4)
            pb.matrix_basis = bone.convert_local_to_pose(desired[bone.name], bone.matrix_local,
                parent_matrix=parent, parent_matrix_local=parent_rest, invert=True)
            if bone.name in previous and pb.rotation_quaternion.dot(previous[bone.name]) < 0:
                pb.rotation_quaternion.negate()
            previous[bone.name] = pb.rotation_quaternion.copy()
            actual[bone.name] = bone.convert_local_to_pose(pb.matrix_basis, bone.matrix_local,
                parent_matrix=parent, parent_matrix_local=parent_rest)
            maximum_error = max(maximum_error, (actual[bone.name].translation - desired[bone.name].translation).length)
            for prop in ('location', 'rotation_quaternion', 'scale'):
                pb.keyframe_insert(prop, frame=frame, group=bone.name)
    bpy.ops.object.select_all(action='DESELECT')
    target.hide_set(False)
    target.select_set(True)
    bpy.context.view_layer.objects.active = target
    path = OUT / ('A_Teeth_' + name.title() + '.fbx')
    bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={'ARMATURE'}, add_leaf_bones=False,
        use_armature_deform_only=False, bake_anim=True, bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=False, bake_anim_simplify_factor=0)
    if maximum_error > .01:
        raise RuntimeError(f'{name}: baked bone endpoint error {maximum_error:.4f} cm')
    report['clips'].append({'name': name, 'frames': [start, end], 'source_frames': [start, source_end],
        'max_motion_cm': maximum_motion, 'max_endpoint_error_cm': maximum_error, 'file': str(path)})
    print('MC_CHARACTER2_BAKED', name, 'error_cm', maximum_error, flush=True)
    target.animation_data.action = None
    bpy.data.actions.remove(baked)
(OUT / 'BakeReport.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf8')
print('MC_CHARACTER2_BAKE_PASS', flush=True)
