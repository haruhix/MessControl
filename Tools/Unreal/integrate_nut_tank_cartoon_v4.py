"""Import five Tank clips and a shield-free mesh on the existing skeleton.

Run in the idle Editor with the V4 native slots compiled. Preserve saved artist
assets, the current skeleton/reference pose, all other encounter tuning, and
unrelated unsaved packages. Only the generated V4 assets and bindings save.
"""
import importlib.util
import json
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
loader = importlib.util.spec_from_file_location('mc_tank_v4_helpers', ROOT / 'Tools/Unreal/integrate_nut_tank_cartoon_v3.py')
h = importlib.util.module_from_spec(loader)
loader.loader.exec_module(h)
h.SPEC = dict(h.qa.ROLE_SPECS[0], source='TankCartoonV4', clips=(
    ('charge_tell', 'ChargeTell'), ('charge_recovery', 'ChargeRecovery'),
    ('shield_break', 'ShieldBreak'), ('unshielded_idle', 'UnshieldedIdle'),
    ('unshielded_walk', 'UnshieldedWalk')))
h.DEST = '/Game/Gameplay/CoreLoop/NutTank/Animations/MCStyleV4'
h.REPORT = ROOT / 'Saved/Checks/TankCartoonV4AnimationIntegration.json'
h.qa.OWNER_TAG, h.qa.OWNER_VALUE = 'MCTankCartoonV4', 'integrate_nut_tank_cartoon_v4.py:1'
h.qa.CONTACT_CLIPS['tank'].add('ShieldBreak')
h.qa.LOOPS.update(('UnshieldedIdle', 'UnshieldedWalk'))
MESH_BASE = '/Game/Gameplay/CoreLoop/NutTank'
MESH_DEST = MESH_BASE + '/SK_NutTank_NoShield'
REPORT = ROOT / 'Saved/Checks/TankCartoonV4Integration.json'
lib = u.EditorAssetLibrary


