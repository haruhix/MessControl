"""Plan/apply the asset-only CoffeeSlime v2 migration; v1 importer is unchanged.

Run in the MessControl Editor. Default mode only writes a technical plan.
After ROOT's visual gate, apply the exact manifest revision explicitly:
    runpy.run_path(path, run_name="__main__", init_globals={
        "MC_COFFEE_V2_APPLY_REVISION": "v2"})
After an unsaved failed owned attempt only, MC_COFFEE_V2_RESUME_FAILURE=True
reuses its verified original disk backup. It never backs up partial live state
as the baseline and rejects unrelated/new dirty migration targets.

Expected ArtSource/CoffeeSlime/v2/manifest.json additions to the v1 schema:
  schema_version: 2; revision: "v2"; fbx_units: "centimeters"
  mesh_file: "exports/SK_CoffeeSlime.fbx"
  skeleton.reference_positions_space: "UE_COMPONENT"
  skeleton.reference_positions_cm: {bone: [x, y, z], ...all five bones}
  materials: {slot_name: {color:[r,g,b], roughness:0.3, coat:0.0,
      coat_roughness:0.1, specular:0.5, color_gain:1.0, emissive_gain:0.0,
      textures:{BaseColor:"textures/name.png", Roughness:"textures/name.png"}}}
  clips: unchanged names/timings; each record additionally has file:"exports/...fbx"
  clips.A_CoffeeSlime_Hop.min_gel_body_lift_cm: measured export lower bound

material_slots is the exact ordered FBX slot list, with any positive slot count.
Textures are optional. A texture entry may instead be {file, asset_name, size}.
Reference positions are expected UE component-space centimeters, including its
handedness conversion, rather than unconverted Blender bone-head coordinates.
Optional integration fields: dimensions_tolerance_fraction, rest_position_tolerance_cm.

The same owned mesh/skeleton identities are retained. Mesh import explicitly
updates skeleton rest pose, then ALL five clips are reimported onto that rest;
no stale animation/source-hash shortcut is used. Known saved packages and the
manifest are backed up under Saved before the first asset/CVar mutation. No
automatic rollback is attempted against loaded objects after failure.
"""
import hashlib
import json
import math
import re
import runpy
import shutil
import struct
import traceback
import uuid
from datetime import datetime, timezone
from pathlib import Path

import unreal as u


ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = ROOT / "ArtSource/CoffeeSlime/v2"
MANIFEST_FILE = SOURCE / "manifest.json"
DEST = "/Game/Cataclysm/Jelly/Coffee"
MESH_PATH = DEST + "/SK_CoffeeSlime"
SKELETON_PATH = DEST + "/SK_CoffeeSlime_Skeleton"
OUTPUT = ROOT / "Artifacts/CoffeeSlimeV2_20261015/ImportReport.json"
OWNER_TAG = "MCCoffeeSlimeGenerated"
OWNER_VALUE = "import_coffee_slime_v2.py:2"
V1_OWNER_VALUE = "import_coffee_slime.py:1"
REVISION_TAG = "MCCoffeeSlimeRevision"
MANIFEST_TAG = "MCCoffeeSlimeManifestSHA256"
SOURCE_TAG = "MCCoffeeSlimeSourceSHA256"
GRAPH_TAG = "MCCoffeeSlimeMaterialSpecSHA256"
BUILD_TAG = "MCCoffeeSlimeMaterialBuild"
PREVIOUS_OWNER_TAG = "MCCoffeeSlimePreviousGenerator"
BONE_PARENTS = dict(root=None, gel_body="root", gel_top="gel_body", face="gel_body", foam_tip="gel_top")
CLIP_DURATIONS = dict(Idle=1.2, Hop=.9, Hurt=.28, PreBurst=.6, Burst=.18)
CLIP_PATHS = {"A_CoffeeSlime_" + name: DEST + "/Animations/A_CoffeeSlime_" + name for name in CLIP_DURATIONS}
V1_MATERIAL_NAMES = ("M_CoffeeGel", "M_MilkFoam", "M_Ivory", "M_MauveLid", "M_EspressoDark", "M_CaramelLip")
LEGACY_TYPES = {MESH_PATH: u.SkeletalMesh, SKELETON_PATH: u.Skeleton}
LEGACY_TYPES.update({path: u.AnimSequence for path in CLIP_PATHS.values()})
LEGACY_TYPES.update({DEST + "/" + name: u.Material for name in V1_MATERIAL_NAMES})
LEGACY_TYPES.update({DEST + "/Textures/T_" + name + "_" + role: u.Texture2D
                     for name in ("CoffeeGel", "MilkFoam") for role in ("BaseColor", "Roughness")})
lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
tools = u.AssetToolsHelpers.get_asset_tools()
# Load only the definitions of the proven v1 helpers. Its main/import path does
# not run, and none of its v1 source/material/ownership globals are changed.
V1 = runpy.run_path(str(ROOT / "Tools/Unreal/import_coffee_slime.py"),
                    run_name="coffee_slime_v1_support")["CoffeeImporter"]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def package(obj):
    require(obj, "Missing expected asset object")
    return obj.get_path_name().split(".")[0]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_file(relative):
    path = (SOURCE / relative).resolve()
    require(path.is_relative_to(SOURCE.resolve()), "Source path escapes CoffeeSlime/v2: " + str(relative))
    return path


def asset_name(value):
    require(isinstance(value, str) and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value),
            "Expected an asset basename, not a path: " + str(value))
    return value


def dirty_packages():
    return {p.get_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
            + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}


def vector3(value):
    require(isinstance(value, (list, tuple)) and len(value) == 3 and all(
        isinstance(v, (float, int)) and math.isfinite(v) for v in value), "Expected a finite XYZ vector")
    return [float(v) for v in value]


