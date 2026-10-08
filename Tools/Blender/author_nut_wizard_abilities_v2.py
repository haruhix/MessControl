"""Add the missing close-range attack to the saved wizard study.

The first-pass blend and original artist file stay read-only. Pose helpers are
loaded as definitions through AST, without running first-pass authoring.
"""
import ast, hashlib, json, math
from pathlib import Path
import bpy
from mathutils import Euler, Matrix, Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/NutAnimationStyle/Wizard'
INPUT = OUT / 'NutWizard_Style.blend'
SOURCE = Path('C:/Users/user/Downloads/Telegram Desktop/Nut_Wizard.blend')
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
input_hash, source_hash = digest(INPUT), digest(SOURCE)
bpy.ops.wm.open_mainfile(filepath=str(INPUT))
scene = bpy.context.scene
rig = bpy.data.objects['rig']
for track in rig.animation_data.nla_tracks:
    track.mute = True
rig.animation_data.use_nla = False

def curves(action):
    return [f for layer in action.layers for strip in layer.strips
            for bag in strip.channelbags for f in bag.fcurves]

def action_hash(action):
    payload = [(f.data_path, f.array_index,
                [(list(p.co), list(p.handle_left), list(p.handle_right),
                  p.interpolation) for p in f.keyframe_points])
               for f in curves(action)]
    return hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()

existing_hashes = {a.name: action_hash(a) for a in bpy.data.actions}
rest_signature = [(b.name, b.parent.name if b.parent else None,
                   [list(row) for row in b.matrix_local], b.use_deform)
                  for b in rig.data.bones]
rig.animation_data.action = bpy.data.actions['MC_Wizard_Idle']
rig.animation_data.action_slot = rig.animation_data.action.slots[0]
scene.frame_set(1)
bpy.context.view_layer.update()
rest = {b.name: b.matrix_local.copy() for b in rig.data.bones}
original_props = {b.name: {k: b[k] for k in b.keys()
                         if isinstance(b[k], (float, int, bool))}
                  for b in rig.pose.bones}
controls = [b for b in rig.pose.bones if b.name.startswith('c_')]
records = []
helper = ROOT / 'Tools/Blender/author_nut_wizard_style.py'
tree = ast.parse(helper.read_text(encoding='utf8'))
names = {'bind', 'reset', 'set_matrix', 'pose', 'apply', 'key', 'mix', 'make'}
definitions = [node for node in tree.body
               if isinstance(node, ast.FunctionDef) and node.name in names
               or isinstance(node, ast.Assign)
               and any(isinstance(t, ast.Name) and t.id == 'BASE' for t in node.targets)]
exec(compile(ast.Module(body=definitions, type_ignores=[]), str(helper), 'exec'))

# A startled, disdainful shove: closed silhouette in the wind-up, one palm
# driven forward at the damage instant, then a backward rebound. It reads as
# close-range self-defence rather than a second projectile spell. Feet stay put.
idle = pose()
coil = pose(root=(-.025, .025, -.085), body=(-8, 0, 15), head=(9, 0, -12),
            r=(-.47, -.11, .72), l=(.60, .01, .62),
            rr=(0, -15, 58), lr=(0, 0, -12), grasp=.40)
held = pose(root=(-.035, .035, -.10), body=(-11, 0, 20), head=(12, 0, -16),
            r=(-.46, -.10, .76), l=(.64, .045, .60),
            rr=(0, -18, 65), lr=(0, 0, -8), grasp=.35)
contact = pose(root=(.035, -.035, -.045), body=(17, 0, -14), head=(-10, 0, 8),
               r=(-.35, -.49, .78), l=(.68, .055, .63),
               rr=(-15, -12, 98), lr=(0, 0, 4), grasp=0)
follow = pose(root=(.045, -.043, -.06), body=(21, 0, -20), head=(-12, 0, 13),
              r=(-.32, -.50, .76), l=(.70, .065, .63),
              rr=(-20, -10, 102), lr=(0, 0, 7), grasp=.05)
rebound = pose(root=(-.018, .018, -.067), body=(-5, 0, 7), head=(7, 0, -4),
               r=(-.52, -.17, .70), l=(.62, -.08, .66),
               rr=(0, 0, 60), lr=(0, 0, -35), grasp=.25)
