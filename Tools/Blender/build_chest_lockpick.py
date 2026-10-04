"""Editable five-second lockpick approval scene. No Unreal animation import.

Run an isolated Blender process with --factory-startup --background --disable-autoexec
--threads 2 --python Tools/Blender/build_chest_lockpick.py. Optional -- --render
renders the lightweight Workbench playblast; -- --stills renders contact studies.
The current player skeleton and the exported game props are copied, never edited.
"""
import bpy
import hashlib
import json
import math
import sys
from pathlib import Path
from mathutils import Matrix, Quaternion, Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/ChestLockpick'
REVIEW = ROOT / 'Artifacts/ChestLockpickReview_20261004'
PROPS = OUT / 'Props'
SOURCE = ROOT / 'ArtSource/CharacterCurrent/CharacterGameplay.blend'
OUT.mkdir(parents=True, exist_ok=True)
REVIEW.mkdir(parents=True, exist_ok=True)
before_hash = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
scene = bpy.context.scene
scene.name = 'Chest lockpick - APPROVAL'
rig = next(obj for obj in scene.objects if obj.type == 'ARMATURE')
hero = next(obj for obj in scene.objects if obj.type == 'MESH')
assert len(rig.data.bones) == 107, 'Use the current player reference skeleton.'
rig.name = 'Hero_Current_107Bones'
rig.animation_data_clear()
rig.rotation_euler = (0, 0, math.pi)
rig.location = (-.17, -.90, .018)
rig.show_in_front = False
for polygon in hero.data.polygons:
    polygon.use_smooth = True
for bone in rig.pose.bones:
    bone.rotation_mode = 'QUATERNION'
    bone.matrix_basis = Matrix.Identity(4)
    for constraint in list(bone.constraints):
        bone.constraints.remove(constraint)
if hero.data.shape_keys:
    hero.data.shape_keys.animation_data_clear()
    for key in hero.data.shape_keys.key_blocks:
        key.value = 0

def import_mesh(path, name):
    assert path.is_file(), 'Missing game prop export: ' + str(path)
    previous = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(path))
    created = list(set(bpy.data.objects) - previous)
    meshes = [obj for obj in created if obj.type == 'MESH']
    assert len(meshes) == 1, 'Expected one actual game prop mesh.'
    obj = meshes[0]
    obj.parent = None
    obj.name = name
    obj.rotation_euler = (0, 0, 0)
    return obj

def material(name, color, texture=None):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color[:3], 1)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = (*color[:3], 1)
    bsdf.inputs['Roughness'].default_value = .42
    if texture and texture.is_file():
        tex = mat.node_tree.nodes.new('ShaderNodeTexImage')
        tex.image = bpy.data.images.load(str(texture), check_existing=True)
        mat.node_tree.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
        mat.node_tree.nodes.active = tex
    return mat

def bounds(obj):
    # The datablock was just reoriented; object.bound_box can still be cached.
    points = [vertex.co.copy() for vertex in obj.data.vertices]
    return Vector(tuple(min(p[i] for p in points) for i in range(3))), Vector(tuple(max(p[i] for p in points) for i in range(3)))

bottom = import_mesh(PROPS / 'SM_Chest_Bot.fbx', 'Chest_Bottom_GameMesh')
lid = import_mesh(PROPS / 'SM_Chest_Top.fbx', 'Chest_Lid_GameMesh')
# The actual aperture is on source +X. Reorient both copied mesh datablocks so
# the existing front faces -Y in this authoring scene; keep original exports.
front_rotation = Matrix.Rotation(-math.pi/2, 4, 'Z')
bottom.data.transform(front_rotation)
lid.data.transform(front_rotation)
bottom_lo, bottom_hi = bounds(bottom)
lid_lo, lid_hi = bounds(lid)
# Imported FBX centimetres, preview world metres; ModelScale matches the actor.
PROP_SCALE = .0035
bottom.scale = (PROP_SCALE,) * 3
bottom.location = (0, 0, -bottom_lo.z * PROP_SCALE)
chest_height = (bottom_hi.z - bottom_lo.z) * PROP_SCALE
front_y = bottom_lo.y * PROP_SCALE
bpy.ops.object.empty_add(type='PLAIN_AXES', location=(0, lid_hi.y * PROP_SCALE, chest_height))
hinge = bpy.context.object
hinge.name = 'Chest_Lid_Hinge'
hinge.empty_display_size = .07
lid.parent = hinge
lid.scale = (PROP_SCALE,) * 3
lid.location = (0, -lid_hi.y * PROP_SCALE, -lid_lo.z * PROP_SCALE)
chest_mat = material('Game chest - exported BaseColor', (.84, .62, .14), PROPS / 'Chest_BaseColor.png')
for obj in (bottom, lid):
    obj.data.materials.clear()
    obj.data.materials.append(chest_mat)
    for polygon in obj.data.polygons:
        polygon.use_smooth = False

