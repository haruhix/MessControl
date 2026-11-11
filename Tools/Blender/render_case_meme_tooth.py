"""Pose the real MessControl gameplay tooth for two transparent reaction portraits."""
import math
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector


ASSETS = Path(__file__).resolve().parents[2] / "ArtSource/CharacterCurrent"
args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if len(args) != 1 or Path(args[0]).resolve().drive.upper() != "E:":
    raise SystemExit("Pass an output folder on E: after --")
HERE = Path(args[0]).resolve()
HERE.mkdir(parents=True, exist_ok=True)
rig = bpy.data.objects["root"]
skin = bpy.data.objects["SK_CharacterGameplay"]
scene = bpy.context.scene
assert bpy.app.version >= (5, 0)
assert Path(bpy.data.filepath).resolve() == (ASSETS / "CharacterGameplay.blend").resolve()
rig.animation_data_clear()
skin.data.shape_keys.animation_data_clear()

scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 20
scene.cycles.use_denoising = True
scene.cycles.max_bounces = 5
scene.render.threads_mode = "FIXED"
scene.render.threads = 5
scene.render.resolution_percentage = 100
scene.render.film_transparent = True
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.render.image_settings.color_depth = "8"
scene.view_settings.view_transform = "AgX"
scene.view_settings.look = "AgX - Medium High Contrast"
scene.render.use_compositing = False

world = bpy.data.worlds.new("Portrait Ambient")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.79, 0.87, 1.0, 1.0)
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.35
scene.world = world


