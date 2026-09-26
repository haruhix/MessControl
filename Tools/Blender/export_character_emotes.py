"""Bake the animator's control rig onto the existing gameplay skeleton. Source stays untouched.
blender --background --factory-startup "Teeth (2).blend" --python this.py
"""
import bpy, json, math
from pathlib import Path
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/CharacterAnimation'
OUT.mkdir(parents=True, exist_ok=True)
source = bpy.data.objects['rig']
source.animation_data.use_nla = False
for a in ('hello', 'highfive'):
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
for name in ('hello', 'highfive'):
    source.animation_data.action = None
    for pb in source.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
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
            if bone.name.startswith('c_'):
                desired[bone.name] = (desired[bone.parent.name] @ bone.parent.matrix_local.inverted() @ rest_t) if bone.parent else rest_t.copy()
            maximum_motion = max(maximum_motion, (desired[bone.name].translation-rest_t.translation).length)
        for bone in target.data.bones:
            pb = target.pose.bones[bone.name]
            parent_matrix = desired[bone.parent.name] if bone.parent else Matrix.Identity(4)
            parent_rest = bone.parent.matrix_local if bone.parent else Matrix.Identity(4)
            pb.matrix_basis = bone.convert_local_to_pose(desired[bone.name], bone.matrix_local,
                parent_matrix=parent_matrix, parent_matrix_local=parent_rest, invert=True)
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
    report['clips'].append({'name': name, 'frames': [start,end], 'max_motion_cm': maximum_motion, 'file': str(path)})
    print('BAKED', name, maximum_motion, flush=True)

# A clean, editable gameplay preview file with both baked actions.
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
bpy.context.scene['README'] = 'Select root, Action Editor: A_Teeth_Hello / A_Teeth_Highfive. Baked from the animator source; gameplay skeleton and repaired eyelids preserved.'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'TeethAnimationLab.blend'))
(OUT / 'BakeReport.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf8')
print('MC_EMOTES_BAKE_PASS', flush=True)
