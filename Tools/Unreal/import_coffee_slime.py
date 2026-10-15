"""Import the owned CoffeeSlime mesh, five baked clips and six PBR materials.

Run once in an idle, clean MessControl Editor after source export validation:
    py "E:/DEVGAME/MessControl/Tools/Unreal/import_coffee_slime.py"
No map, Blueprint, controller or native default is edited. Only explicitly
tracked assets under /Game/Cataclysm/Jelly/Coffee are saved, after all gates.
The FBX Interchange flag is restored even when an import or gate fails.
"""
import hashlib
import json
import math
import struct
import traceback
from pathlib import Path

import unreal as u


ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = ROOT / "ArtSource/CoffeeSlime/v1"
EXPORTS = Path(globals().get("MC_COFFEE_EXPORT_FOLDER", SOURCE / "exports")).resolve()
DEST = "/Game/Cataclysm/Jelly/Coffee"
ANIM_DEST = DEST + "/Animations"
TEXTURE_DEST = DEST + "/Textures"
OUTPUT = ROOT / "Artifacts/CoffeeSlime_20261015/ImportReport.json"
OWNER_TAG = "MCCoffeeSlimeGenerated"
OWNER_VALUE = "import_coffee_slime.py:1"
SOURCE_TAG = "MCCoffeeSlimeSourceSHA256"
PIPELINE_TAG = "MCCoffeeSlimeImportSettings"
PIPELINE_VALUE = "legacy_fbx_meters_scale1_100hz:1"
GRAPH_TAG = "MCCoffeeSlimeMaterialSpecSHA256"
BUILD_TAG = "MCCoffeeSlimeMaterialBuild"
CLIP_DURATIONS = dict(Idle=1.2, Hop=.9, Hurt=.28, PreBurst=.6, Burst=.18)
BONE_PARENTS = dict(root=None, gel_body="root", gel_top="gel_body",
                    face="gel_body", foam_tip="gel_top")
MATERIALS = {
    "M_CoffeeGel": dict(color=(.075, .025, .009), roughness=.20, coat=.12, coat_roughness=.1,
                        baked=True, color_gain=2., specular=.3),
    "M_MilkFoam": dict(color=(.8, .68, .43), roughness=.46, coat=0., coat_roughness=.1, baked=True),
    "M_Ivory": dict(color=(.88, .83, .68), roughness=.28, coat=.15, coat_roughness=.1),
    "M_MauveLid": dict(color=(.26, .12, .15), roughness=.36, coat=0., coat_roughness=.1),
    "M_EspressoDark": dict(color=(.006, .003, .002), roughness=.16, coat=.23, coat_roughness=.1),
    "M_CaramelLip": dict(color=(.23, .08, .02), roughness=.27, coat=.25, coat_roughness=.1),
}
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
tools = u.AssetToolsHelpers.get_asset_tools()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def package(obj):
    return obj.get_path_name().split(".")[0]


def owned(obj):
    return obj and package(obj).startswith(DEST + "/") and lib.get_metadata_tag(obj, OWNER_TAG) == OWNER_VALUE


def digest(filename):
    filename = filename.resolve()
    require(filename.is_relative_to(SOURCE.resolve()) and filename.is_file(),
            "Missing or out-of-scope source file: " + str(filename))
    return hashlib.sha256(filename.read_bytes()).hexdigest()


def dirty_packages():
    packages = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    packages += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return sorted({p.get_name() for p in packages})


def write_report(report):
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False), encoding="utf-8")


