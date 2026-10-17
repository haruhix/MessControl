"""Author an editable, Blender-only pool from the approved native source rig."""
import hashlib
import importlib.util
import json
import math
import re
from pathlib import Path
from types import SimpleNamespace

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'ArtSource/NutAnimationStyle/Wizard/Original/NutWizard_Original.blend'
DEST = ROOT / 'ArtSource/NutAnimationStyle/Wizard/MixamoPool'
BLEND = DEST / 'NutWizard_Mixamo_AnimationPool_v2.blend'
REPORT = DEST / 'AnimationPoolReport_v2.json'
OUTPUT = ROOT / 'Artifacts/WizardMixamoPool'


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


author = module('mixamo_author', 'author_wizard_mixamo_variants.py')
recipes = module('mixamo_recipes', 'wizard_mixamo_pool_recipes.py')
review = module('mixamo_review', 'preview_wizard_human_v8.py')


def defaults(rig):
    return {bone.name: dict(basis=bone.matrix_basis.copy(), properties={key: bone[key] for key in bone.keys()
            if isinstance(bone[key], (bool, int, float))}) for bone in rig.pose.bones}


def signature(action):
    return [(fc.data_path, fc.array_index, tuple((tuple(key.co), key.interpolation) for key in fc.keyframe_points))
            for fc in author.curves(action)]


def source_frame(segment, frame):
    points = segment.get('timing_points')
    if points:
        return author.remap(frame, points)
    t = (frame-segment['start']) / max(1, segment['end']-segment['start'])
    if segment.get('ease', segment.get('easing')) in ('smooth', 'smoothstep', 'ease_in_out'):
        t = author.smooth(t)
    return segment['source_start'] + (segment['source_end']-segment['source_start'])*t


def bake(rig, action, stored_defaults, records):
    author.bind(rig, action, stored_defaults)
    previous_euler, previous_quat = {}, {}
    for frame, (pose, props) in enumerate(records, 1):
        bpy.context.scene.frame_set(frame)
        for name, (loc, rotation, scale) in pose.items():
            bone = rig.pose.bones[name]
            bone.location, bone.scale = loc, scale
            if bone.rotation_mode == 'QUATERNION':
                if name in previous_quat and previous_quat[name].dot(rotation) < 0:
                    rotation = -rotation
                bone.rotation_quaternion = rotation
                previous_quat[name] = rotation.copy()
                prop = 'rotation_quaternion'
            else:
                assert bone.rotation_mode != 'AXIS_ANGLE', name
                bone.rotation_euler = rotation.to_euler(bone.rotation_mode, previous_euler.get(name)) if name in previous_euler else rotation.to_euler(bone.rotation_mode)
                previous_euler[name] = bone.rotation_euler.copy()
                prop = 'rotation_euler'
            for path in ('location', prop, 'scale'):
                bone.keyframe_insert(data_path=path, frame=frame, group=name)
        for (name, prop), value in props.items():
            bone = rig.pose.bones[name]
            bone[prop] = value
            bone.keyframe_insert(data_path='["'+prop+'"]', frame=frame, group=name)
    for curve in author.curves(action):
        for key in curve.keyframe_points:
            key.interpolation = 'LINEAR'
        curve.update()