brush = import_mesh(ROOT / 'ArtSource/CharacterCurrent/Tools/SM_Brush.fbx', 'Brush_Key_GameMesh')
brush.scale = (.0035,) * 3
brush.rotation_mode = 'QUATERNION'
brush.data.materials.clear()
brush.data.materials.append(material('Game brush - BaseColor', (.02, .5, .75), ROOT / 'ArtSource/CharacterCurrent/Tools/Brush_BaseColor.png'))
tip_vertices = [v.co.copy() for v in brush.data.vertices if v.co.x < min(p.co.x for p in brush.data.vertices) + 1.5]
tip_local = sum(tip_vertices, Vector()) / len(tip_vertices)
grip_local = Vector((31, 0, tip_local.z))

# Actual keyhole ring vertices in the saved Bottom mesh: local X=147.810 cm,
# Y=+-19.075 cm, circle center Z=68.042 cm. Reuse that existing opening.
socket = Vector((0, front_y - .003, (68.042-bottom_lo.z)*PROP_SCALE))
bpy.ops.object.empty_add(type='PLAIN_AXES', location=socket)
contact = bpy.context.object
contact.name = 'CONTACT_Brush_Handle_Tip'
contact.empty_display_size = .025
contact['purpose'] = 'Thin handle tip stays inside this socket during frames 29-96; brush head remains outside.'

def smooth(a, b, t):
    x = max(0., min(1., (t-a)/(b-a)))
    return x*x*(3-2*x)

def aim_matrix(rest, start, end, rest_direction):
    rotation = rest_direction.normalized().rotation_difference((end-start).normalized())
    result = rotation.to_matrix().to_4x4() @ rest
    result.translation = start
    return result

def arm_pose(side, wrist_world, hand_direction):
    names = ['arm_stretch_'+side, 'forearm_stretch_'+side, 'hand_'+side]
    upper, lower, hand = [rig.pose.bones[n] for n in names]
    inverse = rig.matrix_world.inverted()
    wrist = inverse @ wrist_world
    shoulder = upper.matrix.translation.copy()
    elbow_rest = lower.bone.head_local.copy()
    wrist_rest = hand.bone.head_local.copy()
    upper_rest = upper.bone.head_local.copy()
    l1, l2 = (elbow_rest-upper_rest).length, (wrist_rest-elbow_rest).length
    direction = (wrist-shoulder).normalized()
    distance = (wrist-shoulder).length
    # The two arm joints solve the contact pose without changing the reference rig.
    if distance > l1+l2-.001:
        extension = distance/(l1+l2-.001)
        l1 *= extension
        l2 *= extension
    along = max(.001, min(distance-.001, (l1*l1-l2*l2+distance*distance)/(2*distance)))
    pole = inverse.to_3x3() @ Vector((1 if side=='r' else -1, -.3, -.65))
    pole -= direction*direction.dot(pole)
    pole.normalize()
    elbow = shoulder + direction*along + pole*math.sqrt(max(.001, l1*l1-along*along))
    upper.matrix = aim_matrix(upper.bone.matrix_local, shoulder, elbow, elbow_rest-upper_rest)
    bpy.context.view_layer.update()
    lower.matrix = aim_matrix(lower.bone.matrix_local, elbow, wrist, wrist_rest-elbow_rest)
    bpy.context.view_layer.update()
    hand_end = wrist + inverse.to_3x3() @ hand_direction.normalized()
    hand.matrix = aim_matrix(hand.bone.matrix_local, wrist, hand_end, hand.bone.tail_local-hand.bone.head_local)
    bpy.context.view_layer.update()