def quat_angle(a, b):
    length = math.sqrt(sum(v * v for v in a) * sum(v * v for v in b))
    require(length > 1.e-10, "Invalid bone quaternion")
    return math.degrees(2. * math.acos(min(1., abs(sum(x * y for x, y in zip(a, b))) / length)))


class CoffeeV2Importer:
    node = staticmethod(V1.node)
    connect_input = staticmethod(V1.connect_input)
    transform_dict = staticmethod(V1.transform_dict)

    def __init__(self, report, manifest):
        self.report, self.manifest = report, manifest
        self.revision = manifest["revision"]
        self.manifest_sha = sha256(MANIFEST_FILE)
        self.assets, self.existing_assets, self.targets, self.texture_specs = {}, {}, {}, {}
        self.dirty_before = set()
        self.backup_complete = False
        self.resume_failure = None
        self.resume_dirty = set()
        if globals().get("MC_COFFEE_V2_RESUME_FAILURE", False):
            require(OUTPUT.is_file(), "No failed v2 report to resume")
            previous = json.loads(OUTPUT.read_text(encoding="utf-8"))
            require(previous.get("status") == "failed" and previous.get("mode") == "apply"
                    and previous.get("revision") == self.revision
                    and previous.get("manifest_sha256") == self.manifest_sha
                    and not previous.get("saved_packages"),
                    "Resume requires the exact manifest and an unsaved failed apply attempt")
            require(previous.get("backup_manifest") and previous.get("touched_packages"),
                    "Failed attempt has no original backup or owned touched set")
            self.resume_failure = previous
            self.report.update(backup_directory=previous.get("backup_directory"),
                               backup_manifest=previous["backup_manifest"], resume_requested=True)

    def acceptable(self, obj):
        if not obj or not package(obj).startswith(DEST + "/"):
            return False
        owner = lib.get_metadata_tag(obj, OWNER_TAG)
        if owner == OWNER_VALUE:
            return lib.get_metadata_tag(obj, REVISION_TAG).startswith("v2")
        expected = LEGACY_TYPES.get(package(obj))
        return owner == V1_OWNER_VALUE and expected is not None and isinstance(obj, expected)

    def validate_manifest(self):
        m = self.manifest
        require(m.get("schema_version") == 2 and m.get("asset") == "CoffeeSlime"
                and isinstance(self.revision, str) and re.fullmatch(r"v2(?:[._-][A-Za-z0-9_-]+)?", self.revision),
                "Expected a CoffeeSlime schema2/v2 revision")
        require(m.get("units") == "meters" and m.get("fbx_units") == "centimeters"
                and m.get("forward") == "+X" and m.get("up") == "+Z" and m.get("pivot") == "floor",
                "Expected meter source / canonical centimeter FBX / floor pivot / +X +Z")
        dimensions = vector3(m["expected_dimensions_m"])
        require(all(v > 0 for v in dimensions), "Expected positive manifest dimensions")
        for key, default in (("dimensions_tolerance_fraction", .05), ("rest_position_tolerance_cm", .5)):
            value = m.get("integration", {}).get(key, default)
            require(isinstance(value, (float, int)) and math.isfinite(value) and 0. < value <= 1.,
                    "Invalid integration tolerance: " + key)
        skeleton = m["skeleton"]
        require(skeleton["bone_parents"] == BONE_PARENTS and skeleton["reference_positions_space"] == "UE_COMPONENT",
                "The native hierarchy and UE component-space rest contract must remain explicit")
        require(set(skeleton["reference_positions_cm"]) == set(BONE_PARENTS), "Declare all five reference bone positions")
        for position in skeleton["reference_positions_cm"].values():
            vector3(position)
        require(max(abs(v) for v in skeleton["reference_positions_cm"]["root"]) < .1,
                "Root must remain at the floor origin")
        require(set(m["clips"]) == set(CLIP_PATHS), "Keep the five native clip names")
        files = [source_file(m["mesh_file"])]
        require(files[0].suffix.lower() == ".fbx", "Expected the canonical centimeter mesh FBX")
        for role, duration in CLIP_DURATIONS.items():
            name = "A_CoffeeSlime_" + role
            clip = m["clips"][name]
            require(all(isinstance(clip[key], int) for key in ("frame_start", "frame_end", "fps"))
                    and clip["fps"] == 100 and abs(clip["duration_seconds"] - duration) < 1.e-6
                    and clip["frame_end"] - clip["frame_start"] == round(duration * 100),
                    "Native duration/100Hz contract changed: " + name)
            require(clip["critical_frames"] and all(isinstance(frame, int) and clip["frame_start"] <= frame <= clip["frame_end"]
                    for frame in clip["critical_frames"]), "Critical frames must be inside the clip: " + name)
            files.append(source_file(clip["file"]))
            require(files[-1].suffix.lower() == ".fbx", "Expected a canonical centimeter clip FBX: " + name)
        hop_lift = m["clips"]["A_CoffeeSlime_Hop"]["min_gel_body_lift_cm"]
        require(isinstance(hop_lift, (float, int)) and math.isfinite(hop_lift) and hop_lift > 0.,
                "Declare a measured Hop lift gate")
        slots = m["material_slots"]
        require(slots and len(set(slots)) == len(slots) and set(slots) == set(m["materials"]),
                "Material specs must exactly cover the ordered manifest slot list")
        self.targets = {MESH_PATH: u.SkeletalMesh, SKELETON_PATH: u.Skeleton}
        self.targets.update({path: u.AnimSequence for path in CLIP_PATHS.values()})
        for name, spec in m["materials"].items():
            self.targets[DEST + "/" + asset_name(name)] = u.Material
            require(all(value >= 0. for value in vector3(spec["color"])), "Negative material color: " + name)
            gain = spec.get("color_gain", 1.)
            require(isinstance(gain, (float, int)) and math.isfinite(gain) and gain > 0., "Invalid color gain: " + name)
            for key, default in (("roughness", None), ("coat", 0.), ("coat_roughness", .1), ("specular", .5), ("emissive_gain", 0.)):
                value = spec.get(key, default)
                require(isinstance(value, (float, int)) and math.isfinite(value) and 0. <= value <= 1.,
                        "Invalid PBR value: " + name + " " + key)
            for role, entry in spec.get("textures", {}).items():
                require(role in ("BaseColor", "Roughness"), "Unsupported baked texture role: " + role)
                entry = dict(file=entry) if isinstance(entry, str) else dict(entry)
                filename = source_file(entry["file"])
                stem = filename.stem
                default_name = stem if stem.startswith("T_") else "T_" + (stem[2:] if stem.startswith("M_") else stem)
                output_name = asset_name(entry.get("asset_name", default_name))
                target = DEST + "/Textures/" + output_name
                record = dict(file=str(filename), role=role, asset_name=output_name, size=entry.get("size"))
                require(target not in self.texture_specs or self.texture_specs[target] == record,
                        "Conflicting declarations for texture " + target)
                self.texture_specs[target] = record
                self.targets[target] = u.Texture2D
                files.append(filename)
        self.report.update(revision=self.revision, manifest_sha256=self.manifest_sha,
            expected_dimensions_m=dimensions, expected_reference_positions_cm=skeleton["reference_positions_cm"],
            material_slot_order=slots, targets=sorted(self.targets))
        missing = sorted(str(path) for path in files if not path.is_file())
        self.report["missing_source_files"] = missing
        self.report["source_sha256"] = {str(path.relative_to(ROOT)): sha256(path) for path in set(files) if path.is_file()}
        return not missing

    def preflight(self):
        require(Path(u.Paths.get_project_file_path()).name == "MessControl.uproject" and ROOT.name == "MessControl",
                "Wrong Editor project")
        require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), "Finish PIE before v2 import")
        current_dirty = dirty_packages()
        self.dirty_before = set(self.resume_failure["dirty_before"]) if self.resume_failure else current_dirty
        self.report["dirty_before"] = sorted(self.dirty_before)
        # Unrelated dirty packages are preserved. No target or rollback package
        # may contain unsaved artist edits when its saved bytes are backed up.
        protected = set(self.targets) | set(LEGACY_TYPES)
        require(not self.dirty_before.intersection(protected), "Preserve unsaved CoffeeSlime packages before migration")
        if self.resume_failure:
            touched = set(self.resume_failure["touched_packages"])
            prior_dirty = set(self.resume_failure["dirty_after_failure"])
            self.resume_dirty = current_dirty - self.dirty_before
            require(touched <= set(self.targets) and self.resume_dirty <= touched
                    and self.resume_dirty <= prior_dirty and self.dirty_before <= current_dirty,
                    "Resume dirty state differs from the exact failed owned attempt")
            for target in touched:
                obj = lib.load_asset(target)
                require(obj and self.acceptable(obj)
                        and lib.get_metadata_tag(obj, OWNER_TAG) == OWNER_VALUE
                        and lib.get_metadata_tag(obj, REVISION_TAG) == self.revision
                        and lib.get_metadata_tag(obj, MANIFEST_TAG) == self.manifest_sha,
                        "Resume refuses a missing/custom/changed failed target: " + target)
            self.report["dirty_at_resume"] = sorted(current_dirty)
            self.report["resumed_touched_packages"] = sorted(touched)
        else:
            require(current_dirty == self.dirty_before, "Dirty state changed during preflight")
        for path in lib.list_assets(DEST, recursive=True, include_folder=False):
            obj = lib.load_asset(path)
            require(obj, "Could not inspect existing CoffeeSlime package: " + path)
            if self.acceptable(obj):
                self.existing_assets[package(obj)] = obj
            elif package(obj) in self.targets:
                raise RuntimeError("Preserved unowned/custom migration target: " + path)
            else:
                self.report["preserved_unowned"].append(path)
        for target, kind in self.targets.items():
            if lib.does_asset_exist(target):
                obj = lib.load_asset(target)
                require(isinstance(obj, kind) and self.acceptable(obj), "Unexpected type/owner: " + target)
                self.existing_assets[target] = obj
        existing_mesh = self.existing_assets.get(MESH_PATH)
        if existing_mesh:
            require(package(existing_mesh.get_editor_property("skeleton")) == SKELETON_PATH,
                    "Existing mesh points to another skeleton; preserve it")
        for target, obj in sorted(self.existing_assets.items()):
            refs = sorted({str(p).split(".")[0] for p in lib.find_package_referencers_for_asset(target, True)})
            self.report["saved_referencers_before"][target] = refs
        if SKELETON_PATH in self.existing_assets:
            allowed = {MESH_PATH, SKELETON_PATH, *CLIP_PATHS.values()}
            refs = self.report["saved_referencers_before"][SKELETON_PATH]
            require(set(refs) <= allowed, "Skeleton is shared with unexpected saved packages: " + str(refs))
            require(all(self.acceptable(self.existing_assets.get(p)) for p in refs),
                    "Skeleton referencer is not a known owned mesh/clip")
        require(dirty_packages() <= self.dirty_before | self.resume_dirty,
                "Loading preflight assets dirtied packages; reload before migration")
        self.report["legacy_retained"] = sorted(p for p in self.existing_assets if p not in self.targets)
        self.report["reference_query_limit"] = "Saved hard/soft package references; not an exhaustive claim about transient Editor/code references."

    def backup(self):
        if self.resume_failure:
            previous = self.resume_failure
            filename = Path(previous["backup_manifest"]).resolve()
            base = (ROOT / "Saved/CoffeeSlimeV2Backups").resolve()
            require(filename.is_relative_to(base) and filename.name == "Before.json" and filename.is_file(),
                    "Resume backup path is outside the owned backup directory")
            before = json.loads(filename.read_text(encoding="utf-8"))
            require(before["revision"] == self.revision and before["manifest_sha256"] == self.manifest_sha
                    and set(before["planned_targets"]) == set(self.targets)
                    and set(before["dirty_before"]) == self.dirty_before,
                    "Original backup plan no longer matches this exact resume")
            for target, record in before["packages"].items():
                require(target in self.existing_assets, "Original backup asset disappeared: " + target)
                for item in record["files"]:
                    source, saved = Path(item["source"]).resolve(), Path(item["backup"]).resolve()
                    require(source.is_relative_to((ROOT/"Content").resolve()) and saved.is_relative_to(filename.parent)
                            and source.is_file() and saved.is_file()
                            and sha256(source) == item["sha256"] and sha256(saved) == item["sha256"],
                            "Original saved package or backup changed after the failed attempt: " + target)
            require(sha256(filename.parent/"manifest_v2.json") == self.manifest_sha,
                    "Original manifest backup changed")
            audit = filename.parent / ("Failure_" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
                                       + "_" + uuid.uuid4().hex[:8] + ".json")
            audit.write_text(json.dumps(previous, indent=2, ensure_ascii=False), encoding="utf-8")
            self.report.update(backup_directory=str(filename.parent), backup_manifest=str(filename),
                               resumed_failure_report=str(audit), resumed_original_backup=True)
            self.backup_complete = True
            return
        run = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_" + uuid.uuid4().hex[:8]
        directory = ROOT / "Saved/CoffeeSlimeV2Backups" / run
        directory.mkdir(parents=True, exist_ok=False)
        records = {}
        for target, obj in sorted(self.existing_assets.items()):
            relative = Path(target.removeprefix("/Game/"))
            stem = ROOT / "Content" / relative
            require(stem.with_suffix(".uasset").is_file(), "Target must be saved before backup: " + target)
            records[target] = dict(owner=lib.get_metadata_tag(obj, OWNER_TAG),
                revision=lib.get_metadata_tag(obj, REVISION_TAG), class_name=obj.get_class().get_name(), files=[])
            for suffix in (".uasset", ".uexp", ".ubulk", ".uptnl"):
                source = stem.with_suffix(suffix)
                if not source.is_file():
                    continue
                destination = directory / "Content" / relative.with_suffix(suffix)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
                checksum = sha256(source)
                require(sha256(destination) == checksum, "Backup checksum failed: " + str(source))
                records[target]["files"].append(dict(source=str(source), backup=str(destination), sha256=checksum))
        shutil.copy2(MANIFEST_FILE, directory / "manifest_v2.json")
        require(sha256(directory / "manifest_v2.json") == self.manifest_sha, "Manifest backup checksum failed")
        before = dict(revision=self.revision, manifest_sha256=self.manifest_sha, packages=records,
                      planned_targets=sorted(self.targets), saved_referencers_before=self.report["saved_referencers_before"],
                      dirty_before=sorted(self.dirty_before), restore="Close/reload Editor before restoring saved package bytes; no automatic live rollback.")
        (directory / "Before.json").write_text(json.dumps(before, indent=2), encoding="utf-8")
        self.report.update(backup_directory=str(directory), backup_manifest=str(directory / "Before.json"))
        self.backup_complete = True

    def track(self, obj, filename=None):
        require(self.backup_complete and obj and package(obj) in self.targets, "Mutation outside the backed-up migration plan")
        target = package(obj)
        require(isinstance(obj, self.targets[target]), "Unexpected generated asset type: " + target)
        if target in self.existing_assets:
            require(self.acceptable(obj), "Preserved custom target owner: " + target)
        previous = lib.get_metadata_tag(obj, OWNER_TAG)
        if previous == V1_OWNER_VALUE:
            lib.set_metadata_tag(obj, PREVIOUS_OWNER_TAG, previous)
            if target not in self.report["migrated_v1_packages"]:
                self.report["migrated_v1_packages"].append(target)
        lib.set_metadata_tag(obj, OWNER_TAG, OWNER_VALUE)
        lib.set_metadata_tag(obj, REVISION_TAG, self.revision)
        lib.set_metadata_tag(obj, MANIFEST_TAG, self.manifest_sha)
        if filename is not None:
            lib.set_metadata_tag(obj, SOURCE_TAG, sha256(filename))
        self.assets[target] = obj
        return obj

    def import_file(self, filename, target, kind, options=None, mesh=False):
        require(self.backup_complete and filename.is_file() and target in self.targets, "Unplanned source/target import")
        if mesh and target in self.existing_assets:
            # UE5.8 EditorFactories.cpp, UReimportFbxSkeletalMeshFactory::Reimport,
            # assigns ReimportUI->SkeletalMeshImportData = existing ImportData
            # before ApplyImportUIToImportOptions. Task.Options' new data is
            # therefore not the effective source for an existing mesh reimport.
            existing = self.existing_assets[target]
            data = existing.get_editor_property("asset_import_data")
            require(isinstance(data, u.FbxSkeletalMeshImportData), "Existing mesh has no legacy FBX import data")
            requested = options.get_editor_property("skeletal_mesh_import_data")
            names = ("update_skeleton_reference_pose", "use_t0_as_ref_pose", "reorder_material_to_fbx_order",
                     "import_uniform_scale", "convert_scene", "convert_scene_unit", "force_front_x_axis",
                     "normal_import_method", "import_morph_targets")
            for name in names:
                data.set_editor_property(name, requested.get_editor_property(name))
            # A prior geometry-only import would explicitly disable skeleton
            # pose updates. Set both fields because Reimport uses LastImportContentType.
            data.set_editor_property("import_content_type", u.FBXImportContentType.FBXICT_ALL)
            self.report["effective_existing_mesh_import_data"] = dict(
                update_skeleton_reference_pose=bool(data.get_editor_property("update_skeleton_reference_pose")),
                use_t0_as_ref_pose=bool(data.get_editor_property("use_t0_as_ref_pose")),
                reorder_material_to_fbx_order=bool(data.get_editor_property("reorder_material_to_fbx_order")),
                content_type=str(data.get_editor_property("import_content_type")),
                basis="UE5.8 existing mesh AssetImportData, assigned by Reimport factory before ApplyImportUIToImportOptions")
            require(self.report["effective_existing_mesh_import_data"]["update_skeleton_reference_pose"],
                    "Effective reimport data did not enable skeleton rest migration")
        task = u.AssetImportTask()
        destination, name = target.rsplit("/", 1)
        task.set_editor_properties(dict(filename=str(filename), destination_path=destination, destination_name=name,
            automated=True, save=False, replace_existing=True, replace_existing_settings=True))
        if options is not None:
            task.factory = u.FbxFactory()
            task.options = options
        tools.import_asset_tasks([task])
        paths = list(task.imported_object_paths)
        objects = [lib.load_asset(p) for p in paths]
        allowed = {target, SKELETON_PATH} if mesh else {target}
        require(objects and all(obj and package(obj) in allowed for obj in objects),
                "Unexpected import name/package; no rename/delete attempted: " + str(paths))
        for obj in objects:
            self.track(obj, filename)
        result = lib.load_asset(target)
        require(isinstance(result, kind), "Expected " + kind.__name__ + " at " + target)
        self.report["imported"].append(dict(asset=target, source=str(filename.relative_to(ROOT)), objects=paths))
        return self.track(result, filename)

    def textures(self):
        result = {}
        for target, record in self.texture_specs.items():
            filename = Path(record["file"])
            header = filename.read_bytes()[:24]
            require(header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR", "Expected PNG: " + str(filename))
            size = list(struct.unpack(">II", header[16:24]))
            require(all(0 < value <= 8192 for value in size) and (not record["size"] or size == record["size"]),
                    "Baked texture dimensions differ: " + str(filename))
            texture = self.import_file(filename, target, u.Texture2D)
            texture.set_editor_properties(dict(srgb=record["role"] == "BaseColor", max_texture_size=max(size),
                compression_settings=u.TextureCompressionSettings.TC_DEFAULT if record["role"] == "BaseColor"
                else u.TextureCompressionSettings.TC_GRAYSCALE))
            result[target] = texture
            self.report["textures"].append(dict(asset=target, size=size, srgb=record["role"] == "BaseColor"))
        return result

    def materials(self, textures):
        result = {}
        for name, spec in self.manifest["materials"].items():
            target = DEST + "/" + name
            material = self.existing_assets.get(target)
            signature = hashlib.sha256(json.dumps(spec, sort_keys=True).encode("utf-8")).hexdigest()
            reuse = material and lib.get_metadata_tag(material, OWNER_TAG) == OWNER_VALUE and (
                lib.get_metadata_tag(material, GRAPH_TAG) == signature and lib.get_metadata_tag(material, BUILD_TAG) == "ready")
            if material is None:
                material = tools.create_asset(name, DEST, u.Material, u.MaterialFactoryNew())
            self.track(material)
            if not reuse:
                lib.set_metadata_tag(material, BUILD_TAG, "building")
                # v1 proved the bulk-delete helper can skip a live collection;
                # delete a snapshot and verify it is empty before rebuilding.
                for expression in list(edit.get_material_expressions(material)):
                    edit.delete_material_expression(material, expression)
                require(not edit.get_material_expressions(material), "Material graph reset was incomplete: " + target)
                coat = spec.get("coat", 0.)
                material.set_editor_properties(dict(blend_mode=u.BlendMode.BLEND_OPAQUE, two_sided=False,
                    use_material_attributes=True, shading_model=u.MaterialShadingModel.MSM_CLEAR_COAT if coat > 0
                    else u.MaterialShadingModel.MSM_DEFAULT_LIT))
                attributes = self.node(material, u.MaterialExpressionMakeMaterialAttributes, -100, 0, desc="CoffeeSlimeV2.PBR")
                for index, role in enumerate(("BaseColor", "Roughness")):
                    entry = spec.get("textures", {}).get(role)
                    if entry:
                        entry = dict(file=entry) if isinstance(entry, str) else entry
                        filename = source_file(entry["file"])
                        matches = [path for path, record in self.texture_specs.items() if record["file"] == str(filename) and record["role"] == role]
                        require(len(matches) == 1, "Ambiguous material texture: " + name + " " + role)
                        node = self.node(material, u.MaterialExpressionTextureSample, -650, index * 230,
                            texture=textures[matches[0]], sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR if role == "BaseColor"
                            else u.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, desc="CoffeeSlimeV2." + role)
                        output = "RGB" if role == "BaseColor" else "R"
                    elif role == "BaseColor":
                        node = self.node(material, u.MaterialExpressionConstant3Vector, -650, 0,
                            constant=u.LinearColor(*spec["color"], 1.), desc="CoffeeSlimeV2.BaseColor")
                        output = ""
                    else:
                        node = self.node(material, u.MaterialExpressionConstant, -650, 230,
                            r=spec["roughness"], desc="CoffeeSlimeV2.Roughness")
                        output = ""
                    if role == "BaseColor" and spec.get("color_gain", 1.) != 1.:
                        gain = self.node(material, u.MaterialExpressionMultiply, -350, 0,
                            const_b=spec["color_gain"], desc="CoffeeSlimeV2.BaseColorGain")
                        self.connect_input(node, output, gain, "A")
                        node, output = gain, ""
                    self.connect_input(node, output, attributes, role)
                    if role == "BaseColor" and spec.get("emissive_gain", 0.) > 0.:
                        emission = self.node(material, u.MaterialExpressionMultiply, -350, -180,
                            const_b=spec["emissive_gain"], desc="CoffeeSlimeV2.EmissiveGain")
                        self.connect_input(node, output, emission, "A")
                        self.connect_input(emission, "", attributes, "EmissiveColor")
                for index, (pin, value) in enumerate((("Metallic", 0.), ("Specular", spec.get("specular", .5)),
                    ("ClearCoat", coat), ("ClearCoatRoughness", spec.get("coat_roughness", .1)))):
                    scalar = self.node(material, u.MaterialExpressionConstant, -650, 490 + index * 90,
                                       r=value, desc="CoffeeSlimeV2." + pin)
                    self.connect_input(scalar, "", attributes, pin)
                require(edit.connect_material_property(attributes, "", u.MaterialProperty.MP_MATERIAL_ATTRIBUTES), "Cannot connect PBR attributes")
                edit.set_base_material_usage(material, u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
                errors = list(edit.recompile_material(material) or [])
                require(not errors, "Material compile failed: " + name + " " + str(errors))
                lib.set_metadata_tag(material, GRAPH_TAG, signature)
                lib.set_metadata_tag(material, BUILD_TAG, "ready")
            require(isinstance(edit.get_material_property_input_node(material, u.MaterialProperty.MP_MATERIAL_ATTRIBUTES),
                               u.MaterialExpressionMakeMaterialAttributes), "Missing owned PBR graph: " + name)
            require(material.get_editor_property("blend_mode") == u.BlendMode.BLEND_OPAQUE
                    and not material.get_editor_property("two_sided")
                    and material.get_editor_property("use_material_attributes")
                    and material.get_editor_property("shading_model") == (
                        u.MaterialShadingModel.MSM_CLEAR_COAT if spec.get("coat", 0.) > 0.
                        else u.MaterialShadingModel.MSM_DEFAULT_LIT)
                    and edit.has_material_usage(material, u.MaterialUsage.MATUSAGE_SKELETAL_MESH),
                    "Unexpected modified owned material settings: " + name)
            result[name] = material
            self.report["materials"].append(dict(asset=target, reused_graph=bool(reuse), spec=spec))
        return result

    @staticmethod
    def fbx_options(mesh, skeleton=None):
        options = V1.fbx_options(mesh, skeleton)
        if mesh:
            options.set_editor_property("reset_to_fbx_on_material_conflict", True)
            data = options.get_editor_property("skeletal_mesh_import_data")
            # UE5.8: mesh rest always updates; skeleton rest requires this flag.
            data.set_editor_property("update_skeleton_reference_pose", True)
            data.set_editor_property("reorder_material_to_fbx_order", True)
        return options

    def reference_pose(self, mesh, skeleton):
        require(package(skeleton) == SKELETON_PATH, "Mesh switched to an unexpected skeleton")
        pose = u.AnimPoseExtensions.get_reference_pose(skeleton)
        require(u.AnimPoseExtensions.is_valid(pose), "Invalid updated skeleton rest pose")
        names = set(map(str, u.AnimPoseExtensions.get_bone_names(pose)))
        require(names == set(BONE_PARENTS), "Extra/missing FBX bones: " + str(names))
        actual = {}
        tolerance = self.manifest.get("integration", {}).get("rest_position_tolerance_cm", .5)
        # Read the mesh's own local rest transforms independently from the
        # updated USkeleton. This transient, unregistered component creates no
        # actor/package and does not evaluate or play an animation.
        component = u.new_object(u.SkeletalMeshComponent)
        component.set_skeletal_mesh_asset(mesh)
        for name, parent in BONE_PARENTS.items():
            found_parent = str(u.SkeletalMeshEditorSubsystem.get_bone_parent(mesh, name))
            require((None if found_parent == "None" else found_parent) == parent, "Bone parent changed: " + name)
            actual[name] = self.transform_dict(u.AnimPoseExtensions.get_bone_pose(pose, name, u.AnimPoseSpaces.WORLD))
            require(math.dist(actual[name]["translation_cm"], self.manifest["skeleton"]["reference_positions_cm"][name]) <= tolerance,
                    "Skeleton still has stale/unexpected v1 rest position: " + name + " " + str(actual[name]["translation_cm"]))
            require(max(abs(value - 1.) for value in actual[name]["scale"]) < .001, "Non-unit reference bone: " + name)
            index = component.get_bone_index(name)
            require(index >= 0, "Missing mesh reference bone: " + name)
            mesh_local = self.transform_dict(component.get_ref_pose_transform(index))
            skeleton_local = self.transform_dict(u.AnimPoseExtensions.get_bone_pose(pose, name, u.AnimPoseSpaces.LOCAL))
            require(math.dist(mesh_local["translation_cm"], skeleton_local["translation_cm"]) < .01
                    and max(abs(a - b) for a, b in zip(mesh_local["scale"], skeleton_local["scale"])) < .001
                    and quat_angle(mesh_local["quaternion_xyzw"], skeleton_local["quaternion_xyzw"]) < .05,
                    "Mesh and skeleton reference transforms differ: " + name)
        return actual

    def sample_clip(self, sequence, mesh, record, reference):
        require(sequence.get_editor_property("additive_anim_type") == u.AdditiveAnimationType.AAT_NONE,
                "Native SingleNode playback requires a non-additive clip: " + sequence.get_name())
        # Reimport can retain per-sequence retarget settings. Use the updated
        # skeleton's default pose, with no named or cached v1 source mesh pose.
        # ClearRetargetSourceAsset does not clear its cached transform array by
        # itself in UE5.8; UpdateRetargetSourceAssetData is required as well.
        sequence.set_editor_property("retarget_source", u.Name("None"))
        sequence.clear_retarget_source_asset()
        sequence.update_retarget_source_asset_data()
        sequence.set_editor_properties(dict(enable_root_motion=False, force_root_lock=False))
        options = u.AnimPoseEvaluationOptions()
        options.set_editor_properties(dict(optional_skeletal_mesh=mesh, evaluation_type=u.AnimDataEvalType.SOURCE,
            should_retarget=False, extract_root_motion=False, incorporate_root_motion_into_pose=True))
        runtime = u.AnimPoseEvaluationOptions()
        runtime.set_editor_properties(dict(optional_skeletal_mesh=mesh, evaluation_type=u.AnimDataEvalType.RAW,
            should_retarget=True, extract_root_motion=False, incorporate_root_motion_into_pose=True))
        samples = []
        for frame in sorted(set((record["frame_start"], record["frame_end"], *record["critical_frames"]))):
            seconds = (frame - record["frame_start"]) / record["fps"]
            pose = u.AnimPoseExtensions.get_anim_pose_at_time(sequence, seconds, options)
            retargeted = u.AnimPoseExtensions.get_anim_pose_at_time(sequence, seconds, runtime)
            require(u.AnimPoseExtensions.is_valid(pose) and u.AnimPoseExtensions.is_valid(retargeted), "Invalid evaluated clip pose")
            require(all(set(map(str, u.AnimPoseExtensions.get_bone_names(value))) == set(BONE_PARENTS)
                        for value in (pose, retargeted)), "Clip pose has extra/missing bones")
            bones = {}
            for name in BONE_PARENTS:
                bones[name] = self.transform_dict(u.AnimPoseExtensions.get_bone_pose(pose, name, u.AnimPoseSpaces.WORLD))
                other = self.transform_dict(u.AnimPoseExtensions.get_bone_pose(retargeted, name, u.AnimPoseSpaces.WORLD))
                require(math.dist(bones[name]["translation_cm"], other["translation_cm"]) < .1
                        and max(abs(a - b) for a, b in zip(bones[name]["scale"], other["scale"])) < .001
                        and quat_angle(bones[name]["quaternion_xyzw"], other["quaternion_xyzw"]) < .05,
                        "Retargeted Raw pose differs from v2 source: " + name)
            root = bones["root"]
            require(math.dist(root["translation_cm"], reference["root"]["translation_cm"]) < .05
                    and max(abs(v - 1.) for v in root["scale"]) < .001
                    and quat_angle(root["quaternion_xyzw"], reference["root"]["quaternion_xyzw"]) < .05,
                    "Raw root differs from unit stationary v2 reference")
            samples.append(dict(frame=frame, seconds=seconds, bones=bones))
        heights = [sample["bones"]["gel_body"]["translation_cm"][2] for sample in samples]
        if sequence.get_name() == "A_CoffeeSlime_Hop":
            require(max(heights) - min(heights) >= record["min_gel_body_lift_cm"], "Imported Hop lost authored body lift")
        sequence.set_editor_properties(dict(force_root_lock=True, root_motion_root_lock=u.RootMotionRootLock.REF_POSE))
        sequence.set_preview_skeletal_mesh(mesh)
        return dict(samples=samples, gel_body_z_range_cm=[min(heights), max(heights)],
                    basis="Source before root lock plus retargeted Raw comparison; not a render or compressed-data proof")

    def mesh_and_clips(self, materials):
        old_mesh = self.existing_assets.get(MESH_PATH)
        old_skeleton = old_mesh.get_editor_property("skeleton") if old_mesh else self.existing_assets.get(SKELETON_PATH)
        mesh = self.import_file(source_file(self.manifest["mesh_file"]), MESH_PATH, u.SkeletalMesh,
                                self.fbx_options(True, old_skeleton), mesh=True)
        skeleton = mesh.get_editor_property("skeleton")
        require(skeleton and (not old_skeleton or skeleton == old_skeleton), "Existing skeleton identity changed")
        self.track(skeleton, source_file(self.manifest["mesh_file"]))
        reference = self.reference_pose(mesh, skeleton)
        require(mesh.get_editor_property("physics_asset") is None, "Native sphere collision must remain independent")
        bounds = mesh.get_bounds()
        dimensions = [float(value) * 2. for value in bounds.box_extent.to_tuple()]
        expected = [value * 100. for value in self.manifest["expected_dimensions_m"]]
        tolerance = self.manifest.get("integration", {}).get("dimensions_tolerance_fraction", .05)
        require(all(abs(a - b) <= tolerance * b for a, b in zip(dimensions, expected)), "Imported dimensions differ from v2 manifest")
        require(abs(bounds.origin.z - bounds.box_extent.z) < .5, "Imported mesh lost floor pivot")
        slots = list(mesh.get_editor_property("materials"))
        labels = [str(slot.get_editor_property("imported_material_slot_name")) for slot in slots]
        require(labels == self.manifest["material_slots"], "Old/stale/incorrect imported material slots: " + str(labels))
        for slot, label in zip(slots, labels):
            slot.set_editor_property("material_interface", materials[label])
        mesh.set_editor_property("materials", slots)
        subsystem = u.get_editor_subsystem(u.SkeletalMeshEditorSubsystem)
        sections = []
        for lod in range(u.SkeletalMeshEditorSubsystem.get_lod_count(mesh)):
            count = subsystem.get_num_sections(mesh, lod)
            require(count > 0, "Imported LOD has no render sections")
            for section in range(count):
                index = subsystem.get_lod_material_slot(mesh, lod, section)
                require(0 <= index < len(slots), "LOD retains an out-of-range v1 material section")
                sections.append(dict(lod=lod, section=section, material_index=index, slot=labels[index]))
        require(sections, "No verified material sections")
        self.report.update(mesh=MESH_PATH, skeleton=SKELETON_PATH, actual_reference_pose=reference,
                           rest_dimensions_cm=dimensions, actual_material_slots=labels, lod_sections=sections,
                           material_assignments={label: package(materials[label]) for label in labels},
                           clip_retarget_basis="Updated default skeleton pose; named source and cached source-mesh pose cleared")
        # Never reuse a source-hash-matching v1 clip after its skeleton rest was
        # changed. The mesh and all five clips migrate as one checked batch.
        for name, record in self.manifest["clips"].items():
            sequence = self.import_file(source_file(record["file"]), CLIP_PATHS[name], u.AnimSequence,
                                        self.fbx_options(False, skeleton))
            require(sequence.get_editor_property("skeleton") == skeleton, "Clip uses another skeleton: " + name)
            duration = float(sequence.get_play_length())
            span = int(u.AnimationLibrary.get_num_frames(sequence))
            require(abs(duration - record["duration_seconds"]) < .005 and span == record["frame_end"] - record["frame_start"],
                    "Imported clip duration/100Hz span differs: " + name)
            poses = self.sample_clip(sequence, mesh, record, reference)
            self.report["clips"].append(dict(asset=CLIP_PATHS[name], duration_seconds=duration, frame_span=span, **poses))
        require(self.reference_pose(mesh, skeleton) == reference, "Clip import changed the updated v2 reference pose")
        u.AnimationLibrary.set_skeleton_preview_mesh(skeleton, mesh)

    def save(self):
        require(dirty_packages() - self.dirty_before <= set(self.assets), "Import dirtied unplanned packages")
        for target, obj in sorted(self.assets.items()):
            require(self.acceptable(obj) and lib.get_metadata_tag(obj, REVISION_TAG) == self.revision,
                    "Refusing to save an unowned/wrong revision package: " + target)
            require(lib.save_loaded_asset(obj, only_if_is_dirty=False), "Could not save migrated package: " + target)
            self.report["saved_packages"].append(target)
        self.report["dirty_after"] = sorted(dirty_packages())
        require(dirty_packages() == self.dirty_before, "Migration left dirty targets or changed unrelated dirty state")


def main():
    report = dict(status="planning", passed=False, imported=[], textures=[], materials=[], clips=[], saved_packages=[],
                  migrated_v1_packages=[], preserved_unowned=[], saved_referencers_before={},
                  scope="Asset-only v2 migration at existing native paths; no geometry authoring, native build, runtime or PIE actions",
                  limitations=["Package backups support manual recovery; a multi-package save is not atomic.",
                               "Saved reference queries do not enumerate transient/live code references.",
                               "Technical pose/import gates do not replace ROOT's concept and rendered visual approval."])
    importer = None
    try:
        if not MANIFEST_FILE.is_file():
            report["status"] = "awaiting_v2_manifest"
            return
        manifest = json.loads(MANIFEST_FILE.read_text(encoding="utf-8"))
        importer = CoffeeV2Importer(report, manifest)
        ready = importer.validate_manifest()
        apply = globals().get("MC_COFFEE_V2_APPLY_REVISION") == manifest["revision"]
        report["mode"] = "apply" if apply else "plan"
        if not ready:
            require(not apply, "Missing v2 export files; no package changes made")
            report["status"] = "awaiting_v2_exports"
            return
        importer.preflight()
        if not apply:
            report["status"] = "planned_no_mutations"
            report["apply_after_root_visual_gate"] = dict(MC_COFFEE_V2_APPLY_REVISION=manifest["revision"])
            return
        importer.backup()
        textures = importer.textures()
        materials = importer.materials(textures)
        cvar = "Interchange.FeatureFlags.Import.FBX"
        previous = u.SystemLibrary.get_console_variable_int_value(cvar)
        report["interchange_fbx_before"] = previous
        try:
            u.SystemLibrary.execute_console_command(None, cvar + " 0")
            require(u.SystemLibrary.get_console_variable_int_value(cvar) == 0, "Could not select legacy FBX importer")
            importer.mesh_and_clips(materials)
        finally:
            u.SystemLibrary.execute_console_command(None, cvar + " " + str(previous))
            report["interchange_fbx_restored"] = u.SystemLibrary.get_console_variable_int_value(cvar) == previous
            require(report["interchange_fbx_restored"], "Could not restore FBX Interchange flag")
        importer.save()
        report.update(status="passed", passed=True)
    except Exception:
        report.update(status="failed", passed=False, error=traceback.format_exc(),
                      touched_packages=sorted(set(importer.assets) | set(
                          importer.resume_failure.get("touched_packages", []) if importer.resume_failure else []))
                          if importer else [], dirty_after_failure=sorted(dirty_packages()))
        raise
    finally:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_text(json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False), encoding="utf-8")
        u.log("MC_COFFEE_SLIME_V2_" + report["status"].upper() + " " + str(OUTPUT))


if __name__ == "__main__":
    main()
