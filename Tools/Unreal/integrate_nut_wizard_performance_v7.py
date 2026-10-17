"""Import the V7 mesh and one Cast; optionally create an isolated test profile.

Run in idle UE with py, after export_nut_wizard_performance_v7.py completes.
Only /Game/Gameplay/CoreLoop/NutWizard/MCPerformanceV7 is authored/saved.
DA_NutRain, the original mesh, skeleton, materials, old clips and maps stay intact.
main(create_test_profile=False) imports only; the default duplicate updates the
mesh, Cast and its release palm offset. This script never starts PIE/capture.
"""
import hashlib
import importlib.util
import json
import math
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
DEST = '/Game/Gameplay/CoreLoop/NutWizard/MCPerformanceV7'
MESH_PATH = DEST + '/SK_NutWizard_PerformanceV7'
CAST_PATH = DEST + '/A_NutWizard_Cast'
TEST_PROFILE_PATH = DEST + '/DA_NutRain_PerformanceV7'
HAND_MATERIAL_PATH = DEST + '/M_NutWizard_HandEye'
HAND_TEXTURE_PATH = DEST + '/T_NutWizard_HandEye'
ORIGINAL_MESH_PATH = '/Game/Gameplay/CoreLoop/NutWizard/SK_NutWizard'
ORIGINAL_PROFILE_PATH = '/Game/Gameplay/CoreLoop/DA_NutRain'
SOURCE = ROOT / 'ArtSource/NutAnimationStyle/ExportsV7/Wizard'
REPORT = ROOT / 'Saved/Checks/WizardPerformanceV7Integration.json'
OWNER_TAG, OWNER_VALUE = 'MCWizardPerformanceV7', 'integrate_nut_wizard_performance_v7.py:1'
lib = u.EditorAssetLibrary
loader = importlib.util.spec_from_file_location('mc_wizard_v7_pose_helpers', ROOT / 'Tools/Unreal/integrate_nut_style_v2.py')
qa = importlib.util.module_from_spec(loader)
loader.loader.exec_module(qa)


