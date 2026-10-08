"""Import the owned Nut Tank rig, ball and seven animation-only FBX clips.

Run in an idle Editor, including through MCP's Slate Output Log console:
    py "E:/DEVGAME/MessControl/Tools/Unreal/import_nut_tank.py"
Only generated packages in /Game/Gameplay/CoreLoop/NutTank are saved. The
artist's original rig/materials are read as references and never reimported.
"""
import hashlib
import json
from pathlib import Path

import unreal as u


ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = ROOT / "ArtSource/NutTank"
DEST = "/Game/Gameplay/CoreLoop/NutTank"
ARTIST = "/Game/FromBlender8/SM_Nut_Tank"
OWNER_TAG = "MCNutTankDerivation"
OWNER_VALUE = "import_nut_tank.py:1"
CLIP_NAMES = tuple("A_NutTank_" + suffix for suffix in
                   ("Idle", "Walk", "WalkLeft", "WalkRight", "Melee", "Jump", "Transform"))
lib = u.EditorAssetLibrary
tools = u.AssetToolsHelpers.get_asset_tools()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def package_path(obj):
    return obj.get_path_name().split(".")[0]


def owned(obj):
    return (package_path(obj).startswith(DEST + "/") and
            lib.get_metadata_tag(obj, OWNER_TAG) == OWNER_VALUE)


def mark_owned(obj, source_file=None):
    require(package_path(obj).startswith(DEST + "/"),
            "Import unexpectedly produced a package outside the owned folder: " + obj.get_path_name())
    lib.set_metadata_tag(obj, OWNER_TAG, OWNER_VALUE)
    if source_file is not None:
        lib.set_metadata_tag(obj, "MCNutTankSource", str(source_file))


def disk_hashes(objects):
    result = {}
    for obj in objects:
        path = package_path(obj)
        if not path.startswith("/Game/"):
            continue
        stem = Path(u.Paths.project_content_dir()) / path[len("/Game/"):]
        for suffix in (".uasset", ".uexp", ".ubulk"):
            filename = stem.with_suffix(suffix)
            if filename.is_file():
                result[str(filename)] = hashlib.sha256(filename.read_bytes()).hexdigest()
    return result


def import_task(name, source_file, options, destination=DEST):
    filename = (SOURCE / source_file).resolve()
    require(filename.is_relative_to(SOURCE.resolve()) and filename.is_file(),
            "Required generated FBX is missing or outside ArtSource/NutTank: " + str(filename))
    task = u.AssetImportTask()
    task.filename = str(filename)
    task.destination_path = destination
    task.destination_name = name
    task.automated = True
    task.save = False
    task.replace_existing = True
    task.replace_existing_settings = True
    task.factory = u.FbxFactory()
    task.options = options
    return task


def base_options(import_type):
    options = u.FbxImportUI()
    options.set_editor_property("automated_import_should_detect_type", False)
    options.set_editor_property("mesh_type_to_import", import_type)
    options.set_editor_property("original_import_type", import_type)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("create_physics_asset", False)
    return options


def imported(task, asset_type):
    objects = [lib.load_asset(path) for path in task.imported_object_paths]
    require(objects and all(objects), "FBX import produced no usable assets: " + task.filename)
    for obj in objects:
        mark_owned(obj, task.filename)
    matching = [obj for obj in objects if isinstance(obj, asset_type)]
    require(len(matching) == 1, "Expected exactly one " + asset_type.__name__ +
            " from " + task.filename + ": " + str(list(task.imported_object_paths)))
    asset = matching[0]
    target = task.destination_path + "/" + task.destination_name
    if package_path(asset) != target:
        require(not lib.does_asset_exist(target),
                "Importer returned an unexpected name while the target already exists: " + target)
        require(lib.rename_asset(asset.get_path_name(), target), "Cannot normalize imported asset name: " + target)
        asset = lib.load_asset(target)
    mark_owned(asset, task.filename)
    return asset


def apply_artist_materials(mesh, artist_slots):
    walnut = artist_slots[0].material_interface
    hands = artist_slots[1].material_interface
    require(walnut and hands, "The artist Walnut and hand/eye materials must both be assigned")
    slots = list(mesh.get_editor_property("materials"))
    require(slots, "Generated tank has no material slots")
    report = []
    for slot in slots:
        label = str(slot.material_slot_name)
        normalized = label.lower().replace("_", "").replace("-", "").replace(" ", "").replace("/", "")
        # A procedural club may acquire a separate slot; its intended fallback
        # is the artist's walnut surface, rather than a newly imported material.
        material = hands if normalized in ("handeye", "material001") else walnut
        slot.material_interface = material
        report.append({"slot": label, "material": material.get_path_name()})
    mesh.set_editor_property("materials", slots)
    return report


