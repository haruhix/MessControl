"""Create an unbound native material candidate changing only enamel BaseColor."""
import hashlib
import json
from pathlib import Path
import struct
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
frozen = root / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08b_SeamUV/Candidates/Final96_GumMinStretch_LocalFangRepair'
request_path = root / 'Tools/Unreal/Menu3D/Evidence/warm_coronal_import_request.json'
request = json.loads(request_path.read_text(encoding='utf8')) if request_path.exists() else {'revision': '01'}
revision = request['revision']
assert revision in ('01', '02')
candidate = frozen / ('LookCandidates/WarmCoronalBody_' + revision + '/Textures')
manifest_path = candidate / 'shader_binding.json'
expected_manifest = {'01': '3ccbba9ec4c90bf391f6946b34b24d2fb6648650161e09666683e36c04376cc3',
                     '02': 'a8415683b991194a495318129b3509fef8ce9699952ca01282c13738279f9066'}[revision]
assert hashlib.sha256(manifest_path.read_bytes()).hexdigest() == expected_manifest
source_manifest_path = frozen / 'Textures/shader_binding.json'
assert hashlib.sha256(source_manifest_path.read_bytes()).hexdigest() == '740cc6fd09959eb9dc040dcf74e34e9d5fe680c5776b81600fa13300557efe6f'
manifest = json.loads(manifest_path.read_text(encoding='utf8'))
source_manifest = json.loads(source_manifest_path.read_text(encoding='utf8'))
assert manifest['normal_format'] == 'DirectX' and manifest['texture_resolution'] == 4096
assert manifest['lighting_baked_into_basecolor'] is False
assert manifest['changed_roles'] == ['MC_Enamel.BaseColorTexture']
assert manifest['mesh_packet_sha256'] == source_manifest['mesh_packet_sha256']
for set_name, maps in manifest['map_metadata'].items():
    for name, entry in maps.items():
        source = Path(entry['path']).resolve()
        assert source.is_relative_to(frozen.resolve())
        payload = source.read_bytes()
        assert hashlib.sha256(payload).hexdigest() == entry['sha256']
        assert payload[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack('>II', payload[16:24]) == (4096, 4096)
        if (set_name, name) != ('MC_Enamel', 'BaseColorTexture'):
            original = source_manifest['map_metadata'][set_name][name]
            assert entry['sha256'] == original['sha256'] and source == Path(original['path']).resolve()
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0] == '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
base = '/Game/UI/MainMenu3D/ReferenceFinish'
texture_name = 'T_DAY03_Enamel_BaseColor_WarmBody' + revision
texture_path = base + '/Textures/DAY03/' + texture_name
material_path = base + '/Materials/MI_DAY03_Enamel_WarmBody' + revision
assert not u.EditorAssetLibrary.does_asset_exist(texture_path) and not u.EditorAssetLibrary.does_asset_exist(material_path)
entry = manifest['map_metadata']['MC_Enamel']['BaseColorTexture']
task = u.AssetImportTask()
for field, value in [('filename', entry['path']), ('destination_path', base + '/Textures/DAY03'),
                     ('destination_name', texture_name), ('automated', True), ('save', False)]:
    task.set_editor_property(field, value)
u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
texture = u.load_asset(texture_path)
assert texture
texture.set_editor_property('srgb', True)
texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_DEFAULT)
groups = [name for name in dir(u.TextureGroup) if name.replace('_', '') == 'TEXTUREGROUPCHARACTER']
assert len(groups) == 1
texture.set_editor_property('lod_group', getattr(u.TextureGroup, groups[0]))
u.EditorAssetLibrary.set_metadata_tag(texture, 'Menu3DSourceSHA', entry['sha256'])
assert u.EditorAssetLibrary.save_loaded_asset(texture)
original_material = u.load_asset(base + '/Materials/MI_DAY03_Enamel_v08')
material = u.EditorAssetLibrary.duplicate_asset(original_material.get_path_name(), material_path)
assert material
u.MaterialEditingLibrary.set_material_instance_texture_parameter_value(material, 'BaseColorTexture', texture)
assert u.MaterialEditingLibrary.get_material_instance_texture_parameter_value(material, 'BaseColorTexture') == texture
unchanged = {}
for name in ('RoughnessTexture', 'NormalTexture', 'AOTexture', 'ThicknessTexture'):
    expected = u.MaterialEditingLibrary.get_material_instance_texture_parameter_value(original_material, name)
    assert u.MaterialEditingLibrary.get_material_instance_texture_parameter_value(material, name) == expected
    unchanged[name] = expected.get_path_name()
assert u.EditorAssetLibrary.save_loaded_asset(material)
report = {'status': 'UNBOUND_NATIVE_BASECOLOR_ONLY_CANDIDATE_AB_PENDING',
          'manifest_sha256': hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
          'texture': texture.get_path_name(), 'material': material.get_path_name(), 'unchanged_data_maps': unchanged,
          'work_map_assignment_changed': False}
report_name = 'v08_warm_coronal_import_report.json' if revision == '01' else 'v08_warm_coronal02_import_report.json'
(root / 'Tools/Unreal/Menu3D/Evidence' / report_name).write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report))
