"""Import the owned wizard rig and eight phase-ready animation references.

Run in an idle Editor with py "E:/DEVGAME/MessControl/Tools/Unreal/import_nut_wizard.py".
Only /Game/Gameplay/CoreLoop/NutWizard is created/saved. Shared artist Walnut and
hand/eye materials are read from the original Tank, never changed or reimported.
Packed original texture bytes remain recorded in ArtSource/NutWizard/Textures.
"""
import hashlib
import json
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = ROOT / "ArtSource/NutWizard"
DEST = "/Game/Gameplay/CoreLoop/NutWizard"
ARTIST_MATERIAL_REFERENCE = "/Game/FromBlender8/SM_Nut_Tank"
OWNER_TAG = "MCNutWizardDerivation"
OWNER_VALUE = "import_nut_wizard.py:1"
CLIP_NAMES = {"A_NutWizard_" + suffix for suffix in
              ("Idle", "Walk", "Cast", "HeavyCast", "Summon", "Rain", "Hit", "Death")}
lib = u.EditorAssetLibrary
tools = u.AssetToolsHelpers.get_asset_tools()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def package_path(obj):
    return obj.get_path_name().split(".")[0]


def owned(obj):
    return (package_path(obj).startswith(DEST + "/") and lib.get_metadata_tag(obj, OWNER_TAG) == OWNER_VALUE)


def mark_owned(obj, source_file=None):
    require(package_path(obj).startswith(DEST + "/"), "Importer created a package outside its owned folder: " + obj.get_path_name())
    lib.set_metadata_tag(obj, OWNER_TAG, OWNER_VALUE)
    if source_file:
        lib.set_metadata_tag(obj, "MCNutWizardSource", str(source_file))


def disk_hashes(objects):
    result = {}
    for obj in objects:
        package = package_path(obj)
        if package.startswith("/Game/"):
            stem = Path(u.Paths.project_content_dir()) / package[len("/Game/"):]
            for suffix in (".uasset", ".uexp", ".ubulk"):
                filename = stem.with_suffix(suffix)
                if filename.is_file():
                    result[str(filename)] = hashlib.sha256(filename.read_bytes()).hexdigest()
    return result


def import_task(name, filename, options, destination=DEST):
    source = (SOURCE / filename).resolve()
    require(source.is_relative_to(SOURCE.resolve()) and source.is_file(), "Generated FBX missing or outside owned source folder: " + str(source))
    task = u.AssetImportTask()
    task.filename = str(source)
    task.destination_path = destination
    task.destination_name = name
    task.automated = True
    task.save = False
    task.replace_existing = True
    task.replace_existing_settings = True
    task.factory = u.FbxFactory()
    task.options = options
    return task


def options_for(import_type):
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
    require(objects and all(objects), "No usable assets imported: " + task.filename)
    for obj in objects:
        mark_owned(obj, task.filename)
    matching = [obj for obj in objects if isinstance(obj, asset_type)]
    require(len(matching) == 1, "Expected exactly one " + asset_type.__name__ + ": " + task.filename)
    asset = matching[0]
    target = task.destination_path + "/" + task.destination_name
    if package_path(asset) != target:
        require(not lib.does_asset_exist(target), "Unexpected import name would replace an existing asset: " + target)
        require(lib.rename_asset(asset.get_path_name(), target), "Cannot normalize imported asset name: " + target)
        asset = lib.load_asset(target)
    mark_owned(asset, task.filename)
    return asset


