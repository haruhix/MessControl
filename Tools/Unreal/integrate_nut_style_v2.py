"""Import and connect 19 Nut Style v2 clips, preserving existing art and tuning.

Run after the native boss animation slots are built and the FBX export completes:
    py "E:/DEVGAME/MessControl/Tools/Unreal/integrate_nut_style_v2.py"

An idle Editor or PythonScript commandlet is supported. Only the new MCStyleV2
animation folders and DA_NutRain are saved. Unrelated unsaved packages are kept.
No maps, actors, skeletons, meshes, materials or legacy animations are authored.
APIs were checked against the installed UE 5.8 import and AnimPose headers.
"""
import hashlib
import json
import math
import os
from pathlib import Path

import unreal as u


ROOT = Path(u.Paths.project_dir()).resolve()
EXPORT_ROOT = ROOT / "ArtSource/NutAnimationStyle/Exports"
PROFILE = "/Game/Gameplay/CoreLoop/DA_NutRain"
OWNER_TAG = "MCNutStyleV2Integration"
OWNER_VALUE = "integrate_nut_style_v2.py:1"
ROLE_SPECS = (
    {"role": "tank", "source": "Tank", "folder": "NutTank", "mesh": "SK_NutTank",
     "prefix": "A_NutTank_", "clips": (
         ("idle", "Idle"), ("walk", "Walk"), ("walk_left", "WalkLeft"),
         ("walk_right", "WalkRight"), ("melee", "Melee"), ("jump", "Jump"),
         ("transform", "Transform"), ("charge_tell", "ChargeTell"),
         ("charge_loop", "ChargeLoop"), ("charge_recovery", "ChargeRecovery"))},
    {"role": "mage", "source": "Wizard", "folder": "NutWizard", "mesh": "SK_NutWizard",
     "prefix": "A_NutWizard_", "clips": (
         ("idle", "Idle"), ("walk", "Walk"), ("cast", "Cast"),
         ("heavy_cast", "HeavyCast"), ("summon", "Summon"), ("rain", "Rain"),
         ("hit", "Hit"), ("death", "Death"), ("melee", "Melee"))},
)
BINDING_FIELDS = tuple(spec["role"] + "_" + field + "_animation"
                       for spec in ROLE_SPECS for field, _ in spec["clips"])
LOOPS = {"Idle", "Walk", "WalkLeft", "WalkRight", "ChargeLoop"}
CONTACT_CLIPS = {
    "tank": {"Idle", "Melee"},
    "mage": {"Idle", "Cast", "Summon", "Rain", "Melee"},
}
LIMB_PAIRS = tuple((upper + "_" + side, lower + "_" + side)
                   for side in ("l", "r") for upper, lower in (
                       ("arm_stretch", "forearm_stretch"), ("forearm_stretch", "hand"),
                       ("thigh_stretch", "leg_stretch"), ("leg_stretch", "foot")))
lib = u.EditorAssetLibrary


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def package_path(obj):
    return obj.get_path_name().split(".")[0]


def dirty_packages():
    packages = (list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
                list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))
    return {package.get_name() for package in packages}


def owned(obj, destination):
    return (package_path(obj).startswith(destination + "/") and
            lib.get_metadata_tag(obj, OWNER_TAG) == OWNER_VALUE)


def digest(filename):
    return hashlib.sha256(filename.read_bytes()).hexdigest()


def protected_disk_hashes():
    """Every original boss/artist package, excluding the new version folders."""
    result = {}
    content = Path(u.Paths.project_content_dir()).resolve()
    for folder in ("Gameplay/CoreLoop/NutTank", "Gameplay/CoreLoop/NutWizard", "FromBlender8"):
        directory = content / folder
        for filename in directory.rglob("*"):
            if (filename.is_file() and filename.suffix.lower() in (".uasset", ".uexp", ".ubulk")
                    and "MCStyleV2" not in filename.parts):
                result[str(filename)] = digest(filename)
    return result


def vec(v):
    return [float(v.x), float(v.y), float(v.z)]


def transform_values(transform):
    rotation = transform.get_editor_property("rotation")
    return (vec(transform.get_editor_property("translation")) +
            [float(rotation.x), float(rotation.y), float(rotation.z), float(rotation.w)] +
            vec(transform.get_editor_property("scale3d")))


