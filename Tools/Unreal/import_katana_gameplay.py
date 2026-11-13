"""Import the six Katana clips, render mesh, and UI icon without editing shared profiles."""
import json
import os
from pathlib import Path

import unreal as u


LIB = u.EditorAssetLibrary
ASSETS = u.AssetToolsHelpers.get_asset_tools()
ANIM_FOLDER = "/Game/Gameplay/CharacterCurrent/Animations/Katana"
TOOL_FOLDER = "/Game/Gameplay/CharacterCurrent/Tools"
UI_FOLDER = "/Game/Gameplay/UI/V2/Art/Tools"


def imported(source, folder, name, options=None):
    task = u.AssetImportTask()
    task.filename = str(source)
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    if options:
        task.factory = u.FbxFactory()
        task.options = options
    ASSETS.import_asset_tasks([task])
    path = f"{folder}/{name}"
    result = LIB.load_asset(path)
    if result is None or not task.imported_object_paths:
        raise RuntimeError(f"Import of {source} into {path} did not create an asset")
    return result


def save(asset):
    if not LIB.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")


def animation_curves(clip, entry):
    times = [i / entry["fps"] for i in range(entry["frames"])]
    assert abs(clip.get_play_length() - times[-1]) < .005, (clip.get_name(), clip.get_play_length(), times[-1])
    for curve, values in entry["curves"].items():
        curve_type = u.RawCurveTrackTypes.RCT_FLOAT
        if u.AnimationLibrary.does_curve_exist(clip, curve, curve_type):
            u.AnimationLibrary.remove_curve(clip, curve, False)
        u.AnimationLibrary.add_curve(clip, curve, curve_type, False)
        u.AnimationLibrary.add_float_curve_keys(clip, curve, times, values)
        assert u.AnimationLibrary.does_curve_exist(clip, curve, curve_type), (clip.get_name(), curve)


def calibrated_curves(clip, hero, entry):
    options = u.AnimPoseEvaluationOptions()
    options.set_editor_properties({
        "optional_skeletal_mesh": hero,
        "evaluation_type": u.AnimDataEvalType.RAW,
        "should_retarget": False,
    })
    previous = None
    left_errors = []
    for frame, sample in enumerate(entry["samples"]):
        pose = u.AnimPoseExtensions.get_anim_pose_at_time(clip, frame / entry["fps"], options)
        assert u.AnimPoseExtensions.is_valid(pose)
        hand = pose.get_bone_pose("hand_r", u.AnimPoseSpaces.WORLD)
        origin, base, tip = [u.Vector(*coords) for coords in sample["prop"]]
        # The sword's local +Z passes through the blade base. Remove the +Z
        # component of the tip vector to find -X, since the tip curves left.
        z_axis = u.MathLibrary.normal(base - origin)
        tip_axis = tip - origin
        x_axis = u.MathLibrary.normal(z_axis * u.MathLibrary.dot_vector_vector(z_axis, tip_axis) - tip_axis)
        expected = u.Transform(location=origin, rotation=u.MathLibrary.make_rot_from_zx(z_axis, x_axis))
        relative = u.MathLibrary.make_relative_transform(expected, hand)
        quat = relative.rotation.to_tuple()
        if previous and sum(a * b for a, b in zip(previous, quat)) < 0:
            quat = tuple(-v for v in quat)
        previous = quat
        values = (*relative.translation.to_tuple(), *quat, *relative.scale3d.to_tuple())
        for field, value in zip(("X", "Y", "Z", "QX", "QY", "QZ", "QW", "SX", "SY", "SZ"), values):
            entry["curves"]["MCWeapon" + field][frame] = value
        # Skin/IK validation owns grip surfaces, but the left wrist origin must
        # not move when converting the baked hand poses through UE's FBX layer.
        left = pose.get_bone_pose("hand_l", u.AnimPoseSpaces.WORLD)
        expected_left = u.Vector(*(row[3] for row in sample["left_hand"][:3]))
        left_errors.append((left.translation - expected_left).length())
    return max(left_errors)


def animation(source, hero, skeleton, entry):
    settings = u.FbxImportUI()
    settings.automated_import_should_detect_type = False
    settings.import_mesh = False
    settings.import_as_skeletal = True
    settings.mesh_type_to_import = u.FBXImportType.FBXIT_ANIMATION
    settings.import_animations = True
    settings.skeleton = skeleton
    settings.import_materials = False
    settings.import_textures = False
    settings.create_physics_asset = False
    data = settings.anim_sequence_import_data
    data.set_editor_properties({
        "animation_length": u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME,
        "import_bone_tracks": True,
        "use_default_sample_rate": False,
        "custom_sample_rate": int(entry["fps"]),
    })
    name = entry["asset"]
    clip = imported(source / (name + ".fbx"), ANIM_FOLDER, name, settings)
    return save_animation(clip, hero, skeleton, entry)


def save_animation(clip, hero, skeleton, entry):
    assert isinstance(clip, u.AnimSequence) and clip.get_editor_property("skeleton") == skeleton
    error = calibrated_curves(clip, hero, entry)
    animation_curves(clip, entry)
    clip.set_editor_property("enable_root_motion", False)
    save(clip)
    return {"asset": clip.get_path_name(), "seconds": clip.get_play_length(),
            "curves": list(entry["curves"]), "bones_in_bake": entry["bone_count"],
            "max_left_wrist_origin_error_cm": error}