def main():
    commandlet = "-run=" in u.SystemLibrary.get_command_line().lower()
    if not commandlet:
        require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), "Stop PIE before importing the wizard")
    dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    require(not dirty, "Preserve unrelated unsaved edits before importing: " + ", ".join(package.get_name() for package in dirty))
    manifest = json.loads((SOURCE / "ExportReport.json").read_text(encoding="utf8"))
    require(manifest["status"] == "complete" and manifest["source_preserved"] and manifest["hierarchy_consistent"]
            and manifest["derived_reference_pose_unchanged"] and manifest["root_animation_constant"], "Wizard export invariants did not pass")
    records = [{**record, "name": Path(record["file"]).stem} for record in manifest["clips"]]
    require(len(records) == len(CLIP_NAMES) and {record["name"] for record in records} == CLIP_NAMES, "Manifest must contain all eight agreed wizard clips")
    mesh_file = manifest["mesh_file"]
    required_files = {mesh_file, *(record["file"] for record in records)}
    file_records = {record["file"]: record for record in manifest["files"]}
    require(required_files.issubset(file_records), "Manifest omits an agreed FBX")
    require(len({file_records[filename]["fbx"]["hierarchy_sha256"] for filename in required_files}) == 1,
            "Wizard mesh and clips have incompatible hierarchies")
    for filename in required_files:
        path = (SOURCE / filename).resolve()
        require(path.is_relative_to(SOURCE.resolve()) and path.is_file(), "Missing generated FBX: " + str(path))
        require(hashlib.sha256(path.read_bytes()).hexdigest() == file_records[filename]["sha256"], "Generated FBX changed after export: " + filename)
    artist = lib.load_asset(ARTIST_MATERIAL_REFERENCE)
    require(isinstance(artist, u.SkeletalMesh), "Shared artist material reference is missing: " + ARTIST_MATERIAL_REFERENCE)
    artist_slots = list(artist.get_editor_property("materials"))
    require(len(artist_slots) >= 2 and artist_slots[0].material_interface and artist_slots[1].material_interface,
            "Artist material reference lacks Walnut and hand/eye")
    originals = [artist, artist.get_editor_property("skeleton"), *(slot.material_interface for slot in artist_slots if slot.material_interface)]
    before_hashes = disk_hashes(originals)
    before_materials = [(str(slot.material_slot_name), slot.material_interface) for slot in artist_slots]
    for path in lib.list_assets(DEST, recursive=True, include_folder=False):
        obj = lib.load_asset(path)
        require(obj and owned(obj), "Refusing to overwrite an unowned wizard asset: " + path)
    existing_mesh = lib.load_asset(DEST + "/SK_NutWizard")
    skeleton = None
    if existing_mesh:
        require(isinstance(existing_mesh, u.SkeletalMesh) and owned(existing_mesh), "Existing generated wizard has an unexpected owner/type")
        skeleton = existing_mesh.get_editor_property("skeleton")
        require(skeleton and owned(skeleton), "Wizard must keep its own independent skeleton")
    cvar = "Interchange.FeatureFlags.Import.FBX"
    previous_interchange = u.SystemLibrary.get_console_variable_int_value(cvar)
    try:
        u.SystemLibrary.execute_console_command(None, cvar + " 0")
        options = options_for(u.FBXImportType.FBXIT_SKELETAL_MESH)
        options.set_editor_property("import_mesh", True)
        options.set_editor_property("import_as_skeletal", True)
        options.set_editor_property("import_animations", False)
        if skeleton:
            options.set_editor_property("skeleton", skeleton)
        data = options.get_editor_property("skeletal_mesh_import_data")
        data.set_editor_property("update_skeleton_reference_pose", False)
        data.set_editor_property("use_t0_as_ref_pose", False)
        data.set_editor_property("normal_import_method", u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
        task = import_task("SK_NutWizard", mesh_file, options)
        tools.import_asset_tasks([task])
        mesh = imported(task, u.SkeletalMesh)
        generated_skeleton = mesh.get_editor_property("skeleton")
        require(generated_skeleton and package_path(generated_skeleton).startswith(DEST + "/"), "Wizard did not receive an independent skeleton")
        require(not skeleton or generated_skeleton == skeleton, "Reimport replaced the owned skeleton")
        skeleton = generated_skeleton
        mark_owned(skeleton, SOURCE / mesh_file)
        bone_names = [str(name) for name in skeleton.get_reference_pose().get_bone_names()]
        # The proven Tank importer sanitizes FBX dots to underscores in Unreal.
        source_cast_bone = manifest["casting_anchor"]["bone"]
        cast_bone = next((name for name in (source_cast_bone, source_cast_bone.replace(".", "_")) if name in bone_names), None)
        require(cast_bone is not None, "Imported skeleton lacks the agreed casting bone")
        material_report = []
        mesh_slots = list(mesh.get_editor_property("materials"))
        require(mesh_slots, "Wizard mesh has no material slots")
        for slot in mesh_slots:
            normalized = str(slot.material_slot_name).lower().replace("_", "").replace("/", "").replace(" ", "")
            slot.material_interface = artist_slots[1 if normalized in ("handeye", "material001") else 0].material_interface
            material_report.append({"slot": str(slot.material_slot_name), "material": slot.material_interface.get_path_name()})
        mesh.set_editor_property("materials", mesh_slots)
        tasks = []
        for record in records:
            options = options_for(u.FBXImportType.FBXIT_ANIMATION)
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
            require(all(isinstance(lib.load_asset(path), u.AnimSequence) for path in task.imported_object_paths), "Animation-only import created non-animation assets")
            require(sequence.get_editor_property("skeleton") == skeleton, "Animation imported against an incompatible skeleton")
            length = sequence.get_play_length()
            require(length > 0 and abs(length - record["duration_seconds"]) < .04, "Unexpected duration for " + record["name"])
            sequence.set_editor_property("enable_root_motion", False)
            sequence.set_editor_property("force_root_lock", True)
            sequence.set_editor_property("root_motion_root_lock", u.RootMotionRootLock.REF_POSE)
            clips.append({**record, "asset": sequence.get_path_name(), "imported_duration": length})
        require([str(name) for name in skeleton.get_reference_pose().get_bone_names()] == bone_names, "Clip imports changed the owned skeleton hierarchy")
        require(before_materials == [(str(slot.material_slot_name), slot.material_interface) for slot in artist.get_editor_property("materials")], "Shared artist material slots changed")
        require(disk_hashes(originals) == before_hashes, "Shared artist packages changed on disk")
        saved = []
        for path in lib.list_assets(DEST, recursive=True, include_folder=False):
            obj = lib.load_asset(path)
            require(obj and owned(obj), "An unowned package appeared during import: " + path)
            require(lib.save_loaded_asset(obj, only_if_is_dirty=False), "Could not save owned wizard asset: " + path)
            saved.append(package_path(obj))
        dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
        require(not dirty, "Import left dirty packages: " + ", ".join(package.get_name() for package in dirty))
        report = {"passed": True, "mesh": mesh.get_path_name(), "skeleton": skeleton.get_path_name(), "bone_names": bone_names,
                  "materials": material_report, "material_reference_mesh": ARTIST_MATERIAL_REFERENCE, "clips": clips,
                  "casting_anchor": {**manifest["casting_anchor"], "imported_bone": cast_bone}, "original_packages_preserved": True,
                  "saved_packages": sorted(set(saved)), "playback_contract": manifest["playback_contract"],
                  "root_motion": "Disabled, force root lock REF_POSE; original tracks retained; native authoritative phases control playback"}
        output = ROOT / "Saved/Checks/NutWizardImport.json"
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2), encoding="utf8")
        u.log("MC_NUT_WIZARD_IMPORT_PASS " + json.dumps(report))
    finally:
        u.SystemLibrary.execute_console_command(None, cvar + " " + str(previous_interchange))


if __name__ == "__main__":
    main()