class CoffeeImporter:
    def __init__(self, report):
        self.report = report
        self.assets = {}
        self.before_paths = set()

    def track(self, obj, filename=None):
        require(obj and package(obj).startswith(DEST + "/"), "Unexpected asset outside owned destination")
        target = package(obj)
        if target in self.before_paths:
            require(owned(obj), "Preserved preexisting custom asset: " + target)
        lib.set_metadata_tag(obj, OWNER_TAG, OWNER_VALUE)
        if filename is not None:
            lib.set_metadata_tag(obj, SOURCE_TAG, digest(filename))
            lib.set_metadata_tag(obj, PIPELINE_TAG, PIPELINE_VALUE)
        self.assets[target] = obj
        return obj

    def existing(self, target, kind):
        if not lib.does_asset_exist(target):
            return None
        obj = lib.load_asset(target)
        require(isinstance(obj, kind) and owned(obj), "Preserved unexpected type/custom asset: " + target)
        return obj

    def preflight(self):
        require(Path(u.Paths.get_project_file_path()).name == "MessControl.uproject" and ROOT.name == "MessControl",
                "Run only in the MessControl Editor")
        require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), "Finish PIE before importing")
        self.report["dirty_before"] = dirty_packages()
        if self.report["dirty_before"]:
            require(globals().get("MC_COFFEE_RESUME_OWNED", False) and OUTPUT.is_file(),
                    "Preserve unsaved packages before import: " + ", ".join(self.report["dirty_before"]))
            previous = json.loads(OUTPUT.read_text(encoding="utf-8"))
            require(previous.get("status") == "failed"
                    and set(self.report["dirty_before"]) <= set(previous.get("touched_packages", [])),
                    "Recovery may only continue packages touched by this failed import")
            require(all(owned(lib.load_asset(p)) for p in self.report["dirty_before"]),
                    "Recovery found an unowned unsaved package")
            self.report["resumed_owned_failure"] = previous.get("error")
        for path in lib.list_assets(DEST, recursive=True, include_folder=False):
            obj = lib.load_asset(path)
            require(owned(obj), "Preserved preexisting unowned/custom package: " + path)
            self.before_paths.add(package(obj))
        manifest_file = SOURCE / "manifest.json"
        manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
        require(manifest.get("asset") == "CoffeeSlime" and manifest.get("units") == "meters"
                and manifest.get("forward") == "+X" and manifest.get("up") == "+Z" and manifest.get("pivot") == "floor",
                "Manifest does not match the agreed meter/floor/+X/+Z contract")
        require(manifest["skeleton"]["bone_parents"] == BONE_PARENTS, "Unexpected source bone hierarchy")
        require(manifest["material_slots"] == list(MATERIALS), "Expected six ordered material slots")
        require(set(manifest["clips"]) == {"A_CoffeeSlime_" + role for role in CLIP_DURATIONS}, "Expected exactly five clips")
        sources = [EXPORTS / "SK_CoffeeSlime.fbx"]
        for role, seconds in CLIP_DURATIONS.items():
            name = "A_CoffeeSlime_" + role
            clip = manifest["clips"][name]
            require(clip["fps"] == 100 and abs(clip["duration_seconds"] - seconds) < 1.e-6
                    and clip["frame_end"] - clip["frame_start"] == round(seconds * 100),
                    "Unexpected 100Hz clip timing: " + name)
            sources.append(EXPORTS / (name + ".fbx"))
        for name in ("M_CoffeeGel", "M_MilkFoam"):
            for role in ("BaseColor", "Roughness"):
                filename = SOURCE / "textures" / (name + "_" + role + ".png")
                header = filename.read_bytes()[:24]
                require(header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR"
                        and struct.unpack(">II", header[16:24]) == (512, 512), "Expected baked 512px PNG: " + str(filename))
                sources.append(filename)
        self.report.update(manifest_sha256=digest(manifest_file),
                           source_sha256={str(p.relative_to(ROOT)): digest(p) for p in sources},
                           preexisting_owned_packages=sorted(self.before_paths))
        return manifest

    def import_file(self, filename, destination, name, kind, options=None, allowed_types=None):
        target = destination + "/" + name
        obj = self.existing(target, kind)
        sha = digest(filename)
        if obj and lib.get_metadata_tag(obj, SOURCE_TAG) == sha and lib.get_metadata_tag(obj, PIPELINE_TAG) == PIPELINE_VALUE:
            data = obj.get_editor_property("asset_import_data")
            if Path(data.get_first_filename()).resolve() != filename.resolve():
                data.scripted_add_filename(str(filename.resolve()), 0, "")
                self.report.setdefault("repointed_sources", []).append(target)
            self.track(obj)
            self.report["skipped_unchanged"].append(target)
            return obj
        task = u.AssetImportTask()
        task.set_editor_properties(dict(filename=str(filename.resolve()), destination_path=destination,
            destination_name=name, automated=True, replace_existing=True,
            replace_existing_settings=True, save=False))
        if options is not None:
            task.factory = u.FbxFactory()
            task.options = options
        tools.import_asset_tasks([task])
        paths = list(task.imported_object_paths)
        objects = [lib.load_asset(p) for p in paths]
        require(objects and all(objects), "Import returned no usable assets: " + str(filename))
        allowed_types = allowed_types or (kind,)
        require(all(isinstance(o, allowed_types) and package(o).startswith(DEST + "/") for o in objects),
                "Import produced unexpected packages/types: " + str(paths))
        matches = [o for o in objects if isinstance(o, kind)]
        require(len(matches) == 1, "Expected one " + kind.__name__ + " from " + str(filename))
        for produced in objects:
            self.track(produced, filename)
        obj = matches[0]
        if package(obj) != target:
            require(not lib.does_asset_exist(target), "Unexpected import name collides with target: " + target)
            old_path = package(obj)
            require(lib.rename_asset(old_path, target), "Could not normalize imported asset name: " + target)
            self.assets.pop(old_path, None)
            obj = lib.load_asset(target)
            self.track(obj, filename)
        self.report["imported"].append(dict(asset=target, source=str(filename.relative_to(ROOT)), objects=paths))
        return obj

    def textures(self):
        result = {}
        for name in ("M_CoffeeGel", "M_MilkFoam"):
            result[name] = {}
            for role in ("BaseColor", "Roughness"):
                filename = SOURCE / "textures" / (name + "_" + role + ".png")
                asset_name = "T_" + name[2:] + "_" + role
                texture = self.import_file(filename, TEXTURE_DEST, asset_name, u.Texture2D)
                texture.set_editor_properties(dict(srgb=role == "BaseColor", max_texture_size=512,
                    compression_settings=u.TextureCompressionSettings.TC_DEFAULT if role == "BaseColor"
                    else u.TextureCompressionSettings.TC_GRAYSCALE))
                result[name][role] = texture
                self.report["textures"].append(dict(asset=texture.get_path_name(), source=str(filename.relative_to(ROOT)),
                    srgb=bool(texture.get_editor_property("srgb")), compression=str(texture.get_editor_property("compression_settings"))))
        return result

    @staticmethod
    def node(material, kind, x, y, **properties):
        expression = edit.create_material_expression(material, kind, x, y)
        require(expression, "Could not create " + kind.__name__)
        expression.set_editor_properties(properties)
        return expression

    @staticmethod
    def connect_input(source, output, target, requested_pin):
        normalize = lambda text: "".join(c for c in str(text).lower() if c.isalnum())
        matches = [str(pin) for pin in edit.get_material_expression_input_names(target)
                   if normalize(pin) == normalize(requested_pin)]
        require(len(matches) == 1 and edit.connect_material_expressions(source, output, target, matches[0]),
                "Could not connect material pin " + requested_pin)

    def materials(self, textures):
        result = {}
        for name, spec in MATERIALS.items():
            signature = hashlib.sha256(json.dumps(spec, sort_keys=True).encode("utf-8")).hexdigest()
            material = self.existing(DEST + "/" + name, u.Material)
            new = material is None
            if new:
                material = tools.create_asset(name, DEST, u.Material, u.MaterialFactoryNew())
                self.track(material)
            else:
                legacy = dict(color=(.075, .025, .009), roughness=.20, coat=.4, coat_roughness=.1, baked=True)
                legacy_signature = hashlib.sha256(json.dumps(legacy, sort_keys=True).encode("utf-8")).hexdigest()
                if (name == "M_CoffeeGel" and globals().get("MC_COFFEE_POLISH_OWNED", False)
                        and lib.get_metadata_tag(material, GRAPH_TAG) in (legacy_signature, signature)
                        and lib.get_metadata_tag(material, BUILD_TAG) == "ready"):
                    # Explicit migration of this run's generated graph; preserve
                    # every custom/unknown version and all other five materials.
                    expressions = list(edit.get_material_expressions(material))
                    require(7 <= len(expressions) <= 11 and all(
                        str(n.get_editor_property("desc")).startswith("CoffeeSlime") for n in expressions),
                        "Preserved modified legacy coffee graph")
                    # Delete from a snapshot: this engine's bulk helper can
                    # skip expressions while removing from its live collection.
                    for expression in expressions:
                        edit.delete_material_expression(material, expression)
                    require(not edit.get_material_expressions(material), "Coffee graph deletion was incomplete")
                    new = True
                require(lib.get_metadata_tag(material, GRAPH_TAG) == signature
                        and lib.get_metadata_tag(material, BUILD_TAG) == "ready" or new,
                        "Preserved custom/incomplete material graph: " + material.get_path_name())
                self.track(material)
            shading = u.MaterialShadingModel.MSM_CLEAR_COAT if spec["coat"] > 0. else u.MaterialShadingModel.MSM_DEFAULT_LIT
            if new:
                lib.set_metadata_tag(material, BUILD_TAG, "building")
                material.set_editor_properties(dict(blend_mode=u.BlendMode.BLEND_OPAQUE,
                    shading_model=shading, two_sided=False, use_material_attributes=True))
                attributes = self.node(material, u.MaterialExpressionMakeMaterialAttributes, -100, 0,
                                       desc="CoffeeSlime owned PBR attributes")
                if spec.get("baked"):
                    color = self.node(material, u.MaterialExpressionTextureSample, -600, -160,
                        texture=textures[name]["BaseColor"], sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR,
                        desc="CoffeeSlime.BaseColor")
                    rough = self.node(material, u.MaterialExpressionTextureSample, -600, 90,
                        texture=textures[name]["Roughness"], sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
                        desc="CoffeeSlime.Roughness")
                    color_output, rough_output = "RGB", "R"
                else:
                    color = self.node(material, u.MaterialExpressionConstant3Vector, -600, -160,
                        constant=u.LinearColor(*spec["color"], 1.), desc="CoffeeSlime.BaseColor")
                    rough = self.node(material, u.MaterialExpressionConstant, -600, 90,
                        r=spec["roughness"], desc="CoffeeSlime.Roughness")
                    color_output = rough_output = ""
                self.connect_input(color, color_output, attributes, "BaseColor")
                if spec.get("color_gain", 1.) != 1.:
                    gain = self.node(material, u.MaterialExpressionMultiply, -320, -160,
                                     const_b=spec["color_gain"], desc="CoffeeSlime.BaseColorGain")
                    self.connect_input(color, color_output, gain, "A")
                    self.connect_input(gain, "", attributes, "BaseColor")
                self.connect_input(rough, rough_output, attributes, "Roughness")
                for index, (pin, value) in enumerate((("Metallic", 0.), ("Specular", spec.get("specular", .5)),
                    ("ClearCoat", spec["coat"]), ("ClearCoatRoughness", spec["coat_roughness"]))):
                    scalar = self.node(material, u.MaterialExpressionConstant, -600, 320 + index * 90,
                                       r=value, desc="CoffeeSlime." + pin)
                    # MP_CustomData0/1 are hidden from Python. Named inputs on
                    # MakeMaterialAttributes expose the supported ClearCoat path.
                    self.connect_input(scalar, "", attributes, pin)
                require(edit.connect_material_property(attributes, "", u.MaterialProperty.MP_MATERIAL_ATTRIBUTES),
                        "Could not connect owned PBR material attributes")
                edit.set_base_material_usage(material, u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
                errors = list(edit.recompile_material(material))
                require(not errors, "Material compilation failed: " + name + " " + str(errors))
                lib.set_metadata_tag(material, GRAPH_TAG, signature)
                lib.set_metadata_tag(material, BUILD_TAG, "ready")
            require(material.get_editor_property("blend_mode") == u.BlendMode.BLEND_OPAQUE
                    and material.get_editor_property("shading_model") == shading
                    and material.get_editor_property("use_material_attributes")
                    and edit.has_material_usage(material, u.MaterialUsage.MATUSAGE_SKELETAL_MESH),
                    "Preserved unexpected custom material settings: " + name)
            attributes = edit.get_material_property_input_node(material, u.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
            require(isinstance(attributes, u.MaterialExpressionMakeMaterialAttributes), "Missing owned material attributes: " + name)
            expressions = list(edit.get_material_expressions(material))
            inputs = list(edit.get_inputs_for_material_expression(material, attributes))
            for role in ("BaseColor", "Roughness"):
                nodes = [n for n in expressions if n.get_editor_property("desc") == "CoffeeSlime." + role]
                require(len(nodes)==1, "Duplicate or missing material role: "+name+" "+role)
                if role == "BaseColor" and spec.get("color_gain", 1.) != 1.:
                    gain_nodes=[n for n in expressions if n.get_editor_property("desc") == "CoffeeSlime.BaseColorGain"]
                    require(len(gain_nodes)==1 and gain_nodes[0] in inputs and nodes[0] in
                            edit.get_inputs_for_material_expression(material,gain_nodes[0]),
                            "Missing coffee color calibration link")
                else:
                    require(len(nodes) == 1 and nodes[0] in inputs, "Preserved modified material graph: " + name + " " + role)
                if spec.get("baked"):
                    require(isinstance(nodes[0], u.MaterialExpressionTextureSample)
                            and nodes[0].get_editor_property("texture") == textures[name][role],
                            "Preserved modified baked material texture: " + name + " " + role)
            result[name] = material
            self.report["materials"].append(dict(asset=material.get_path_name(), new=new, **spec))
        return result

    @staticmethod
    def fbx_options(mesh, skeleton=None):
        options = u.FbxImportUI()
        kind = u.FBXImportType.FBXIT_SKELETAL_MESH if mesh else u.FBXImportType.FBXIT_ANIMATION
        options.set_editor_properties(dict(automated_import_should_detect_type=False, mesh_type_to_import=kind,
            original_import_type=kind, import_mesh=mesh, import_as_skeletal=True, import_animations=not mesh,
            import_materials=False, import_textures=False, create_physics_asset=False))
        if skeleton is not None:
            options.set_editor_property("skeleton", skeleton)
        data = options.get_editor_property("skeletal_mesh_import_data" if mesh else "anim_sequence_import_data")
        data.set_editor_properties(dict(import_uniform_scale=1., convert_scene=True, convert_scene_unit=True,
                                       force_front_x_axis=False))
        if mesh:
            data.set_editor_properties(dict(update_skeleton_reference_pose=False, use_t0_as_ref_pose=False,
                normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
                reorder_material_to_fbx_order=True, import_morph_targets=False))
        else:
            data.set_editor_properties(dict(animation_length=u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME,
                use_default_sample_rate=False, custom_sample_rate=100, import_bone_tracks=True,
                preserve_local_transform=True, import_custom_attribute=False))
        return options

    @staticmethod
    def hierarchy(mesh, skeleton):
        pose = u.AnimPoseExtensions.get_reference_pose(skeleton)
        require(u.AnimPoseExtensions.is_valid(pose), "Invalid imported skeleton reference pose")
        names = [str(n) for n in u.AnimPoseExtensions.get_bone_names(pose)]
        require(len(names) == 5 and set(names) == set(BONE_PARENTS),
                "Expected five bones, with no FBX Armature wrapper: " + str(names))
        result = {}
        for name in names:
            parent = str(u.SkeletalMeshEditorSubsystem.get_bone_parent(mesh, name))
            result[name] = None if parent == "None" else parent
        require(result == BONE_PARENTS, "Actual imported UE bone hierarchy differs: " + str(result))
        root = u.AnimPoseExtensions.get_bone_pose(pose, "root", u.AnimPoseSpaces.WORLD)
        require(all(abs(v-1.)<.001 for v in root.scale3d.to_tuple()),
                "Reference root must have unit scale; reject meter/centimeter wrapper compensation")
        face = u.AnimPoseExtensions.get_bone_pose(pose, "face", u.AnimPoseSpaces.WORLD)
        require(abs(face.translation.x-58.)<.5 and abs(face.translation.z-62.)<.5,
                "Imported face must point +X at the authored centimeter position")
        return result

    @staticmethod
    def transform_dict(transform):
        result = dict(translation_cm=[float(v) for v in transform.translation.to_tuple()],
                      scale=[float(v) for v in transform.scale3d.to_tuple()],
                      quaternion_xyzw=[float(getattr(transform.rotation, axis)) for axis in "xyzw"])
        require(all(math.isfinite(v) for values in result.values() for v in values), "Non-finite evaluated bone transform")
        return result

    def sample_clip(self, sequence, mesh, record):
        # Source evaluation with locking disabled must expose bad imported root
        # tracks. Forced runtime root lock is applied only after this raw gate.
        sequence.set_editor_property("enable_root_motion", False)
        sequence.set_editor_property("force_root_lock", False)
        options = u.AnimPoseEvaluationOptions()
        options.set_editor_properties(dict(optional_skeletal_mesh=mesh, evaluation_type=u.AnimDataEvalType.SOURCE,
            should_retarget=False, extract_root_motion=False, incorporate_root_motion_into_pose=True))
        frames = sorted(set((record["frame_start"], record["frame_end"], *record["critical_frames"])))
        samples = []
        for frame in frames:
            seconds = (frame - record["frame_start"]) / record["fps"]
            pose = u.AnimPoseExtensions.get_anim_pose_at_time(sequence, seconds, options)
            require(u.AnimPoseExtensions.is_valid(pose), "Animation evaluation failed: " + sequence.get_path_name())
            require(set(map(str, u.AnimPoseExtensions.get_bone_names(pose))) == set(BONE_PARENTS), "Clip pose bone set changed")
            bones = {name: self.transform_dict(u.AnimPoseExtensions.get_bone_pose(pose, name, u.AnimPoseSpaces.WORLD))
                     for name in BONE_PARENTS}
            samples.append(dict(source_frame=frame, seconds=seconds, bones=bones))
        baseline = samples[0]["bones"]["root"]
        max_root_translation = max(math.dist(s["bones"]["root"]["translation_cm"], baseline["translation_cm"]) for s in samples)
        max_root_scale = max(abs(s["bones"]["root"]["scale"][i] - baseline["scale"][i]) for s in samples for i in range(3))
        require(max_root_translation < .05 and max_root_scale < .001
                and max(abs(v - 1.) for v in baseline["scale"]) < .001,
                "Imported source root moves/scales: " + sequence.get_path_name())
        require(max(abs(v) for v in baseline["translation_cm"]) < .1, "Root is not at the authored floor pivot")
        body_z = [s["bones"]["gel_body"]["translation_cm"][2] for s in samples]
        if sequence.get_name() == "A_CoffeeSlime_Hop":
            require(max(body_z) - min(body_z) > 20., "Hop lost meaningful gel_body lift during FBX import")
        sequence.set_editor_property("force_root_lock", True)
        sequence.set_editor_property("root_motion_root_lock", u.RootMotionRootLock.REF_POSE)
        sequence.set_preview_skeletal_mesh(mesh)
        return dict(samples=samples, raw_root_max_translation_delta_cm=max_root_translation,
                    raw_root_max_scale_delta=max_root_scale, gel_body_z_range_cm=[min(body_z), max(body_z)],
                    basis="Source AnimPose at manifest critical frames, before forced root lock; no between-frame or rendered proof")

    def mesh_and_clips(self, manifest, materials):
        previous_mesh = self.existing(DEST + "/SK_CoffeeSlime", u.SkeletalMesh)
        skeleton = previous_mesh.get_editor_property("skeleton") if previous_mesh else None
        if skeleton:
            require(owned(skeleton), "Preserved external/custom existing skeleton")
        mesh = self.import_file(EXPORTS / "SK_CoffeeSlime.fbx", DEST, "SK_CoffeeSlime", u.SkeletalMesh,
                                self.fbx_options(True, skeleton), (u.SkeletalMesh, u.Skeleton))
        imported_skeleton = mesh.get_editor_property("skeleton")
        require(imported_skeleton and (not skeleton or imported_skeleton == skeleton), "Reimport changed the existing skeleton")
        skeleton = self.track(imported_skeleton, EXPORTS / "SK_CoffeeSlime.fbx")
        before_hierarchy = self.hierarchy(mesh, skeleton)
        require(mesh.get_editor_property("physics_asset") is None, "Unexpected physics asset; native sphere must own collision")
        bounds = mesh.get_bounds()
        dimensions = [float(v) * 2. for v in bounds.box_extent.to_tuple()]
        expected = [float(v) * 100. for v in manifest["expected_dimensions_m"]]
        require(all(abs(a - b) <= b * .05 for a, b in zip(dimensions, expected)),
                "Meter-to-centimeter import dimensions differ: " + str(dimensions))
        require(abs(bounds.origin.z - bounds.box_extent.z) < .5, "Imported mesh does not have a floor pivot")
        slots = list(mesh.get_editor_property("materials"))
        labels = [str(s.get_editor_property("imported_material_slot_name")) for s in slots]
        require(labels == list(MATERIALS), "Actual UE material slot order differs: " + str(labels))
        for slot, name in zip(slots, labels):
            slot.material_interface = materials[name]
        mesh.set_editor_property("materials", slots)
        self.report.update(mesh=mesh.get_path_name(), skeleton=skeleton.get_path_name(), hierarchy=before_hierarchy,
                           rest_dimensions_cm=dimensions, rest_bounds_origin_cm=list(bounds.origin.to_tuple()),
                           material_slots=[dict(slot=name, material=materials[name].get_path_name()) for name in labels])
        for name, record in manifest["clips"].items():
            sequence = self.import_file(EXPORTS / (name + ".fbx"), ANIM_DEST, name, u.AnimSequence,
                                        self.fbx_options(False, skeleton))
            require(sequence.get_editor_property("skeleton") == skeleton, "Clip skeleton differs: " + name)
            duration = float(sequence.get_play_length())
            frame_span = int(u.AnimationLibrary.get_num_frames(sequence))
            require(math.isfinite(duration) and abs(duration - record["duration_seconds"]) < .005
                    and frame_span == record["frame_end"] - record["frame_start"],
                    "Imported 100Hz clip duration/frame span differs: " + name + " " + str((duration, frame_span)))
            poses = self.sample_clip(sequence, mesh, record)
            self.report["clips"].append(dict(name=name, asset=sequence.get_path_name(), duration_seconds=duration,
                sampled_frame_span=frame_span, effective_sample_rate=frame_span / duration,
                root_motion_enabled=False, force_root_lock=True, **poses))
        require(self.hierarchy(mesh, skeleton) == before_hierarchy, "Animation imports changed the skeleton")
        u.AnimationLibrary.set_skeleton_preview_mesh(skeleton, mesh)

    def save(self):
        remaining = dirty_packages()
        require(all(p.startswith(DEST + "/") for p in remaining), "Import dirtied unrelated packages: " + str(remaining))
        for target, obj in sorted(self.assets.items()):
            require(owned(obj), "Refusing to save custom/unrelated asset: " + target)
            require(lib.save_loaded_asset(obj, only_if_is_dirty=False), "Could not save " + target)
            self.report["saved_packages"].append(target)
        self.report["dirty_after"] = dirty_packages()
        require(not self.report["dirty_after"], "Import left unsaved packages: " + str(self.report["dirty_after"]))


def main():
    report = dict(status="running", passed=False, scope="Owned CoffeeSlime assets only; no gameplay/map/Blueprint changes",
                  imported=[], skipped_unchanged=[], textures=[], materials=[], clips=[], saved_packages=[],
                  limitations=["Import/pose checks are technical; native gameplay and rendered appearance remain separate validation.",
                               "Source AnimPose samples do not validate all frames or deformation bounds."])
    previous_interchange = None
    importer = CoffeeImporter(report)
    try:
        manifest = importer.preflight()
        textures = importer.textures()
        materials = importer.materials(textures)
        cvar = "Interchange.FeatureFlags.Import.FBX"
        previous_interchange = u.SystemLibrary.get_console_variable_int_value(cvar)
        report["interchange_fbx_before"] = previous_interchange
        try:
            u.SystemLibrary.execute_console_command(None, cvar + " 0")
            require(u.SystemLibrary.get_console_variable_int_value(cvar) == 0, "Could not scope legacy FBX importer")
            importer.mesh_and_clips(manifest, materials)
        finally:
            u.SystemLibrary.execute_console_command(None, cvar + " " + str(previous_interchange))
            report["interchange_fbx_restored"] = u.SystemLibrary.get_console_variable_int_value(cvar) == previous_interchange
            require(report["interchange_fbx_restored"], "Could not restore FBX Interchange flag")
        importer.save()
        report.update(status="passed", passed=True)
    except Exception:
        report.update(status="failed", passed=False, error=traceback.format_exc(),
                      touched_packages=sorted(importer.assets), dirty_after_failure=dirty_packages())
        raise
    finally:
        write_report(report)
        u.log("MC_COFFEE_SLIME_IMPORT_" + report["status"].upper() + " " + str(OUTPUT))


if __name__ == "__main__":
    main()