scene.frame_start = 1
scene.frame_end = 120
scene.render.fps = 24
rig.animation_data_create()
action = bpy.data.actions.new('A_Hero_ChestLockpick_APPROVAL_5s')
action.use_fake_user = True
action.asset_mark()
rig.animation_data.action = action
contact_errors = []
for frame in range(1, 121):
    scene.frame_set(frame)
    for bone in rig.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    approach = smooth(1, 22, frame)
    retreat = smooth(102, 120, frame)
    agitation = smooth(31, 40, frame)*(1-smooth(83, 96, frame))
    jiggle = math.sin((frame-35)*.46)*math.radians(17)*agitation
    fine = math.sin((frame-33)*1.1)*math.radians(3)*agitation
    final_turn = smooth(86, 96, frame)*math.radians(56)*(1-smooth(98, 111, frame))
    twist = jiggle + fine + final_turn
    inserted = smooth(17, 29, frame)
    withdrawal = smooth(97, 110, frame)
    insertion_axis = Vector((-.70710678, .70710678, 0))
    tip = socket + insertion_axis * (-.15*(1-inserted) + .012*inserted - .16*withdrawal) + Vector((0, 0, .007))
    tip.x += .004*math.sin(frame*.7)*agitation
    base_orientation = Quaternion((0, 0, 1), -math.pi/4)
    orientation = Quaternion(insertion_axis, twist) @ base_orientation
    brush.rotation_quaternion = orientation
    brush.location = tip - orientation @ (tip_local*.0035)
    brush.keyframe_insert('location', frame=frame)
    brush.keyframe_insert('rotation_quaternion', frame=frame)
    rig.location = (-.17, -1.02 + .035*approach - .018*retreat, .018)
    rig.keyframe_insert('location', frame=frame)
    root = rig.pose.bones['root_x']
    root.rotation_quaternion = Quaternion((1, 0, 0), math.radians(4)*approach*(1-retreat) + math.sin(frame*.46)*.012*agitation)
    root.location.z = -.7*agitation
    rig.pose.bones['head_x'].rotation_quaternion = Quaternion((1, 0, 0), math.radians(8)*approach*(1-retreat)) @ Quaternion((0, 0, 1), .022*math.sin(frame*.35)*agitation)
    # Bring the clavicle/shoulder chains forward before solving both arms.
    # This gives the short tooth limbs a reachable contact pose and keeps its
    # face/crown clear of the chest instead of pushing the torso into the lock.
    for side, sign in [('r', 1), ('l', -1)]:
        pb = rig.pose.bones['shoulder_'+side]
        basis = pb.bone.matrix_local.to_quaternion()
        pb.rotation_quaternion = basis.inverted() @ Quaternion((0, 0, 1), sign*math.radians(46)*approach*(1-retreat)) @ basis
    bpy.context.view_layer.update()
    grip = brush.matrix_world @ grip_local
    wrist = grip + orientation @ Vector((0, -.038, -.012))
    # Right hand cups the handle; the thin handle end enters the lock.
    arm_pose('r', wrist, orientation @ Vector((0, .12, -1)))
    brace = Vector((-.46, front_y-.14, chest_height-.015))
    brace_start = Vector((-.49, -.99, .41))
    brace_position = brace_start.lerp(brace, approach*(1-retreat))
    arm_pose('l', brace_position, Vector((0, 1, -.15)))
    for side in ('r', 'l'):
        for name in ('index1_', 'index2_', 'index3_', 'thumb1_', 'thumb2_', 'thumb3_'):
            pb = rig.pose.bones.get(name+side)
            if pb:
                curl = (.45 if side=='r' else .08)*approach*(1-retreat)
                pb.rotation_quaternion = Quaternion((1, 0, 0), curl if not name.startswith('thumb') else -curl*.7)
    for bone in rig.pose.bones:
        for prop in ('location', 'rotation_quaternion', 'scale'):
            bone.keyframe_insert(prop, frame=frame, group=bone.name)
    if hero.data.shape_keys:
        effort = hero.data.shape_keys.key_blocks.get('Mouth_Effort')
        if effort:
            effort.value = .33*agitation
            effort.keyframe_insert('value', frame=frame)
        blink = hero.data.shape_keys.key_blocks.get('Eyes_Blink')
        if blink:
            blink.value = max(0, 1-abs(frame-27)/2, 1-abs(frame-79)/2)
            blink.keyframe_insert('value', frame=frame)
    hinge.rotation_euler.x = -math.radians(27)*smooth(106, 120, frame)
    hinge.keyframe_insert('rotation_euler', frame=frame)
    if 32 <= frame <= 96:
        contact_errors.append((brush.matrix_world @ tip_local-tip).length)

# Render a readable material playblast, leaving the editable scene in camera view.
floor_mat = material('Review floor', (.095, .13, .16))
bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -.004))
floor = bpy.context.object
floor.name = 'Review_Floor'
floor.data.materials.append(floor_mat)
scene.world = bpy.data.worlds.new('Lockpick review world')
scene.world.color = (.075, .095, .12)
def camera(name, position, focus, scale):
    bpy.ops.object.camera_add(location=position)
    obj = bpy.context.object
    obj.name = name
    obj.rotation_euler = (Vector(focus)-obj.location).to_track_quat('-Z', 'Y').to_euler()
    obj.data.type = 'ORTHO'
    obj.data.ortho_scale = scale
    obj.data.lens = 55
    return obj