def transform_distance(a, b):
    av, bv = transform_values(a), transform_values(b)
    translation = math.dist(av[:3], bv[:3])
    norm = math.sqrt(sum(value * value for value in av[3:7]) *
                     sum(value * value for value in bv[3:7]))
    dot = min(1.0, abs(sum(x * y for x, y in zip(av[3:7], bv[3:7]))) / max(norm, 1.e-12))
    rotation = math.degrees(2 * math.acos(dot))
    scale = max(abs(x - y) for x, y in zip(av[7:], bv[7:]))
    return {"translation_cm": translation, "rotation_degrees": rotation, "scale": scale}


def reference_snapshot(skeleton):
    pose = u.AnimPoseExtensions.get_reference_pose(skeleton)
    require(u.AnimPoseExtensions.is_valid(pose), "Cannot evaluate the existing skeleton reference pose")
    names = [str(name) for name in u.AnimPoseExtensions.get_bone_names(pose)]
    return {"bones": names,
            "local": {name: transform_values(u.AnimPoseExtensions.get_bone_pose(
                pose, name, u.AnimPoseSpaces.LOCAL)) for name in names},
            "world": {name: transform_values(u.AnimPoseExtensions.get_bone_pose(
                pose, name, u.AnimPoseSpaces.WORLD)) for name in names}}


def without_bindings(boss):
    normalized = boss.copy()
    for key in BINDING_FIELDS:
        normalized.set_editor_property(key, None)
    return normalized.export_text()


def read_export(spec):
    source = (EXPORT_ROOT / spec["source"]).resolve()
    manifest = json.loads((source / "ExportReport.json").read_text(encoding="utf8"))
    require(manifest.get("status") == "complete" and manifest.get("source_preserved"),
            "Export is incomplete or changed its source: " + str(source))
    require(manifest.get("hierarchy_consistent") and manifest.get("root_animation_constant"),
            "Export hierarchy/root invariants failed: " + str(source))
    require(manifest.get("reference_unchanged") or manifest.get("derived_reference_pose_unchanged"),
            "Exporter did not verify preservation of the reference pose: " + str(source))
    records = [{**clip, "name": Path(clip["file"]).stem} for clip in manifest["clips"]]
    expected = {spec["prefix"] + suffix for _, suffix in spec["clips"]}
    require(len(records) == len(expected) and {clip["name"] for clip in records} == expected,
            "Export must contain exactly the requested style clips: " + spec["role"])
    files = {record["file"]: record for record in manifest["files"]}
    legacy = json.loads((ROOT / "ArtSource" / spec["folder"] / "ExportReport.json").read_text(encoding="utf8"))
    legacy_mesh = legacy.get("mesh_file", "SK_NutTank.fbx")
    legacy_reference = next(record for record in legacy["files"] if record["file"] == legacy_mesh)
    expected_hierarchy = legacy_reference["fbx"]["hierarchy_sha256"]
    for record in records:
        filename = (source / record["file"]).resolve()
        require(filename.is_relative_to(source) and filename.suffix.lower() == ".fbx" and filename.is_file(),
                "Missing animation-only FBX: " + str(filename))
        require(record["file"] in files and digest(filename) == files[record["file"]]["sha256"],
                "FBX changed after the checked export: " + str(filename))
        require(files[record["file"]]["fbx"]["hierarchy_sha256"] == expected_hierarchy,
                "Style FBX does not match the existing imported skeleton hierarchy: " + str(filename))
        require(float(record["duration_seconds"]) > 0, "Animation duration must be positive")
    return source, manifest, {record["name"]: record for record in records}