make('Melee', [(1, idle), (18, coil), (41, held), (50, held),
               (56, contact), (62, follow), (78, rebound), (91, idle), (101, idle)],
     note='Close-range palm shove. Distinct from FireNut; damage accent at 55%, planted feet and recoil.')
action = bpy.data.actions['MC_Wizard_Melee']
action['ability'] = 'Mage Melee'
action['release_fraction'] = .55
action['integration_required'] = 'Add a dedicated MageMelee runtime slot; current game reuses MageCast.'
bind(action)
scene.frame_start = 1
scene.frame_end = 101
scene.frame_set(56)
scene['MC_AbilityRevision'] = 'v2 adds the missing mage melee. FireNut, Summon, Rain and Melee are the four active abilities; HeavyCast is fallback.'
readme = bpy.data.texts.get('MC_ANIMATION_README')
readme.write('\nV2: MC_Wizard_Melee adds a dedicated close-range shove. It is not connected to Unreal yet. HeavyCast is a fallback, not a fourth spell.\n')
blend = OUT / 'NutWizard_Style_v2.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(blend))

# Validate the full new performance and preserve every previously saved curve.
assert existing_hashes == {n: action_hash(bpy.data.actions[n]) for n in existing_hashes}
assert rest_signature == [(b.name, b.parent.name if b.parent else None,
                           [list(row) for row in b.matrix_local], b.use_deform)
                          for b in rig.data.bones]
limbs = ['thigh_stretch.l', 'thigh_stretch.r', 'leg_stretch.l', 'leg_stretch.r',
         'arm_stretch.l', 'arm_stretch.r', 'forearm_stretch.l', 'forearm_stretch.r']
ratios = {n: [float('inf'), float('-inf')] for n in limbs}
feet = {n: [] for n in ('foot.l', 'foot.r')}
minimum_z = float('inf')
minimum_frame = 0
meshes = [o for o in scene.objects if o.type == 'MESH'
          and any(m.type == 'ARMATURE' and m.object == rig for m in o.modifiers)]
for frame in range(1, 102):
    scene.frame_set(frame)
    bpy.context.view_layer.update()
    for b in rig.pose.bones:
        assert all(math.isfinite(v) for row in b.matrix for v in row), b.name
    for n in limbs:
        b = rig.pose.bones[n]
        ratio = (b.tail - b.head).length / rig.data.bones[n].length
        ratios[n][0] = min(ratios[n][0], ratio)
        ratios[n][1] = max(ratios[n][1], ratio)
        assert .95 < ratio < 1.05, (frame, n, ratio)
    for n in feet:
        feet[n].append(list(rig.pose.bones[n].matrix.translation))
    deps = bpy.context.evaluated_depsgraph_get()
    for obj in meshes:
        evaluated = obj.evaluated_get(deps)
        mesh = evaluated.to_mesh()
        try:
            z = min((evaluated.matrix_world @ v.co).z for v in mesh.vertices)
            if z < minimum_z:
                minimum_z, minimum_frame = z, frame
        finally:
            evaluated.to_mesh_clear()
assert minimum_z >= -.005, (minimum_z, minimum_frame)
foot_drift = {n: max((Vector(p) - Vector(points[0])).length for p in points)
              for n, points in feet.items()}
assert max(foot_drift.values()) < .002, foot_drift
assert digest(INPUT) == input_hash and digest(SOURCE) == source_hash
report = {'blend': str(blend), 'new_action': records[0], 'existing_actions_unchanged': True,
          'existing_action_curve_hashes': existing_hashes, 'rig_rest_unchanged': True,
          'input_blend_unchanged': True, 'original_source_unchanged': True,
          'frames_checked': 101, 'limb_length_ratio_ranges': ratios,
          'foot_drift_m': foot_drift, 'mesh_min_z_m': minimum_z,
          'mesh_min_z_frame': minimum_frame, 'finite_pose_matrices': True,
          'game_imported': False, 'runtime_gap': 'MageMelee currently uses MageCast; dedicated slot required.'}
(OUT / 'WizardAbilitiesV2Report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
poses = OUT / 'PosesV2'
poses.mkdir(exist_ok=True)
for frame in (1, 41, 56, 62, 78, 101):
    scene.frame_set(frame)
    scene.render.filepath = str(poses / ('Melee_%03d.png' % frame))
    bpy.ops.render.render(write_still=True)
print('WIZARD_ABILITIES_V2_COMPLETE', str(blend), flush=True)