def main():
    assert bpy.app.background, 'Use isolated background Blender'
    assert not BLEND.exists(), 'Preserve previous art revisions'
    assert not recipes.validate_recipes(), recipes.validate_recipes()
    original_hash = author.sha(SOURCE)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    scene, rig, mesh = bpy.context.scene, bpy.data.objects['rig'], bpy.data.objects['SM_NutWizard']
    rig.animation_data_create()
    stored_defaults = defaults(rig)
    originals = {name: bpy.data.actions[info['action']] for name, info in recipes.ARTIST_ACTIONS.items()}
    signatures = {name: signature(action) for name, action in originals.items()}
    rest = {bone.name: tuple(value for row in bone.matrix_local for value in row) for bone in rig.data.bones}
    paths = {fc.data_path for action in originals.values() for fc in author.curves(action)}
    names = sorted({re.match(r'pose\.bones\["([^"]+)"\]', path)[1] for path in paths if path.startswith('pose.bones[')})
    properties = [re.match(r'pose\.bones\["([^"]+)"\]\["([^"]+)"\]', path).groups() for path in sorted(paths) if path.endswith(']')]
    upper = {name for name in names if not any(word in name for word in ('foot', 'toes', 'leg', 'thigh', 'root', 'traj'))}
    cache = {}

    def sample(source, frame):
        key = source, round(frame, 7)
        if key not in cache:
            author.bind(rig, originals[source], stored_defaults)
            scene.frame_set(math.floor(frame), subframe=frame-math.floor(frame))
            cache[key] = ({name: rig.pose.bones[name].matrix_basis.decompose() for name in names},
                          {(name, prop): float(rig.pose.bones[name][prop]) for name, prop in properties})
        return cache[key]

    neutral, neutral_props = sample('Firenut', 1.)
    scene.render.fps, scene.render.fps_base = 30, 1.
    catalog, actions = [], []
    for number, recipe in enumerate(recipes.RECIPES, 1):
        records = [(author.copy_pose(neutral), dict(neutral_props)) for _ in range(recipe['neutral_in_frames'])]
        previous = None
        for segment in recipe['segments']:
            entry, entry_props = records[-1]
            continuous = bool(previous and previous['source'] == segment['source']
                              and previous['part'] == segment['part']
                              and abs(previous['source_end']-segment['source_start']) < 1e-6)
            # Blends bridge different poses/sources; never damp a continuous
            # source release just because its timing map was divided into beats.
            blend_frames = segment['blend_in'] if not continuous else 0
            for frame in range(segment['start'], segment['end']+1):
                pose, props = sample(segment['source'], source_frame(segment, frame))
                pose, props = author.copy_pose(pose), dict(props)
                if segment.get('part') == 'upper':
                    foundation = segment.get('lower_foundation')
                    if not foundation and 'lower_source' in segment:
                        foundation = dict(source=segment['lower_source'], frame=segment['lower_source_frame'])
                    if foundation:
                        if 'frame' in foundation:
                            lower_frame = float(foundation['frame'])
                        else:
                            lower_frame = source_frame(dict(segment, **foundation), frame)
                        lower, lower_props = sample(foundation['source'], lower_frame)
                    else:
                        lower, lower_props = entry, entry_props
                    pose = author.blend_pose(lower, pose, 1., upper)
                    props.update({key: value for key, value in lower_props.items()
                                  if any(word in key[0] for word in ('foot', 'leg', 'thigh'))})
                if blend_frames:
                    weight = author.smooth((frame-segment['start']+1)/blend_frames)
                    pose = author.blend_pose(entry, pose, weight)
                    props = {key: entry_props[key]+(value-entry_props[key])*weight for key, value in props.items()}
                for name, transform in pose.items():
                    assert all(math.isfinite(v) for component in transform for v in component), (recipe['name'], frame, name)
                records.append((pose, props))
            previous = segment
        last, last_props = records[-1]
        for index in range(1, recipe['neutral_out_frames']+1):
            weight = author.smooth(index/recipe['neutral_out_frames'])
            records.append((author.blend_pose(last, neutral, weight),
                            {key: value+(neutral_props[key]-value)*weight for key, value in last_props.items()}))
        assert len(records) == recipe['frames']
        assert all((records[0][0][name][0]-records[-1][0][name][0]).length < 1e-6
                   and records[0][0][name][1].rotation_difference(records[-1][0][name][1]).angle < 1e-5 for name in names)
        action = bpy.data.actions.new('MC_POOL_'+recipe['name'])
        action.use_fake_user = True
        action['labelRU'], action['groupRU'], action['useRU'] = recipe['labelRU'], recipe['groupRU'], recipe['useRU']
        action['frame_start'], action['frame_end'], action['duration_seconds'] = 1, recipe['frames'], recipe['frames']/30
        action['source_recipe_json'] = json.dumps(recipe, ensure_ascii=False)
        bake(rig, action, stored_defaults, records)
        actions.append(action)
        row = dict(recipe, ordinal=number, action=action.name, curves=len(author.curves(action)))
        catalog.append(row)
        print('POOL_ACTION_READY', number, recipe['name'], recipe['frames'], flush=True)
    assert all(signature(action) == signatures[name] for name, action in originals.items())
    assert rest == {bone.name: tuple(value for row in bone.matrix_local for value in row) for bone in rig.data.bones}
    settings = SimpleNamespace(width=640, height=480, render_aa='5', focus=(-.01,-.13,.85), ortho_scale=3.1)
    cameras = review.setup_scene(scene, rig, mesh, settings)
    review.activate = author.bind
    scale = review.fit_cameras(scene, rig, mesh, cameras,
            [dict(action=action, frame_start=1, frame_end=int(action['frame_end'])) for action in actions], stored_defaults, 3.1)
    scene.camera = cameras['ThreeQuarter']
    scene.timeline_markers.clear()
    author.bind(rig, None, stored_defaults)
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    track = rig.animation_data.nla_tracks.new()
    track.name = 'MC_POOL_ALL_32_PREVIEW'
    cursor = 1
    for action, row in zip(actions, catalog):
        strip = track.strips.new('%02d %s' % (row['ordinal'], row['name']), cursor, action)
        strip.action_frame_start, strip.action_frame_end = 1, row['frames']+1
        strip.frame_end = cursor+row['frames']
        strip.blend_type, strip.extrapolation = 'REPLACE', 'NOTHING'
        strip.blend_in, strip.blend_out = 0, 0
        strip.use_auto_blend = False
        if action.slots:
            strip.action_slot = action.slots[0]
        scene.timeline_markers.new('%02d %s' % (row['ordinal'], row['labelRU'][:24]), frame=cursor)
        row['preview_start'], row['preview_end'] = cursor, cursor+row['frames']-1
        cursor += row['frames']
    rig.animation_data.use_nla = True
    track.mute = False
    scene.frame_start, scene.frame_end = 1, cursor-1
    scene.frame_set(1)
    for obj in bpy.context.selected_objects:
        obj.select_set(False)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == 'VIEW_3D':
                area.spaces.active.region_3d.view_perspective = 'CAMERA'
    notes = bpy.data.texts.new('MIXAMO_POOL_README')
    notes.write('32 Blender-only studies derived from the three approved original retargeted artist performances.\n'
                'Not 32 downloaded Mixamo clips. Original ARP rig, skin and source Actions are preserved.\n'
                'NLA track MC_POOL_ALL_32_PREVIEW plays the complete pool. Timeline markers show each clip.\n'
                'To edit an individual MC_POOL_* Action, mute the pool NLA track and assign its Action slot.\n'
                '30fps authoring; video renders real Blender subframes at60fps.\n'
                'Channel/defense/utility clips are gesture studies for review, without VFX or gameplay.\n\n')
    for row in catalog:
        notes.write('%02d. %s — %s\n%s\n\n' % (row['ordinal'], row['action'], row['labelRU'], row['useRU']))
    DEST.mkdir(parents=True, exist_ok=True)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND))
    assert author.sha(SOURCE) == original_hash
    report = dict(passed=True, source=str(SOURCE), source_sha256=original_hash,
        blend=str(BLEND), blend_sha256=author.sha(BLEND), source_fps=30,
        clip_count=len(catalog), timeline_frames=cursor-1, duration_seconds=(cursor-1)/30,
        original_actions_preserved=True, source_file_preserved=True, rig_and_rest_preserved=True,
        neutral_endpoints_match=True, ortho_scale=scale, names=names, clips=catalog,
        scope='Blender-only animation studies. No Unreal import.',
        limitations=['32 edits/compositions from three original artist motions; no additional Mixamo downloads.',
                     'Gesture prototypes for artistic review; gameplay timing/events and VFX are not integrated.'])
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print('MIXAMO_POOL_AUTHORED', str(BLEND), flush=True)


if __name__ == '__main__':
    main()