wide = camera('Camera_Whole_Interaction', (3.2, -2.0, 1.9), (-.1, -.35, .57), 2.80)
close = camera('Camera_Contact_Closeup', (2.8, -1.6, 1.15), (.025, front_y-.075, .585), 1.10)
scene.camera = wide
for marker in list(scene.timeline_markers):
    scene.timeline_markers.remove(marker)
for frame, label in [(1, 'Approach and brace'), (17, 'Insert handle tip'), (29, 'Contact held - lockpick'), (86, 'Final key turn'), (96, 'UNLOCK'), (97, 'Withdraw brush'), (106, 'Release lid'), (120, 'End - 5 seconds')]:
    scene.timeline_markers.new(label, frame=frame)
scene.timeline_markers.new('SHOT - full interaction', frame=1).camera = wide
scene.timeline_markers.new('SHOT - handle in lock', frame=26).camera = close
scene.timeline_markers.new('SHOT - unlocked chest', frame=106).camera = wide
scene.render.engine = 'BLENDER_WORKBENCH'
scene.render.resolution_x = 768
scene.render.resolution_y = 512
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.film_transparent = False
shading = scene.display.shading
shading.light = 'STUDIO'
shading.studiolight_rotate_z = math.radians(35)
shading.color_type = 'TEXTURE'
shading.show_shadows = True
shading.show_cavity = True
shading.cavity_type = 'BOTH'
shading.curvature_ridge_factor = 1.1
shading.curvature_valley_factor = .7
shading.show_specular_highlight = True
shading.show_object_outline = False
shading.background_type = 'WORLD'
scene.view_settings.view_transform = 'Standard'
scene.view_settings.look = 'None'
scene['APPROVAL_STATUS'] = 'PENDING USER APPROVAL. This scene/clip is not imported into Unreal.'
scene['TIMING'] = '120 frames / 24 fps / exactly 5 sec. Handle tip insert 17-29; lockpick 29-96; unlock 96; withdraw 97-110; lid release 106-120.'
scene['CONTACT'] = 'The brush is used handle-first as a key in the actual mesh keyhole (+X in original Chest Bot). Current hero 107-bone skeleton, actual exported Chest Bot/Top and SM_Brush. No substitute lock mesh.'
bpy.context.preferences.filepaths.save_version = 0
scene.frame_set(56)
bpy.context.view_layer.update()
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
bpy.context.view_layer.objects.active = rig
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type == 'VIEW_3D':
            area.spaces.active.region_3d.view_perspective = 'CAMERA'
            area.spaces.active.region_3d.view_camera_zoom = 0
            area.spaces.active.shading.type = 'SOLID'
            area.spaces.active.shading.color_type = 'TEXTURE'
            area.spaces.active.overlay.show_overlays = False
        elif area.type == 'DOPESHEET_EDITOR':
            area.spaces.active.mode = 'DOPESHEET'
bpy.data.orphans_purge(do_recursive=True)
bpy.ops.file.pack_all()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'ChestLockpick_Approval.blend'))
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == before_hash
report = {'source': str(SOURCE), 'source_sha256': before_hash, 'hero_bones': len(rig.data.bones), 'frames': [1, 120], 'fps': 24, 'seconds': 5,
          'prop_model_scale': .35, 'brush_handle_tip_cm': list(tip_local), 'socket_m': list(socket),
          'max_brush_contact_error_m': max(contact_errors), 'approval': 'pending', 'imported_into_unreal': False,
          'note': 'Existing Bottom keyhole (+X in source) reused. Props rotated -90 degrees only in this derived scene. Deformation preserves current reference skeleton; no source files changed.'}
(OUT / 'ApprovalReport.json').write_text(json.dumps(report, indent=2), encoding='utf8')
args = sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
if '--stills' in args:
    for frame, name in [(14, '01_Approach'), (56, '02_HandleInLock'), (91, '03_FinalTurn'), (120, '04_LidRelease')]:
        scene.frame_set(frame)
        scene.render.filepath = str(REVIEW / (name+'.png'))
        bpy.ops.render.render(write_still=True)
if '--render' in args:
    frames = ROOT / 'Saved/ChestLockpickFrames'
    frames.mkdir(parents=True, exist_ok=True)
    scene.render.filepath = str(frames / 'Frame_')
    bpy.ops.render.render(animation=True)
print('MC_CHEST_LOCKPICK_APPROVAL_SAVED', json.dumps(report), flush=True)