def surface_material():
    folder = TOOL_FOLDER + "/Materials"
    name = "M_KatanaSurface"
    material = LIB.load_asset(f"{folder}/{name}") if LIB.does_asset_exist(f"{folder}/{name}") else None
    if material is None:
        material = ASSETS.create_asset(name, folder, u.Material, u.MaterialFactoryNew())
    editor = u.MaterialEditingLibrary
    editor.delete_all_material_expressions(material)
    vertex = editor.create_material_expression(material, u.MaterialExpressionVertexColor, -420, -110)
    uv = editor.create_material_expression(material, u.MaterialExpressionTextureCoordinate, -440, 100)
    uv.set_editor_property("coordinate_index", 1)
    assert editor.connect_material_property(vertex, "", u.MaterialProperty.MP_BASE_COLOR)
    for y, channel, prop in ((60, "r", u.MaterialProperty.MP_METALLIC),
                             (220, "g", u.MaterialProperty.MP_ROUGHNESS)):
        mask = editor.create_material_expression(material, u.MaterialExpressionComponentMask, -215, y)
        mask.set_editor_properties({"r": channel == "r", "g": channel == "g", "b": False, "a": False})
        assert editor.connect_material_expressions(uv, "", mask, "")
        assert editor.connect_material_property(mask, "", prop)
    editor.recompile_material(material)
    save(material)
    return material


def prop(source, entry):
    settings = u.FbxImportUI()
    settings.automated_import_should_detect_type = False
    settings.import_mesh = True
    settings.import_as_skeletal = False
    settings.mesh_type_to_import = u.FBXImportType.FBXIT_STATIC_MESH
    settings.import_animations = False
    settings.import_materials = False
    settings.import_textures = False
    settings.static_mesh_import_data.set_editor_properties({
        "combine_meshes": True,
        "auto_generate_collision": False,
        "generate_lightmap_u_vs": False,
        "vertex_color_import_option": u.VertexColorImportOption.REPLACE,
        "normal_import_method": u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
    })
    name = entry["asset"]
    mesh = imported(source / (name + ".fbx"), TOOL_FOLDER, name, settings)
    assert isinstance(mesh, u.StaticMesh)
    assert len(mesh.get_editor_property("static_materials")) == 1
    mesh_editor = u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
    if mesh_editor is not None:
        assert mesh_editor.has_vertex_colors(mesh)
    bounds = mesh.get_bounding_box()
    lo, hi = bounds.get_editor_property("min"), bounds.get_editor_property("max")
    actual = [[p.x, p.y, p.z] for p in (lo, hi)]
    expected = entry["bounds_blender_cm"]
    for found, ideal in zip(actual, expected):
        assert max(abs(a - b) for a, b in zip(found, ideal)) < .05, (actual, expected)
    material = surface_material()
    mesh.set_material(0, material)
    assert mesh.get_num_triangles(0) == entry["triangles"]
    save(mesh)
    return {"asset": mesh.get_path_name(), "material": material.get_path_name(), "bounds_cm": actual,
            "tip_cm": entry["tip_ue_cm"], "base_cm": entry["base_ue_cm"],
            "triangles": entry["triangles"], "material_slots": 1}


def icon(project):
    name = "T_UI_PSD_Katana"
    texture = imported(project / "ArtSource/UI/V2/Icons" / (name + ".png"), UI_FOLDER, name)
    assert isinstance(texture, u.Texture2D)
    texture.set_editor_properties({
        "compression_settings": u.TextureCompressionSettings.TC_EDITOR_ICON,
        "lod_group": u.TextureGroup.TEXTUREGROUP_UI,
        "mip_gen_settings": u.TextureMipGenSettings.TMGS_NO_MIPMAPS,
        "compression_no_alpha": False,
        "never_stream": True,
        "srgb": True,
    })
    save(texture)
    return texture.get_path_name()


def main():
    source = Path(os.environ["MC_KATANA_FBX_DIR"]).resolve()
    if source.drive.upper() != "E:":
        raise ValueError("Katana import files must be on E:")
    project = Path(u.Paths.project_dir()).resolve()
    data = json.loads((source / "KatanaBake.json").read_text(encoding="utf-8"))
    u.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    hero = LIB.load_asset("/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay")
    skeleton = hero.get_editor_property("skeleton")
    refresh_mesh_only = os.environ.get("MC_KATANA_REFRESH_MESH_ONLY") == "1"
    refresh_curves_only = os.environ.get("MC_KATANA_REFRESH_CURVES_ONLY") == "1"
    refresh_animations_only = os.environ.get("MC_KATANA_REFRESH_ANIMATIONS_ONLY") == "1"
    previous_report = source / "KatanaImport.json"
    refresh = refresh_mesh_only or refresh_curves_only or refresh_animations_only
    report = json.loads(previous_report.read_text(encoding="utf-8")) if refresh else {}
    report["skeleton"] = skeleton.get_path_name()
    if refresh_curves_only:
        report["animations"] = [save_animation(LIB.load_asset(ANIM_FOLDER + "/" + entry["asset"]),
                                                hero, skeleton, entry) for entry in data["clips"]]
    elif refresh_animations_only:
        report["animations"] = [animation(source, hero, skeleton, entry) for entry in data["clips"]]
    elif refresh_mesh_only:
        report["mesh"] = prop(source, data["prop"])
    else:
        report["mesh"] = prop(source, data["prop"])
        report["icon"] = icon(project)
        report["animations"] = [animation(source, hero, skeleton, entry) for entry in data["clips"]]
    previous_report.write_text(json.dumps(report, indent=2), encoding="utf-8")
    u.log("MC_KATANA_IMPORT_PASS")


if __name__ == "__main__":
    main()