def main():
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),
            "Stop PIE before importing the generated Nut Tank")
    dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    dirty += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    require(not dirty, "Preserve unsaved packages before importing: " + ", ".join(p.get_name() for p in dirty))

    manifest = json.loads((SOURCE / "ExportReport.json").read_text(encoding="utf8"))
    require(manifest["status"] == "complete" and manifest["source_preserved"],
            "Exporter did not complete with original source preservation")
    records = [{**record, "name": Path(record["file"]).stem} for record in manifest["clips"]]
    require(len(records) == 7 and {record["name"] for record in records} == set(CLIP_NAMES),
            "ExportReport must contain exactly the seven agreed A_NutTank clips")
    hierarchy_hashes = {entry["fbx"]["hierarchy_sha256"] for entry in manifest["files"]
                        if entry["file"] == "SK_NutTank.fbx" or entry["file"] in {record["file"] for record in records}}
    require(len(hierarchy_hashes) == 1, "Mesh and animation FBX have incompatible deform hierarchies")
    mesh_file = manifest.get("mesh_file", "SK_NutTank.fbx")
    ball_file = manifest.get("ball_file", "SM_NutTank_Ball.fbx")
    for filename in (mesh_file, ball_file, *(record["file"] for record in records)):
        path = (SOURCE / filename).resolve()
        require(path.is_relative_to(SOURCE.resolve()) and path.is_file(), "Missing generated FBX: " + str(path))

    artist = lib.load_asset(ARTIST)
    require(isinstance(artist, u.SkeletalMesh), "Required original artist skeletal mesh is missing: " + ARTIST)
    artist_slots = list(artist.get_editor_property("materials"))
    require(len(artist_slots) >= 2, "Original artist mesh lacks Walnut and hand/eye material slots")
    artist_material_snapshot = [(str(slot.material_slot_name), slot.material_interface) for slot in artist_slots]
    original_objects = [artist, artist.get_editor_property("skeleton")]
    original_objects += [slot.material_interface for slot in artist_slots if slot.material_interface]
    original_hashes = disk_hashes(obj for obj in original_objects if obj)

    for path in lib.list_assets(DEST, recursive=True, include_folder=False):
        obj = lib.load_asset(path)
        require(obj and owned(obj), "Refusing to replace an unowned asset: " + path)
    existing_mesh = lib.load_asset(DEST + "/SK_NutTank")
    skeleton = None
    if existing_mesh:
        require(isinstance(existing_mesh, u.SkeletalMesh) and owned(existing_mesh), "Generated mesh has an unexpected type/owner")
        skeleton = existing_mesh.get_editor_property("skeleton")
        require(skeleton and owned(skeleton), "Generated mesh must retain its own compatible skeleton")

    interchange_cvar = "Interchange.FeatureFlags.Import.FBX"
    previous_interchange = u.SystemLibrary.get_console_variable_int_value(interchange_cvar)
    try:
        u.SystemLibrary.execute_console_command(None, interchange_cvar + " 0")
        options = base_options(u.FBXImportType.FBXIT_SKELETAL_MESH)
        options.set_editor_property("import_mesh", True)
        options.set_editor_property("import_as_skeletal", True)
        options.set_editor_property("import_animations", False)
        if skeleton:
            options.set_editor_property("skeleton", skeleton)
        data = options.get_editor_property("skeletal_mesh_import_data")
        data.set_editor_property("update_skeleton_reference_pose", False)
        data.set_editor_property("use_t0_as_ref_pose", False)
        data.set_editor_property("normal_import_method", u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
        mesh_task = import_task("SK_NutTank", mesh_file, options)
        tools.import_asset_tasks([mesh_task])
        mesh = imported(mesh_task, u.SkeletalMesh)
        imported_skeleton = mesh.get_editor_property("skeleton")
        require(imported_skeleton and package_path(imported_skeleton).startswith(DEST + "/"),
                "Generated tank did not receive an independent skeleton")
        require(not skeleton or imported_skeleton == skeleton, "Reimport unexpectedly replaced the owned skeleton")
        skeleton = imported_skeleton
        mark_owned(skeleton, SOURCE / mesh_file)
        bone_names = [str(name) for name in skeleton.get_reference_pose().get_bone_names()]
        # The FBX has 49 deform LimbNodes plus a Null root. The importer may
        # retain that wrapper as a 50th bone; compatibility is the shared FBX
        # hierarchy and unchanged imported skeleton, not a guessed bone count.
        require(bone_names, "Generated skeleton contains no bones")
        mesh_materials = apply_artist_materials(mesh, artist_slots)

        options = base_options(u.FBXImportType.FBXIT_STATIC_MESH)
        options.set_editor_property("import_mesh", True)
        options.set_editor_property("import_as_skeletal", False)
        options.set_editor_property("import_animations", False)
        options.get_editor_property("static_mesh_import_data").set_editor_property("combine_meshes", True)
        ball_task = import_task("SM_NutTank_Ball", ball_file, options)
        tools.import_asset_tasks([ball_task])
        ball = imported(ball_task, u.StaticMesh)
        ball_slots = list(ball.get_editor_property("static_materials"))
        require(ball_slots, "Imported ball has no material slots")
        for slot in ball_slots:
            slot.material_interface = artist_slots[0].material_interface
        ball.set_editor_property("static_materials", ball_slots)

        tasks = []
        for record in records:
            options = base_options(u.FBXImportType.FBXIT_ANIMATION)
            options.set_editor_property("import_mesh", False)
            options.set_editor_property("import_animations", True)
            options.set_editor_property("skeleton", skeleton)
            data = options.get_editor_property("anim_sequence_import_data")
            data.set_editor_property("animation_length", u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
            data.set_editor_property("use_default_sample_rate", True)
            data.set_editor_property("import_bone_tracks", True)
            data.set_editor_property("preserve_local_transform", True)
            data.set_editor_property("import_custom_attribute", False)
            tasks.append(import_task(record["name"], record["file"], options, DEST + "/Animations"))
        tools.import_asset_tasks(tasks)
        clips = []
        for record, task in zip(records, tasks):
            sequence = imported(task, u.AnimSequence)
            require(all(isinstance(lib.load_asset(path), u.AnimSequence) for path in task.imported_object_paths),
                    "Animation-only import unexpectedly created other assets: " + task.filename)
            require(sequence.get_editor_property("skeleton") == skeleton, "Clip imported on an incompatible skeleton")
            length = sequence.get_play_length()
            expected_length = record.get("duration_seconds")
            require(length > 0 and (expected_length is None or abs(length - expected_length) < .04),
                    "Unexpected clip duration: " + record["name"] + " " + str(length))
            sequence.set_editor_property("enable_root_motion", False)
            sequence.set_editor_property("force_root_lock", True)
            sequence.set_editor_property("root_motion_root_lock", u.RootMotionRootLock.REF_POSE)
            # Do not remove a root track based on another rig's FBX scale issue.
            # The generated rig's reference pose is kept and world movement is native.
            clips.append({**record, "asset": sequence.get_path_name(), "imported_duration": length})
        require([str(name) for name in skeleton.get_reference_pose().get_bone_names()] == bone_names,
                "Clip imports changed the generated skeleton hierarchy")
        require([(str(slot.material_slot_name), slot.material_interface) for slot in
                 artist.get_editor_property("materials")] == artist_material_snapshot,
                "Original artist material slots changed")
        require(disk_hashes(obj for obj in original_objects if obj) == original_hashes,
                "Original artist packages changed on disk")

        saved_paths = []
        for path in lib.list_assets(DEST, recursive=True, include_folder=False):
            obj = lib.load_asset(path)
            require(obj and owned(obj), "Unexpected unowned asset appeared during import: " + path)
            require(lib.save_loaded_asset(obj, only_if_is_dirty=False), "Cannot save generated asset: " + path)
            saved_paths.append(package_path(obj))
        remaining = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
        remaining += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
        require(not remaining, "Import left unsaved packages: " + ", ".join(p.get_name() for p in remaining))
        report = {"passed": True, "mesh": mesh.get_path_name(), "skeleton": skeleton.get_path_name(),
                  "bone_names": bone_names, "ball": ball.get_path_name(), "materials": mesh_materials,
                  "ball_material": artist_slots[0].material_interface.get_path_name(), "clips": clips,
                  "root_motion": "Disabled; forced root lock to reference pose; no bone tracks removed",
                  "original_packages_preserved": True, "saved_packages": sorted(set(saved_paths))}
        output = ROOT / "Saved/Checks/NutTankImport.json"
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2), encoding="utf8")
        u.log("MC_NUT_TANK_IMPORT_PASS " + json.dumps(report))
    finally:
        u.SystemLibrary.execute_console_command(None, interchange_cvar + " " + str(previous_interchange))


if __name__ == "__main__":
    main()