def protected_hashes():
    content = Path(u.Paths.project_content_dir()).resolve()
    destination = content / DEST.removeprefix('/Game/')
    result = {}
    for folder in ('Gameplay/CoreLoop/NutWizard', 'Gameplay/CoreLoop/NutTank', 'FromBlender8'):
        for path in (content / folder).rglob('*'):
            if path.is_file() and path.suffix.lower() in ('.uasset', '.uexp', '.ubulk') and not path.is_relative_to(destination):
                result[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    profile = content / 'Gameplay/CoreLoop/DA_NutRain.uasset'
    qa.require(profile.is_file(), 'The original saved encounter profile is missing')
    result[str(profile)] = hashlib.sha256(profile.read_bytes()).hexdigest()
    return result


def owned(asset):
    return qa.package_path(asset).startswith(DEST + '/') and lib.get_metadata_tag(asset, OWNER_TAG) == OWNER_VALUE


def mark(asset, source_file=None):
    qa.require(qa.package_path(asset).startswith(DEST + '/'), 'Refusing to tag an original asset')
    lib.set_metadata_tag(asset, OWNER_TAG, OWNER_VALUE)
    if source_file:
        lib.set_metadata_tag(asset, 'MCWizardPerformanceV7Source', str(source_file))


def hand_eye_material():
    """Reproduce the packed Blender atlas without altering the old material."""
    atlas = ROOT / 'ArtSource/NutAnimationStyle/Wizard/Textures/hand_eye.png'
    qa.require(atlas.is_file(), 'Export the packed hand_eye image before integration')
    task = u.AssetImportTask()
    task.filename = str(atlas)
    task.destination_path, task.destination_name = DEST, 'T_NutWizard_HandEye'
    task.automated, task.save = True, False
    task.replace_existing, task.replace_existing_settings = True, True
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = lib.load_asset(HAND_TEXTURE_PATH)
    qa.require(isinstance(texture, u.Texture2D), 'Could not import hand_eye atlas')
    texture.set_editor_property('srgb', True)
    mark(texture, atlas)
    material = lib.load_asset(HAND_MATERIAL_PATH)
    if not material:
        material = u.AssetToolsHelpers.get_asset_tools().create_asset('M_NutWizard_HandEye', DEST, u.Material, u.MaterialFactoryNew())
    qa.require(isinstance(material, u.Material), 'Could not create hand_eye material')
    mark(material, atlas)
    edit = u.MaterialEditingLibrary
    edit.delete_all_material_expressions(material)
    sample = edit.create_material_expression(material, u.MaterialExpressionTextureSample, -450, -100)
    sample.set_editor_property('texture', texture)
    uv = edit.create_material_expression(material, u.MaterialExpressionTextureCoordinate, -650, -100)
    uv.set_editor_property('coordinate_index', 0)
    qa.require(edit.connect_material_expressions(uv, '', sample, 'UVs'), 'Cannot wire atlas UV0')
    qa.require(edit.connect_material_property(sample, 'RGB', u.MaterialProperty.MP_BASE_COLOR), 'Cannot wire atlas color')
    for value, node_y, prop in ((.7868217, 150, u.MaterialProperty.MP_ROUGHNESS), (0., 280, u.MaterialProperty.MP_METALLIC)):
        constant = edit.create_material_expression(material, u.MaterialExpressionConstant, -250, node_y)
        constant.set_editor_property('r', value)
        qa.require(edit.connect_material_property(constant, '', prop), 'Cannot wire material scalar')
    errors = edit.recompile_material(material)
    qa.require(not errors, 'hand_eye material compile failed: '+str(errors))
    return material, texture


def task_for(name, filename, options):
    task = u.AssetImportTask()
    task.filename = str(SOURCE / filename)
    task.destination_path, task.destination_name = DEST, name
    task.automated, task.save = True, False
    task.replace_existing, task.replace_existing_settings = True, True
    task.factory, task.options = u.FbxFactory(), options
    return task


def import_one(task, asset_type, target):
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = list(task.imported_object_paths)
    qa.require(len(paths) == 1, 'Expected one new asset, without a new skeleton/physics/material: ' + str(paths))
    asset = lib.load_asset(paths[0])
    qa.require(isinstance(asset, asset_type) and qa.package_path(asset).startswith(DEST + '/'), 'Unexpected import type/destination')
    if qa.package_path(asset) != target:
        qa.require(not lib.does_asset_exist(target), 'Cannot normalize import over an existing asset: ' + target)
        qa.require(lib.rename_asset(asset.get_path_name(), target), 'Cannot normalize import name')
        asset = lib.load_asset(target)
    mark(asset, task.filename)
    return asset


def same_except_cast_presentation(settings, original):
    restored, boss = settings.copy(), settings.get_editor_property('boss').copy()
    original_boss = original.get_editor_property('boss')
    for key in ('mage_skeletal_mesh', 'mage_cast_animation', 'mage_cast_offset'):
        boss.set_editor_property(key, original_boss.get_editor_property(key))
    restored.set_editor_property('boss', boss)
    return restored.export_text() == original.export_text()


def main(create_test_profile=True):
    if '-run=' not in u.SystemLibrary.get_command_line().lower():
        qa.require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE before V7 import')
    dirty = qa.dirty_packages()
    original_mesh, original_profile = lib.load_asset(ORIGINAL_MESH_PATH), lib.load_asset(ORIGINAL_PROFILE_PATH)
    qa.require(isinstance(original_mesh, u.SkeletalMesh) and original_profile is not None, 'Original Wizard/profile unavailable')
    skeleton = original_mesh.get_editor_property('skeleton')
    reference = qa.reference_snapshot(skeleton)
    qa.require(len(reference['bones']) == 50 and reference['bones'][0] == 'root'
               and {'root_x', 'head_x', 'hand_l', 'hand_r'}.issubset(reference['bones']),
               'Expected the existing 49 deform bones plus the synthetic FBX root')
    expected = {MESH_PATH, CAST_PATH, HAND_MATERIAL_PATH, HAND_TEXTURE_PATH} | ({TEST_PROFILE_PATH} if create_test_profile else set())
    protected_paths = {ORIGINAL_MESH_PATH, ORIGINAL_PROFILE_PATH, qa.package_path(skeleton)}
    qa.require(not dirty.intersection(protected_paths | expected), 'Preserve unsaved original/target asset edits before importing')
    for path in lib.list_assets(DEST, recursive=True, include_folder=False):
        asset = lib.load_asset(path)
        qa.require(asset and owned(asset) and qa.package_path(asset) in {MESH_PATH, CAST_PATH, TEST_PROFILE_PATH, HAND_MATERIAL_PATH, HAND_TEXTURE_PATH},
                   'Unexpected unowned V7 asset: ' + path)
    protected = protected_hashes()
    original_settings = original_profile.get_editor_property('settings').copy()
    original_mesh_materials = original_mesh.get_editor_property('materials')
    original_mesh_text = [(str(slot.material_slot_name), slot.material_interface) for slot in original_mesh_materials]
    manifest = json.loads((SOURCE / 'ExportReport.json').read_text(encoding='utf8'))
    qa.require(manifest.get('status') == 'complete' and all(manifest.get(key) for key in
               ('source_preserved', 'reference_unchanged', 'hierarchy_consistent', 'root_animation_constant')), 'V7 export checks failed')
    qa.require(Path(manifest['source']).name == 'NutWizard_Performance_v7.blend', 'Wrong saved V7 source')
    qa.require(qa.digest(Path(manifest['source'])) == manifest['source_sha256_before'], 'Source changed after export')
    clips = manifest['clips']
    qa.require(len(clips) == 1 and clips[0]['label'] == 'Cast', 'V7 must contain only one Cast')
    clip = clips[0]
    qa.require(manifest['mesh_file'] == 'SK_NutWizard_PerformanceV7.fbx' and clip['file'] == 'A_NutWizard_Cast.fbx', 'Unexpected V7 FBX names')
    legacy = json.loads((ROOT / 'ArtSource/NutWizard/ExportReport.json').read_text(encoding='utf8'))
    legacy_reference = next(row['fbx'] for row in legacy['files'] if row['file'] == legacy['mesh_file'])
    files = {row['file']: row for row in manifest['files']}
    for name in (manifest['mesh_file'], clip['file']):
        row = files[name]
        qa.require(qa.digest(SOURCE / name) == row['sha256'], 'FBX changed after export: ' + name)
        qa.require(row['fbx']['bone_count'] == 49 and row['fbx']['hierarchy_sha256'] == legacy_reference['hierarchy_sha256']
                   and row['bind_max_delta'] < .0001, 'Native49 hierarchy/bind mismatch: ' + name)
    previous_importer = u.SystemLibrary.get_console_variable_int_value('Interchange.FeatureFlags.Import.FBX')
    report = dict(passed=False, original_mesh=ORIGINAL_MESH_PATH, original_profile=ORIGINAL_PROFILE_PATH,
                  scope='New saved mesh and one Cast, optional duplicate profile; solo gameplay/render review is separate.')
    try:
        u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
        options = u.FbxImportUI()
        for name, value in dict(automated_import_should_detect_type=False, mesh_type_to_import=u.FBXImportType.FBXIT_SKELETAL_MESH,
                               original_import_type=u.FBXImportType.FBXIT_SKELETAL_MESH, import_mesh=True, import_as_skeletal=True,
                               import_animations=False, import_materials=False, import_textures=False, create_physics_asset=False,
                               skeleton=skeleton).items():
            options.set_editor_property(name, value)
        data = options.get_editor_property('skeletal_mesh_import_data')
        legacy_data = original_mesh.get_editor_property('asset_import_data')
        qa.require(isinstance(legacy_data, u.FbxSkeletalMeshImportData), 'Original mesh lacks proven FBX import settings')
        for key in ('import_translation', 'import_rotation', 'import_uniform_scale', 'convert_scene', 'convert_scene_unit', 'force_front_x_axis'):
            data.set_editor_property(key, legacy_data.get_editor_property(key))
        data.set_editor_property('update_skeleton_reference_pose', False)
        data.set_editor_property('use_t0_as_ref_pose', False)
        data.set_editor_property('import_morph_targets', False)
        data.set_editor_property('normal_import_method', u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
        mesh = import_one(task_for('SK_NutWizard_PerformanceV7', manifest['mesh_file'], options), u.SkeletalMesh, MESH_PATH)
        qa.require(mesh.get_editor_property('skeleton') == skeleton and qa.reference_snapshot(skeleton) == reference, 'Mesh import changed the original skeleton')
        slots = list(mesh.get_editor_property('materials'))
        qa.require(len(slots) == len(original_mesh_materials), 'V7 material slot count changed')
        for index, slot in enumerate(slots):
            qa.require(str(slot.material_slot_name) == str(original_mesh_materials[index].material_slot_name), 'V7 material slot order/name changed')
            slot.material_interface = original_mesh_materials[index].material_interface
        material, texture = hand_eye_material()
        qa.require(str(slots[1].material_slot_name) == 'hand_eye', 'Wrong hand_eye material slot')
        slots[1].material_interface = material
        mesh.set_editor_property('materials', slots)
        mesh.set_editor_property('physics_asset', original_mesh.get_editor_property('physics_asset'))
        legacy_idle = lib.load_asset('/Game/Gameplay/CoreLoop/NutWizard/Animations/A_NutWizard_Idle')
        options = qa.animation_options(skeleton, legacy_idle, float(manifest['fps']), 'A_NutWizard_Cast')
        sequence = import_one(task_for('A_NutWizard_Cast', clip['file'], options), u.AnimSequence, CAST_PATH)
        qa.require(sequence.get_editor_property('skeleton') == skeleton, 'Cast imported on another skeleton')
        qa.require(abs(sequence.get_play_length() - float(clip['duration_seconds'])) < .004, 'Imported Cast duration changed')
        sequence.set_editor_property('enable_root_motion', False)
        sequence.set_editor_property('force_root_lock', True)
        sequence.set_editor_property('root_motion_root_lock', u.RootMotionRootLock.REF_POSE)
        u.AnimationLibrary.add_curve(sequence, 'MCSpellGesture', u.RawCurveTrackTypes.RCT_FLOAT, False)
        u.AnimationLibrary.add_float_curve_keys(sequence, 'MCSpellGesture', [0., sequence.get_play_length()], [1., 1.])
        marker_times, marker_values = u.AnimationLibrary.get_float_keys(sequence, 'MCSpellGesture')
        qa.require(len(marker_times) == 2 and all(abs(float(value) - 1.) < .0001 for value in marker_values), 'Authored gesture marker did not serialize')
        pose_options = u.AnimPoseEvaluationOptions()
        pose_options.set_editor_property('optional_skeletal_mesh', mesh)
        pose_options.set_editor_property('evaluation_type', u.AnimDataEvalType.RAW)
        pose_options.set_editor_property('extract_root_motion', False)
        context = dict(role='mage', mesh=mesh, skeleton=skeleton, fps=float(manifest['fps']), reference=reference, pose_options=pose_options)
        # Five focused imported-pose checks; the production helper's broad
        # per-frame suite is intentionally not run for this single prototype.
        for fraction in (0., .25, .55, .75, 1.):
            pose = qa.pose_at(context, sequence, sequence.get_play_length() * fraction)
            for bone in reference['bones']:
                values = qa.transform_values(u.AnimPoseExtensions.get_bone_pose(pose, bone, u.AnimPoseSpaces.LOCAL))
                qa.require(all(math.isfinite(value) for value in values), 'Nonfinite imported pose: ' + bone)
        report['cast_anchor'] = qa.measure_cast_anchor(context, sequence, original_settings.get_editor_property('boss'))
        to_save = [mesh, sequence, material, texture]
        report['hand_eye_material'] = dict(material=material.get_path_name(), texture=texture.get_path_name(),
            source_atlas_sha256=qa.digest(ROOT / 'ArtSource/NutAnimationStyle/Wizard/Textures/hand_eye.png'),
            uv_channel=0, roughness=.7868217, original_material_preserved=True)
        if create_test_profile:
            profile = lib.load_asset(TEST_PROFILE_PATH) or lib.duplicate_asset(ORIGINAL_PROFILE_PATH, TEST_PROFILE_PATH)
            qa.require(profile is not None, 'Could not create isolated V7 test profile')
            mark(profile)
            settings, boss = original_settings.copy(), original_settings.get_editor_property('boss').copy()
            boss.set_editor_property('mage_skeletal_mesh', mesh)
            boss.set_editor_property('mage_cast_animation', sequence)
            boss.set_editor_property('mage_cast_offset', u.Vector(*report['cast_anchor']['palm_actor_local_cm']))
            settings.set_editor_property('boss', boss)
            qa.require(same_except_cast_presentation(settings, original_settings), 'Test profile changed other settings')
            profile.modify()
            profile.set_editor_property('settings', settings)
            qa.require(same_except_cast_presentation(profile.get_editor_property('settings'), original_settings), 'Serialized test profile changed other settings')
            report['test_profile_cast_anchor'] = qa.measure_cast_anchor(context, sequence, boss)
            to_save.append(profile)
            report['test_profile'] = profile.get_path_name()
        qa.require(qa.reference_snapshot(skeleton) == reference, 'Original skeleton reference changed')
        qa.require(original_profile.get_editor_property('settings').export_text() == original_settings.export_text(), 'Original encounter bindings changed')
        qa.require(original_mesh_text == [(str(slot.material_slot_name), slot.material_interface) for slot in original_mesh.get_editor_property('materials')], 'Original mesh materials changed')
        qa.require(not (qa.dirty_packages() - dirty - expected), 'Import dirtied an original/unrelated package')
        qa.require(protected_hashes() == protected, 'Import wrote an original package')
        for asset in to_save:
            qa.require(lib.save_loaded_asset(asset, only_if_is_dirty=False), 'Could not save new V7 asset')
        qa.require(protected_hashes() == protected and qa.dirty_packages() == dirty, 'Saving changed original/unrelated packages')
        report.update(passed=True, mesh=mesh.get_path_name(), cast=sequence.get_path_name(), skeleton=skeleton.get_path_name(),
                      native_bones=49, imported_pose_fractions=[0., .25, .55, .75, 1.], saved_packages=sorted(expected),
                      original_packages_preserved=True, original_encounter_profile_preserved=True)
        u.log('MC_WIZARD_PERFORMANCE_V7_INTEGRATION_PASS ' + json.dumps(report))
        return report
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX ' + str(previous_importer))
        REPORT.parent.mkdir(parents=True, exist_ok=True)
        REPORT.write_text(json.dumps(report, indent=2), encoding='utf8')


if __name__ == '__main__':
    main(create_test_profile=bool(globals().get('MC_WIZARD_V7_CREATE_TEST_PROFILE', True)))