def aim(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat("-Z", "Y").to_euler()


def area(name, position, energy, size, color):
    light = bpy.data.lights.new(name, "AREA")
    light.energy = energy
    light.shape = "DISK"
    light.size = size
    light.color = color
    ob = bpy.data.objects.new(name, light)
    scene.collection.objects.link(ob)
    ob.location = position
    aim(ob, (0.0, 0.0, 0.66))


area("Enamel key", (-2.2, -3.2, 3.6), 310, 3.0, (1.0, 0.90, 0.79))
area("Eye fill", (1.7, -3.0, 1.5), 160, 2.6, (0.77, 0.86, 1.0))
area("Cabinet blue rim", (0.7, 2.1, 2.6), 270, 2.5, (0.61, 0.83, 1.0))

camera_data = bpy.data.cameras.new("Reaction Camera")
camera_data.type = "ORTHO"
camera = bpy.data.objects.new("Reaction Camera", camera_data)
scene.collection.objects.link(camera)
scene.camera = camera


def transform_bone(name, rotate=None, move=None):
    bone = rig.pose.bones[name]
    matrix = bone.matrix.copy()
    if rotate is not None:
        matrix = rotate.to_matrix().to_4x4() @ matrix
        matrix.translation = bone.head
    if move is not None:
        matrix.translation += Vector(move)
    bone.matrix = matrix
    bpy.context.view_layer.update()


def direct(name, child, direction):
    bone = rig.pose.bones[name]
    tip = rig.pose.bones[child].head if child else bone.tail
    origin = bone.head.copy()
    current = (tip - origin).normalized()
    transform_bone(name, current.rotation_difference(Vector(direction).normalized()))


def reset():
    for bone in rig.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    for key in skin.data.shape_keys.key_blocks:
        if key.name != "Basis":
            key.value = 0.0
    bpy.context.view_layer.update()


def expression(tilt, turn, brow_left, brow_right, mouth, wide, pupils):
    transform_bone("head_x", Quaternion((0, 0, 1), math.radians(turn)) @
                  Quaternion((0, 1, 0), math.radians(tilt)))
    for side, raise_amount in (("l", brow_left), ("r", brow_right)):
        transform_bone("c_eyebrow_full_" + side, move=(0, 0, raise_amount))
    keys = skin.data.shape_keys.key_blocks
    keys["Mouth_Surprise"].value = mouth
    keys["Mouth_O"].value = 1.0 - mouth
    keys["Eyes_Scary"].value = wide
    keys["Pupil_Contract"].value = pupils


def lower_arm(side, direction, forearm):
    direct("arm_stretch_" + side, "forearm_stretch_" + side, direction)
    direct("forearm_stretch_" + side, "hand_" + side, forearm)


def stretch_digit(name, scale):
    bone = rig.pose.bones[name]
    matrix = bone.matrix.copy()
    matrix.col[1] *= scale
    bone.matrix = matrix
    bpy.context.view_layer.update()


def mirrored_pointer(side):
    sign = -1 if side == "l" else 1

    def point(name, child, direction):
        x, y, z = direction
        direct(name + "_" + side, child + "_" + side if child else None, (sign*x, y, z))

    point("arm_stretch", "forearm_stretch", (-0.73, -0.28, 0.62))
    point("forearm_stretch", "hand", (-0.82, -0.51, 0.23))
    point("hand", "index1_base", (-0.63, -0.33, -0.69))
    point("index1_base", "index1", (-0.67, 0.05, -0.74))
    point("index1", "index2", (-0.45, 0.35, -0.82))
    point("index2", "index3", (0.45, 0.48, -0.76))
    point("index3", None, (0.82, 0.12, 0.51))
    point("thumb1", "thumb2", (-0.97, -0.15, 0.18))
    point("thumb2", "thumb3", (-0.98, -0.15, 0.11))
    point("thumb3", None, (-0.98, -0.13, 0.08))
    for name in ("thumb1", "thumb2", "thumb3"):
        stretch_digit(name + "_" + side, 1.29)

    def joint(name):
        return rig.matrix_world @ rig.pose.bones[name + "_" + side].head

    shoulder = joint("arm_stretch") + Vector((sign*0.069, -0.045, 0.015))
    elbow = joint("forearm_stretch") + Vector((sign*0.008, -0.028, 0.015))
    wrist = joint("hand").lerp(joint("index1_base"), 0.44)

    limb = bpy.data.curves.new("Connected posed forearm", "CURVE")
    limb.dimensions = "3D"
    limb.resolution_u = 16
    limb.bevel_depth = 0.043
    limb.bevel_resolution = 6
    limb.use_fill_caps = True
    path = limb.splines.new("BEZIER")
    path.bezier_points.add(2)
    for point, center, radius in zip(path.bezier_points, (shoulder, elbow, wrist), (1.31, 0.92, 0.73)):
        point.co = center
        point.radius = radius
        point.handle_left_type = "AUTO"
        point.handle_right_type = "AUTO"
    connector = bpy.data.objects.new("Enamel silhouette over actual arm joints", limb)
    scene.collection.objects.link(connector)
    limb.materials.append(enamel)
    return connector


enamel = bpy.data.materials.new("Portrait enamel")
enamel.use_nodes = True
shader = enamel.node_tree.nodes.get("Principled BSDF")
shader.inputs["Base Color"].default_value = (0.73, 0.72, 0.70, 1.0)
shader.inputs["Roughness"].default_value = 0.56


def free_arm(side, upper_direction, lower_direction):
    lower_arm(side, upper_direction, lower_direction)
    sign = 1 if side == "l" else -1

    def joint(name):
        return rig.matrix_world @ rig.pose.bones[name + "_" + side].head

    shoulder = joint("arm_stretch") + Vector((-sign*0.076, -0.034, 0.015))
    elbow = joint("forearm_stretch") + Vector((-sign*0.006, -0.023, 0.002))
    wrist = joint("hand").lerp(joint("index1_base"), 0.45)
    limb = bpy.data.curves.new("Actual free arm silhouette", "CURVE")
    limb.dimensions = "3D"
    limb.resolution_u = 14
    limb.bevel_depth = 0.041
    limb.bevel_resolution = 6
    limb.use_fill_caps = True
    path = limb.splines.new("BEZIER")
    path.bezier_points.add(2)
    for point, center, radius in zip(path.bezier_points, (shoulder, elbow, wrist), (1.24, 0.93, 0.76)):
        point.co = center
        point.radius = radius
        point.handle_left_type = "AUTO"
        point.handle_right_type = "AUTO"
    connector = bpy.data.objects.new("Enamel over actual free arm joints", limb)
    scene.collection.objects.link(connector)
    limb.materials.append(enamel)
    return connector


def gaze(turn):
    for side in ("l", "r"):
        transform_bone("c_eye_" + side, Quaternion((0, 0, 1), turn))


def capture(name, target, offset, lens):
    camera_data.type = "PERSP"
    camera_data.lens = lens
    camera.location = Vector(target) + Vector(offset)
    aim(camera, target)
    scene.render.resolution_x = 960
    scene.render.resolution_y = 960
    scene.render.filepath = str(HERE / f"CaseRareScene{name}Raw.png")
    bpy.context.view_layer.update()
    bpy.ops.render.render(write_still=True)


reset()
transform_bone("root_x", Quaternion((0, 1, 0), -0.095) @ Quaternion((1, 0, 0), 0.065))
expression(-3.0, 4.0, 6.3, 4.8, 0.87, 0.06, 0.16)
gaze(0.035)
left_pointer = mirrored_pointer("l")
left_free = free_arm("r", (-0.31, -0.58, -0.77), (0.33, -0.74, -0.58))
capture("Left", (0.11, -0.06, 0.55), (0.21, -2.80, 0.18), 57)
left_pointer.hide_render = True
left_free.hide_render = True

reset()
transform_bone("root_x", Quaternion((0, 0, 1), -0.16) @ Quaternion((0, 1, 0), 0.07))
expression(7.0, -10.0, 4.4, 5.1, 0.72, 0.09, 0.11)
gaze(-0.14)
mirrored_pointer("r")
free_arm("l", (0.47, -0.44, -0.76), (-0.27, -0.78, -0.57))
capture("Right", (-0.13, -0.07, 0.54), (-0.52, -3.40, 0.31), 62)