def animation_options(skeleton, legacy_clip, fps, name):
    options = u.FbxImportUI()
    options.set_editor_property("automated_import_should_detect_type", False)
    options.set_editor_property("mesh_type_to_import", u.FBXImportType.FBXIT_ANIMATION)
    options.set_editor_property("original_import_type", u.FBXImportType.FBXIT_ANIMATION)
    options.set_editor_property("import_mesh", False)
    options.set_editor_property("import_animations", True)
    options.set_editor_property("skeleton", skeleton)
    options.set_editor_property("override_animation_name", name)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("create_physics_asset", False)
    mesh_data = options.get_editor_property("skeletal_mesh_import_data")
    mesh_data.set_editor_property("update_skeleton_reference_pose", False)
    mesh_data.set_editor_property("use_t0_as_ref_pose", False)
    data = options.get_editor_property("anim_sequence_import_data")
    # Match the already imported mesh's proven axis/unit policy exactly.
    legacy_data = legacy_clip.get_editor_property("asset_import_data")
    require(isinstance(legacy_data, u.FbxAnimSequenceImportData), "Legacy clip is missing FBX import settings")
    for key in ("import_translation", "import_rotation", "import_uniform_scale",
                "convert_scene", "convert_scene_unit", "force_front_x_axis"):
        data.set_editor_property(key, legacy_data.get_editor_property(key))
    data.set_editor_property("animation_length", u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    data.set_editor_property("use_default_sample_rate", False)
    data.set_editor_property("custom_sample_rate", int(round(fps)))
    data.set_editor_property("snap_to_closest_frame_boundary", False)
    data.set_editor_property("import_bone_tracks", True)
    data.set_editor_property("preserve_local_transform", True)
    data.set_editor_property("import_custom_attribute", False)
    data.set_editor_property("add_curve_metadata_to_skeleton", False)
    return options


def import_clip(spec, context, suffix):
    name = spec["prefix"] + suffix
    record = context["records"][name]
    task = u.AssetImportTask()
    task.filename = str(context["source"] / record["file"])
    task.destination_path = context["destination"]
    task.destination_name = name
    task.automated = True
    task.save = False
    task.replace_existing = True
    task.replace_existing_settings = True
    task.factory = u.FbxFactory()
    task.options = animation_options(context["skeleton"], context["legacy_idle"], context["fps"], name)
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = list(task.imported_object_paths)
    require(len(paths) == 1, "Animation-only FBX must produce one AnimSequence: " + task.filename + ": " + str(paths))
    sequence = lib.load_asset(paths[0])
    require(isinstance(sequence, u.AnimSequence), "Animation-only import produced another asset type: " + str(paths))
    target = context["destination"] + "/" + name
    require(package_path(sequence).startswith(context["destination"] + "/"), "Importer escaped its version folder")
    if package_path(sequence) != target:
        require(not lib.does_asset_exist(target), "Cannot normalize importer output onto an existing asset: " + target)
        require(lib.rename_asset(sequence.get_path_name(), target), "Could not normalize imported clip name: " + target)
        sequence = lib.load_asset(target)
    lib.set_metadata_tag(sequence, OWNER_TAG, OWNER_VALUE)
    lib.set_metadata_tag(sequence, "MCNutStyleV2Source", task.filename)
    lib.set_metadata_tag(sequence, "MCNutStyleV2SourceSHA256", digest(Path(task.filename)))
    require(sequence.get_editor_property("skeleton") == context["skeleton"], "Imported clip changed skeleton")
    length = float(sequence.get_play_length())
    require(abs(length - float(record["duration_seconds"])) < max(.004, .1 / context["fps"]),
            "Imported duration differs from the exported range: " + name + " = " + str(length))
    sequence.set_editor_property("enable_root_motion", False)
    sequence.set_editor_property("force_root_lock", True)
    sequence.set_editor_property("root_motion_root_lock", u.RootMotionRootLock.REF_POSE)
    return sequence, {**record, "asset": sequence.get_path_name(), "imported_duration_seconds": length}


def pose_at(context, sequence, time):
    pose = u.AnimPoseExtensions.get_anim_pose_at_time(sequence, time, context["pose_options"])
    require(u.AnimPoseExtensions.is_valid(pose), "Invalid evaluated animation pose: " + sequence.get_path_name())
    return pose


def validate_clip(context, sequence, suffix):
    length = float(sequence.get_play_length())
    count = max(1, int(round(length * context["fps"])))
    names = context["reference"]["bones"]
    require(set(str(name) for name in u.AnimationLibrary.get_animation_track_names(sequence)).issubset(names),
            "Clip has tracks outside the existing skeleton")
    start = pose_at(context, sequence, 0)
    end = pose_at(context, sequence, length)
    root_start = u.AnimPoseExtensions.get_bone_pose(start, names[0], u.AnimPoseSpaces.LOCAL)
    feet = {name: u.AnimPoseExtensions.get_bone_pose(start, name, u.AnimPoseSpaces.WORLD)
            for name in ("foot_l", "foot_r")}
    max_root = {"translation_cm": 0., "rotation_degrees": 0., "scale": 0.}
    max_foot_drift = 0.
    planted_feet = suffix in CONTACT_CLIPS[context["role"]]
    limb_ratios = {upper + "->" + lower: [float("inf"), 0.] for upper, lower in LIMB_PAIRS}
    for frame in range(count + 1):
        pose = start if frame == 0 else end if frame == count else pose_at(context, sequence, length * frame / count)
        for name in names:
            values = transform_values(u.AnimPoseExtensions.get_bone_pose(pose, name, u.AnimPoseSpaces.LOCAL))
            require(all(math.isfinite(value) for value in values), "Non-finite animation bone: " + suffix + "/" + name)
        root = u.AnimPoseExtensions.get_bone_pose(pose, names[0], u.AnimPoseSpaces.LOCAL)
        for key, value in transform_distance(root_start, root).items():
            max_root[key] = max(max_root[key], value)
        for upper, lower in LIMB_PAIRS:
            upper_position = transform_values(u.AnimPoseExtensions.get_bone_pose(
                pose, upper, u.AnimPoseSpaces.WORLD))[:3]
            lower_position = transform_values(u.AnimPoseExtensions.get_bone_pose(
                pose, lower, u.AnimPoseSpaces.WORLD))[:3]
            reference_length = math.dist(context["reference"]["world"][upper][:3],
                                         context["reference"]["world"][lower][:3])
            require(reference_length > 0.001, "Reference limb has zero length: " + upper)
            ratio = math.dist(upper_position, lower_position) / reference_length
            values = limb_ratios[upper + "->" + lower]
            values[0], values[1] = min(values[0], ratio), max(values[1], ratio)
        if planted_feet:
            for name, initial in feet.items():
                current = u.AnimPoseExtensions.get_bone_pose(pose, name, u.AnimPoseSpaces.WORLD)
                max_foot_drift = max(max_foot_drift, transform_distance(initial, current)["translation_cm"])
    require(max_root["translation_cm"] < .05 and max_root["rotation_degrees"] < .05 and max_root["scale"] < .0001,
            "Exported root moves: " + suffix + " " + str(max_root))
    require(not planted_feet or max_foot_drift < .5,
            "Planted feet drift after import: " + suffix + " " + str(max_foot_drift) + " cm")
    require(all(.945 < values[0] and values[1] < 1.055 for values in limb_ratios.values()),
            "Imported limb length changes by more than 5.5%: " + suffix + " " + str(limb_ratios))
    seam = {"translation_cm": 0., "rotation_degrees": 0., "scale": 0.}
    if suffix in LOOPS:
        for name in names:
            first = u.AnimPoseExtensions.get_bone_pose(start, name, u.AnimPoseSpaces.LOCAL)
            last = u.AnimPoseExtensions.get_bone_pose(end, name, u.AnimPoseSpaces.LOCAL)
            for key, value in transform_distance(first, last).items():
                seam[key] = max(seam[key], value)
        require(seam["translation_cm"] < .05 and seam["rotation_degrees"] < .1 and seam["scale"] < .0001,
                "Imported animation loop does not close: " + suffix + " " + str(seam))
    return {"sampled_frames": count + 1, "finite_bones": True, "root_drift": max_root,
            "foot_drift_cm": max_foot_drift if planted_feet else None,
            "limb_length_ratio_ranges": limb_ratios,
            "loop_seam": seam if suffix in LOOPS else None}


def measure_cast_anchor(context, sequence, boss):
    """Report release palm in actor space without changing the gameplay emitter."""
    release = float(boss.get_editor_property("mage_cast_release_fraction"))
    pose = pose_at(context, sequence, float(sequence.get_play_length()) * release)
    hand = u.AnimPoseExtensions.get_bone_pose(pose, "hand_r", u.AnimPoseSpaces.WORLD)
    palm = u.MathLibrary.transform_location(hand, u.Vector(0, 8, 0))
    bounds = context["mesh"].get_bounds()
    origin = bounds.get_editor_property("origin")
    extent = bounds.get_editor_property("box_extent")
    height = float(boss.get_editor_property("mage_height"))
    require(extent.z > 0, "Existing mage mesh bounds have no height")
    scale = height / (2 * extent.z)
    yaw = math.radians(float(boss.get_editor_property("mage_model_yaw")))
    body_radius = max(60.0, min(260.0, height * .45))
    x, y = (palm.x - origin.x) * scale, (palm.y - origin.y) * scale
    local = [x * math.cos(yaw) - y * math.sin(yaw), x * math.sin(yaw) + y * math.cos(yaw),
             (palm.z - origin.z) * scale + height * .5 - body_radius]
    emitter = vec(boss.get_editor_property("mage_cast_offset"))
    return {"release_fraction": release, "palm_actor_local_cm": local,
            "preserved_gameplay_emitter_cm": emitter, "distance_to_emitter_cm": math.dist(local, emitter),
            "scope": "Imported base pose; runtime aiming and palm IK are added afterwards"}


def main(import_only=None):
    if import_only is None:
        import_only = os.environ.get("MC_NUT_STYLE_V2_IMPORT_ONLY", "0") == "1"
    stage = "import" if import_only else "integration"
    report_path = ROOT / ("Saved/Checks/NutStyleV2Import.json" if import_only else
                          "Saved/Checks/NutStyleV2Integration.json")
    commandlet = "-run=" in u.SystemLibrary.get_command_line().lower()
    if not commandlet:
        require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),
                "Stop PIE before importing and connecting Style v2")
    initial_dirty = dirty_packages()
    profile = lib.load_asset(PROFILE)
    require(isinstance(profile, u.MCNutRainProfile), "Existing DA_NutRain is missing")
    require(import_only or PROFILE not in initial_dirty,
            "DA_NutRain has unsaved edits; keep them before connecting new clips")
    original_settings = profile.get_editor_property("settings").copy()
    settings = original_settings.copy()
    original_boss = settings.get_editor_property("boss").copy()
    boss = original_boss.copy()
    # Import-only runs with the existing Editor binary; binding needs new slots.
    baseline_nonanimation = None
    if not import_only:
        for field in BINDING_FIELDS:
            boss.get_editor_property(field)
        baseline_nonanimation = without_bindings(original_boss)
    protected_before = protected_disk_hashes()
    contexts = {}
    for spec in ROLE_SPECS:
        base = "/Game/Gameplay/CoreLoop/" + spec["folder"]
        destination = base + "/Animations/MCStyleV2"
        mesh = lib.load_asset(base + "/" + spec["mesh"])
        require(isinstance(mesh, u.SkeletalMesh), "Existing boss skeletal mesh is missing: " + base)
        require(boss.get_editor_property(spec["role"] + "_skeletal_mesh") == mesh,
                "DA_NutRain uses another boss mesh; inspect before binding style clips: " + spec["role"])
        skeleton = mesh.get_editor_property("skeleton")
        require(isinstance(skeleton, u.Skeleton), "Existing boss mesh has no skeleton")
        require(not {base + "/" + spec["mesh"], package_path(skeleton)}.intersection(initial_dirty),
                "Existing boss mesh/skeleton has unsaved edits: " + base)
        source, manifest, records = read_export(spec)
        fps = float(manifest["fps"])
        require(abs(fps - round(fps)) < .0001 and 1 <= fps <= 120, "Unsupported export sample rate")
        expected_paths = {destination + "/" + spec["prefix"] + suffix for _, suffix in spec["clips"]}
        for path in lib.list_assets(destination, recursive=True, include_folder=False):
            obj = lib.load_asset(path)
            require(isinstance(obj, u.AnimSequence) and owned(obj, destination) and package_path(obj) in expected_paths,
                    "Version folder contains an unexpected or unowned asset: " + path)
        require(not initial_dirty.intersection(expected_paths), "Style v2 clips have unsaved editor changes")
        legacy_idle = lib.load_asset(base + "/Animations/" + spec["prefix"] + "Idle")
        require(isinstance(legacy_idle, u.AnimSequence), "Legacy Idle is required for proven FBX import settings")
        options = u.AnimPoseEvaluationOptions()
        options.set_editor_property("optional_skeletal_mesh", mesh)
        options.set_editor_property("extract_root_motion", False)
        options.set_editor_property("evaluation_type", u.AnimDataEvalType.RAW)
        reference = reference_snapshot(skeleton)
        require(all(name in reference["bones"] for name in ("root", "foot_l", "foot_r", "hand_r")),
                "Existing skeleton lacks expected exported boss bones")
        contexts[spec["role"]] = {"role": spec["role"], "source": source, "manifest": manifest, "records": records,
                                  "destination": destination, "mesh": mesh, "skeleton": skeleton,
                                  "legacy_idle": legacy_idle, "fps": fps, "reference": reference,
                                  "pose_options": options, "sequences": {}}
    report = {"passed": False, "stage": stage, "profile": profile.get_path_name(), "clips": [], "bindings": {},
              "preserved_unrelated_dirty_packages": sorted(initial_dirty), "cast_anchor": {}}
    cvar = "Interchange.FeatureFlags.Import.FBX"
    previous_interchange = u.SystemLibrary.get_console_variable_int_value(cvar)
    try:
        u.SystemLibrary.execute_console_command(None, cvar + " 0")
        for spec in ROLE_SPECS:
            context = contexts[spec["role"]]
            for field, suffix in spec["clips"]:
                sequence, record = import_clip(spec, context, suffix)
                record["role"] = spec["role"]
                record["validation"] = validate_clip(context, sequence, suffix)
                report["clips"].append(record)
                context["sequences"][suffix] = sequence
                key = spec["role"] + "_" + field + "_animation"
                if not import_only:
                    boss.set_editor_property(key, sequence)
                report["bindings"][key] = sequence.get_path_name()
            require(reference_snapshot(context["skeleton"]) == context["reference"],
                    "Animation import changed an existing skeleton reference pose")
        require(len(report["clips"]) == 19, "Style integration requires all 19 checked clips")
        for suffix in ("Cast", "HeavyCast", "Summon", "Rain"):
            context = contexts["mage"]
            report["cast_anchor"][suffix] = measure_cast_anchor(context, context["sequences"][suffix], boss)
            if str(context["manifest"]["source"]).endswith("_Game.blend"):
                require(report["cast_anchor"][suffix]["distance_to_emitter_cm"] < 12.,
                        "Corrected casting palm misses the preserved emitter: " + suffix)
        if not import_only:
            require(without_bindings(boss) == baseline_nonanimation, "Animation binding changed unrelated boss settings")
            settings.set_editor_property("boss", boss)
            settings_check = settings.copy()
            settings_check.set_editor_property("boss", original_boss)
            require(settings_check.export_text() == original_settings.export_text(), "Encounter tuning outside Boss changed")
        require(protected_disk_hashes() == protected_before, "Import changed a legacy art package on disk")
        allowed_dirty = {package_path(sequence) for context in contexts.values()
                         for sequence in context["sequences"].values()}
        if not import_only:
            allowed_dirty.add(PROFILE)
        unexpected_dirty = dirty_packages() - initial_dirty - allowed_dirty
        require(not unexpected_dirty, "Import unexpectedly dirtied protected/unrelated packages: " + str(sorted(unexpected_dirty)))
        saved = []
        for context in contexts.values():
            for sequence in context["sequences"].values():
                require(owned(sequence, context["destination"]), "Cannot save an unowned style clip")
                require(lib.save_loaded_asset(sequence, only_if_is_dirty=False), "Could not save checked style clip")
                saved.append(package_path(sequence))
        if import_only:
            require(profile.get_editor_property("settings").export_text() == original_settings.export_text(),
                    "Import-only changed the encounter profile")
        else:
            profile.modify()
            profile.set_editor_property("settings", settings)
            checked_settings = profile.get_editor_property("settings")
            checked_boss = checked_settings.get_editor_property("boss")
            for key, asset_path in report["bindings"].items():
                require(checked_boss.get_editor_property(key).get_path_name() == asset_path,
                        "DA_NutRain did not retain style binding: " + key)
            require(without_bindings(checked_boss) == baseline_nonanimation, "Profile serialization changed boss tuning")
            require(lib.save_loaded_asset(profile, only_if_is_dirty=False), "Could not save DA_NutRain style bindings")
            saved.append(PROFILE)
        require(protected_disk_hashes() == protected_before, "Saving style packages changed original art")
        require(dirty_packages() == initial_dirty, "Integration changed the unrelated dirty package set")
        report.update(passed=True, saved_packages=sorted(saved), original_art_packages_preserved=True,
                      skeleton_reference_poses_preserved=True, encounter_tuning_preserved=True,
                      bindings_applied=not import_only,
                      root_motion="Disabled; forced reference pose root lock; original bone tracks retained",
                      gameplay_validation="Package and imported base-pose validation; runtime/network checks are separate")
        u.log("MC_NUT_STYLE_V2_" + stage.upper() + "_PASS " +
              json.dumps({"clips": 19, "saved_packages": len(saved), "bindings_applied": not import_only}))
    except Exception as error:
        report["error"] = str(error)
        u.log_error("MC_NUT_STYLE_V2_" + stage.upper() + "_FAIL " + str(error))
        raise
    finally:
        u.SystemLibrary.execute_console_command(None, cvar + " " + str(previous_interchange))
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2), encoding="utf8")


if __name__ == "__main__":
    main()