def import_mesh():
    h.qa.require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE before import')
    dirty = h.qa.dirty_packages()
    old = lib.load_asset(MESH_BASE + '/SK_NutTank')
    skeleton = old.get_editor_property('skeleton')
    h.qa.require(not {h.qa.package_path(old), h.qa.package_path(skeleton), MESH_DEST, h.qa.PROFILE}.intersection(dirty),
                 'Tank assets have unsaved edits')
    reference = h.qa.reference_snapshot(skeleton)
    source, manifest, _ = h.qa.read_export(h.SPEC)
    filename = manifest.get('mesh_file', 'SK_NutTank_NoShield.fbx')
    record = next(r for r in manifest['files'] if r['file'] == filename)
    path = (source / filename).resolve()
    h.qa.require(path.is_relative_to(source) and path.is_file() and h.qa.digest(path) == record['sha256'],
                 'Unshielded FBX differs from checked export')
    legacy = json.loads((ROOT / 'ArtSource/NutTank/ExportReport.json').read_text(encoding='utf8'))
    legacy_record = next(r for r in legacy['files'] if r['file'] == legacy.get('mesh_file', 'SK_NutTank.fbx'))
    h.qa.require(record['fbx']['hierarchy_sha256'] == legacy_record['fbx']['hierarchy_sha256'],
                 'Unshielded mesh changes the skeleton hierarchy')
    existing = lib.load_asset(MESH_DEST) if lib.does_asset_exist(MESH_DEST) else None
    h.qa.require(not existing or lib.get_metadata_tag(existing, h.qa.OWNER_TAG) == h.qa.OWNER_VALUE,
                 'Refusing to replace an unowned mesh')
    options = u.FbxImportUI()
    for key, value in dict(automated_import_should_detect_type=False,
            mesh_type_to_import=u.FBXImportType.FBXIT_SKELETAL_MESH,
            original_import_type=u.FBXImportType.FBXIT_SKELETAL_MESH,
            import_mesh=True, import_as_skeletal=True, import_animations=False,
            import_materials=False, import_textures=False, create_physics_asset=False,
            skeleton=skeleton).items():
        options.set_editor_property(key, value)
    data = options.get_editor_property('skeletal_mesh_import_data')
    previous_data = old.get_editor_property('asset_import_data')
    for key in ('import_translation', 'import_rotation', 'import_uniform_scale',
                'convert_scene', 'convert_scene_unit', 'force_front_x_axis'):
        data.set_editor_property(key, previous_data.get_editor_property(key))
    data.set_editor_property('update_skeleton_reference_pose', False)
    data.set_editor_property('use_t0_as_ref_pose', False)
    data.set_editor_property('normal_import_method', u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    task = u.AssetImportTask()
    task.filename, task.destination_path, task.destination_name = str(path), MESH_BASE, 'SK_NutTank_NoShield'
    task.automated, task.save, task.replace_existing, task.replace_existing_settings = True, False, True, True
    task.factory, task.options = u.FbxFactory(), options
    cvar = 'Interchange.FeatureFlags.Import.FBX'
    previous_cvar = u.SystemLibrary.get_console_variable_int_value(cvar)
    try:
        u.SystemLibrary.execute_console_command(None, cvar + ' 0')
        u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    finally:
        u.SystemLibrary.execute_console_command(None, cvar + ' ' + str(previous_cvar))
    objects = [lib.load_asset(p) for p in task.imported_object_paths]
    h.qa.require(len(objects) == 1 and isinstance(objects[0], u.SkeletalMesh),
                 'Mesh-only import produced extra assets: ' + str(task.imported_object_paths))
    mesh = objects[0]
    h.qa.require(h.qa.package_path(mesh) == MESH_DEST, 'Unexpected mesh package')
    h.qa.require(mesh.get_editor_property('skeleton') == skeleton, 'Unshielded mesh replaced skeleton')
    material_by_slot = {str(s.material_slot_name): s.material_interface for s in old.get_editor_property('materials')}
    slots = list(mesh.get_editor_property('materials'))
    h.qa.require(slots and all(str(s.material_slot_name) in material_by_slot for s in slots), 'Unexpected material slots')
    for slot in slots:
        slot.material_interface = material_by_slot[str(slot.material_slot_name)]
    mesh.set_editor_property('materials', slots)
    lib.set_metadata_tag(mesh, h.qa.OWNER_TAG, h.qa.OWNER_VALUE)
    lib.set_metadata_tag(mesh, 'MCNutTankSource', str(path))
    lib.set_metadata_tag(mesh, 'MCNutTankSourceSHA256', record['sha256'])
    h.qa.require(h.qa.reference_snapshot(skeleton) == reference, 'Mesh import changed reference pose')
    h.qa.require(not (h.qa.dirty_packages() - dirty - {MESH_DEST}), 'Mesh import dirtied unrelated packages')
    h.qa.require(lib.save_loaded_asset(mesh, only_if_is_dirty=False), 'Cannot save unshielded mesh')
    h.qa.require(h.qa.dirty_packages() == dirty, 'Unexpected dirty packages after mesh save')
    return mesh, dict(asset=mesh.get_path_name(), source=str(path), same_skeleton=True,
                      reference_pose_preserved=True, material_assets_preserved=True)


def main():
    report = dict(passed=False)
    try:
        mesh, report['mesh'] = import_mesh()
        h.main()
        profile = lib.load_asset(h.qa.PROFILE)
        original = profile.get_editor_property('settings').copy()
        settings = original.copy()
        boss = settings.get_editor_property('boss').copy()
        previous = boss.get_editor_property('tank_unshielded_skeletal_mesh')
        report['previous_unshielded_mesh'] = previous.get_path_name() if previous else None
        boss.set_editor_property('tank_unshielded_skeletal_mesh', mesh)
        settings.set_editor_property('boss', boss)
        check = settings.copy()
        unchanged = boss.copy()
        unchanged.set_editor_property('tank_unshielded_skeletal_mesh', previous)
        check.set_editor_property('boss', unchanged)
        h.qa.require(check.export_text() == original.export_text(), 'Unrelated encounter settings changed')
        profile.modify()
        profile.set_editor_property('settings', settings)
        h.qa.require(lib.save_loaded_asset(profile, only_if_is_dirty=False), 'Cannot save unshielded binding')
        report.update(passed=True, animation_report=str(h.REPORT), shared_skeleton=True,
                      unshielded_mesh_binding=mesh.get_path_name())
        u.log('MC_TANK_CARTOON_V4_PASS')
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        REPORT.parent.mkdir(parents=True, exist_ok=True)
        REPORT.write_text(json.dumps(report, indent=2), encoding='utf8')


if __name__ == '__main__':
    main()
