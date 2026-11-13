"""Bake the KatanaCutVariants review file to the existing UE character skeleton."""
import json
from pathlib import Path
import re
import sys

import bpy
from mathutils import Matrix, Vector


ROOT = Path(__file__).resolve().parents[2]
CLIPS = {
    "Heavy Carry": "Carry",
    "Diagonal From Right": "DiagonalRight",
    "Diagonal From Left": "DiagonalLeft",
    "Horizontal Right to Left": "Horizontal",
    "Overhead": "Overhead",
    "Charged Series": "Charged",
}
MORPHS = {
    "Mouth_Effort": "MCKatanaMouthEffort",
    "Mouth_Angry": "MCKatanaMouthAngry",
    "Eyes_Scary": "MCKatanaEyesScary",
}
FLIP_Y = Matrix.Diagonal((1, -1, 1, 1))


def cm(matrix):
    scaled = matrix.copy()
    scaled.translation *= 100
    return scaled


def select(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.hide_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def load_target(scene):
    with bpy.data.libraries.load(str(ROOT / "ArtSource/CharacterGameplay/TeethGameplay.blend"),
                                 link=False) as (available, loaded):
        loaded.objects = available.objects
    target = next(o for o in loaded.objects if o and o.type == "ARMATURE")
    scene.collection.objects.link(target)
    bpy.context.view_layer.update()
    target.animation_data_clear()
    world = target.matrix_world.copy()
    target.parent = None
    target.matrix_world = world
    select(target)
    bpy.ops.object.mode_set(mode="EDIT")
    for bone in target.data.edit_bones:
        bone.use_connect = False
    bpy.ops.object.mode_set(mode="OBJECT")
    for pose in target.pose.bones:
        pose.rotation_mode = "QUATERNION"
        pose.matrix_basis = Matrix.Identity(4)
    return target


def scene_prop(scene):
    return next(o for o in scene.objects if o.name.startswith("Katana | Two Hand Driver"))


def source_map(source, target):
    result = {}
    for bone in target.data.bones:
        base = re.sub(r"_([rlx])$", r".\1", bone.name)
        name = base if base in source.data.bones else "c_" + base
        if name not in source.data.bones:
            raise ValueError(f"Missing source bone {name} for {bone.name}")
        result[bone.name] = name
    return result


def arm_branches(target):
    branches = set()
    for side in ("l", "r"):
        shoulder = target.data.bones["shoulder_" + side]
        branches.update(bone.name for bone in (shoulder, *shoulder.children_recursive))
    return branches


def local_weapon(hand, weapon, previous):
    location = FLIP_Y @ (hand.inverted() @ weapon.translation).to_4d()
    rotation = (FLIP_Y @ (hand.to_quaternion().inverted() @ weapon.to_quaternion()).to_matrix().to_4x4()
                @ FLIP_Y).to_quaternion()
    if previous is not None:
        rotation.make_compatible(previous)
    hand_scale, mesh_scale = hand.to_scale(), weapon.to_scale()
    scale = [mesh_scale[i] / hand_scale[i] for i in range(3)]
    return rotation, [1.0, *location[:3], rotation.x, rotation.y, rotation.z, rotation.w, *scale]


def bake(scene, target, name, output, prop_samples, sample_multiplier=1):
    bpy.context.window.scene = scene
    source = next(o for o in scene.objects if o.type == "ARMATURE" and o != target)
    skin = next(o for o in scene.objects if o.name.startswith("SM_Character") and o.type == "MESH")
    sword = scene_prop(scene)
    mapping = source_map(source, target)
    arms = arm_branches(target)
    fingers = {bone.name for side in ("l", "r")
               for bone in target.data.bones["hand_" + side].children_recursive}
    scene.frame_set(scene.frame_start)
    graph = bpy.context.evaluated_depsgraph_get()
    first_root = cm(source.evaluated_get(graph).pose.bones["root.x"].matrix).translation
    rest_root = cm(source.data.bones["root.x"].matrix_local).translation
    offset = first_root - rest_root
    target.animation_data_create()
    action = bpy.data.actions.new(name)
    target.animation_data.action = action
    previous = {}
    weapon_previous = None
    peak_error = 0.0
    fields = ("MCWeaponAuthored", "MCWeaponX", "MCWeaponY", "MCWeaponZ", "MCWeaponQX",
              "MCWeaponQY", "MCWeaponQZ", "MCWeaponQW", "MCWeaponSX", "MCWeaponSY", "MCWeaponSZ")
    curves = {field: [] for field in (*fields, *MORPHS.values())}
    reference = []
    sample_count = (scene.frame_end - scene.frame_start) * sample_multiplier + 1
    for sample in range(sample_count):
        frame = scene.frame_start + sample / sample_multiplier
        scene.frame_set(int(frame), subframe=frame % 1.)
        graph = bpy.context.evaluated_depsgraph_get()
        evaluated = source.evaluated_get(graph)
        root_offset = offset.copy()
        if name == "A_Katana_Carry":
            # CharacterMovement owns forward translation during carry.
            root_offset.y += cm(evaluated.pose.bones["root.x"].matrix).translation.y - first_root.y
        actual = {}
        for bone in target.data.bones:
            source_name = mapping[bone.name]
            rest = cm(source.data.bones[source_name].matrix_local)
            pose = cm(evaluated.pose.bones[source_name].matrix)
            desired = pose @ rest.inverted() @ bone.matrix_local
            desired.translation -= root_offset
            parent = actual[bone.parent.name] if bone.parent else Matrix.Identity(4)
            parent_rest = bone.parent.matrix_local if bone.parent else Matrix.Identity(4)
            if bone.name in arms:
                # Blender's stretch controls produce nonuniform scale and shear
                # through several nested bones. Unreal composes TRS only, which
                # would multiply the scale into visibly distorted gloves.
                # Arm length comes from the sampled bone origins. Finger length
                # comes from the gameplay bind offsets while authored curls turn.
                location = desired.translation
                if bone.name in fingers:
                    location = parent @ (parent_rest.inverted() @ bone.matrix_local.translation)
                desired = Matrix.LocRotScale(location, desired.to_quaternion(), (1, 1, 1))
            target_pose = target.pose.bones[bone.name]
            target_pose.matrix_basis = bone.convert_local_to_pose(desired, bone.matrix_local,
                parent_matrix=parent, parent_matrix_local=parent_rest, invert=True)
            if bone.name in previous:
                target_pose.rotation_quaternion.make_compatible(previous[bone.name])
            previous[bone.name] = target_pose.rotation_quaternion.copy()
            actual[bone.name] = bone.convert_local_to_pose(target_pose.matrix_basis, bone.matrix_local,
                parent_matrix=parent, parent_matrix_local=parent_rest)
            peak_error = max(peak_error, (actual[bone.name].translation - desired.translation).length)
            for channel in ("location", "rotation_quaternion", "scale"):
                target_pose.keyframe_insert(channel, frame=frame, group=bone.name)

        weapon = cm(sword.evaluated_get(graph).matrix_world)
        weapon.translation -= root_offset
        weapon_previous, values = local_weapon(actual["hand_r"], weapon, weapon_previous)
        for field, value in zip(fields, values):
            curves[field].append(value)
        shape_keys = skin.data.shape_keys.evaluated_get(graph)
        for source_name, field in MORPHS.items():
            curves[field].append(shape_keys.key_blocks[source_name].value)
        reference.append({"prop": [list((FLIP_Y @ weapon @ Vector((*point, 1)))[:3])
                                   for point in prop_samples],
                          "right_hand": [list(row) for row in FLIP_Y @ actual["hand_r"] @ FLIP_Y],
                          "left_hand": [list(row) for row in FLIP_Y @ actual["hand_l"] @ FLIP_Y]})

    if peak_error > .01:
        raise ValueError(f"{name}: {peak_error:.4f} cm baked-bone endpoint error")
    select(target)
    path = output / (name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={"ARMATURE"},
        add_leaf_bones=False, use_armature_deform_only=False, bake_anim=True,
        bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False, bake_anim_simplify_factor=0,
        bake_anim_step=1. / sample_multiplier)
    fps = scene.render.fps / scene.render.fps_base * sample_multiplier
    action.use_fake_user = True
    print(f"BAKED {name}: {sample_count} samples, {peak_error:.6f} cm", flush=True)
    return {"asset": name, "frames": sample_count, "fps": fps,
            "max_bone_error_cm": peak_error, "curves": curves, "bone_count": len(mapping),
            "samples": reference}


def material_values(material):
    shader = next(n for n in material.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    return [tuple(shader.inputs["Base Color"].default_value),
            shader.inputs["Metallic"].default_value, shader.inputs["Roughness"].default_value]


def bake_prop(scene, output):
    bpy.context.window.scene = scene
    scene.frame_set(scene.frame_start)
    graph = bpy.context.evaluated_depsgraph_get()
    pivot = scene_prop(scene).evaluated_get(graph).matrix_world.inverted()
    vertices, faces, surfaces, blade = [], [], [], []
    for obj in scene.objects:
        if not obj.name.startswith("GEO-Katana "):
            continue
        evaluated = obj.evaluated_get(graph)
        transform = pivot @ evaluated.matrix_world
        mesh = evaluated.to_mesh()
        try:
            materials = [material_values(m) for m in mesh.materials]
            start = len(vertices)
            points = [(transform @ v.co) * 100.0 for v in mesh.vertices]
            vertices.extend(points)
            if re.match(r"GEO-Katana Blade(?:\.\d+)?$", obj.name):
                blade.extend(points)
            for face in mesh.polygons:
                faces.append([start + index for index in face.vertices])
                surfaces.append((*materials[face.material_index], face.use_smooth))
        finally:
            evaluated.to_mesh_clear()
    if not blade:
        raise ValueError("Missing blade geometry")

    mesh = bpy.data.meshes.new("SM_Katana")
    mesh.from_pydata(vertices, (), faces)
    mesh.update()
    color = mesh.color_attributes.new(name="KatanaColor", type="FLOAT_COLOR", domain="CORNER")
    mesh.color_attributes.active_color = color
    geometry_uv = mesh.uv_layers.new(name="KatanaGeometry")
    surface_uv = mesh.uv_layers.new(name="KatanaSurface")
    mesh.uv_layers.active = geometry_uv
    mesh.materials.append(bpy.data.materials.new("M_KatanaSurface"))
    for polygon, (rgb, metallic, roughness, smooth) in zip(mesh.polygons, surfaces):
        polygon.use_smooth = smooth
        # Project onto the plane perpendicular to the dominant face normal.
        # This UV layer supplies a nondegenerate tangent basis for the FBX import.
        dominant = max(range(3), key=lambda axis: abs(polygon.normal[axis]))
        axes = tuple(axis for axis in range(3) if axis != dominant)
        for index in polygon.loop_indices:
            color.data[index].color = rgb
            vertex = mesh.vertices[mesh.loops[index].vertex_index].co
            geometry_uv.data[index].uv = tuple(vertex[axis] / 130.3 + .5 for axis in axes)
            # Unreal's FBX importer flips V.
            surface_uv.data[index].uv = (metallic, 1.0 - roughness)

    export_scene = bpy.data.scenes.new("KatanaStaticExport")
    export_scene.unit_settings.system = "METRIC"
    export_scene.unit_settings.scale_length = .01
    prop = bpy.data.objects.new("SM_Katana", mesh)
    export_scene.collection.objects.link(prop)
    bpy.context.window.scene = export_scene
    select(prop)
    bpy.ops.export_scene.fbx(filepath=str(output / "SM_Katana.fbx"), use_selection=True,
        object_types={"MESH"}, bake_anim=False, use_mesh_modifiers=True, mesh_smooth_type="FACE",
        # The legacy Unreal importer interprets FbxColor bytes as sRGB.
        add_leaf_bones=False, colors_type="SRGB")
    mesh.calc_loop_triangles()
    bounds = [[min(v[i] for v in vertices) for i in range(3)], [max(v[i] for v in vertices) for i in range(3)]]
    tip = max(blade, key=lambda v: v.z)
    base = Vector((0, 0, min(v.z for v in blade)))
    return {"asset": "SM_Katana", "bounds_blender_cm": bounds, "tip_ue_cm": [tip.x, -tip.y, tip.z],
            "base_ue_cm": list(base), "triangles": len(mesh.loop_triangles), "material_slots": 1}


def main():
    if bpy.app.version < (5, 2):
        raise RuntimeError("The source review file needs Blender 5.2")
    output = Path(sys.argv[sys.argv.index("--") + 1]).resolve()
    if output.drive.upper() != "E:":
        raise ValueError("Export path must be on E:")
    output.mkdir(parents=True, exist_ok=True)
    carry = bpy.data.scenes["Katana | Heavy Carry"]
    bpy.context.window.scene = carry
    target = load_target(carry)
    result = {"prop": bake_prop(carry, output), "clips": []}
    # Calibration markers follow the actual prop geometry, including its length.
    tip = result["prop"]["tip_ue_cm"]
    prop_samples = ((0, 0, 0), result["prop"]["base_ue_cm"], (tip[0], -tip[1], tip[2]))
    for label, name in CLIPS.items():
        scene = bpy.data.scenes["Katana | " + label]
        if target.name not in scene.objects:
            scene.collection.objects.link(target)
        # Fast, wide two-hand arcs need dense FK samples. At 30 FPS the
        # independently interpolated wrist and twist chains lose the second
        # grip between keys even though each authored IK pose is exact.
        multiplier = 8 if scene.get("wide_katana_version") else 1
        result["clips"].append(bake(scene, target, "A_Katana_" + name, output, prop_samples, multiplier))
    (output / "KatanaBake.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
