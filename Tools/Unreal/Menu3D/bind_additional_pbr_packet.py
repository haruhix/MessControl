"""Bind an explicitly released interior or family Painter packet in the work map.

The request names a fixed task-owned target and the exact released manifest
hash. Source image hashes/PNG dimensions are checked before asset mutations.
"""
import hashlib
import json
from pathlib import Path
import struct
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
request = json.loads((root / 'Tools/Unreal/Menu3D/Evidence/additional_pbr_binding_request.json').read_text(encoding='utf8'))
kind = request['kind']
assert kind in ('INTERIOR_DAY03', 'FAMILY_DAY01', 'FAMILY_DAY02')
revision = root / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff'
if kind == 'INTERIOR_DAY03':
    handoff = revision / 'v08_ReferenceFinish/Interior/UV_Organic_r05_Candidate01'
    day, sets, parts = 'DAY03', ('MC_Interior', 'MC_Tongue'), ('Interior', 'Tongue')
    targets = {part: '/Game/UI/MainMenu3D/Meshes/SM_DAY03_' + part + '_Organic_v08' for part in parts}
    suffix, static = 'v08', True
else:
    day = kind.removeprefix('FAMILY_')
    candidate = 'UV_v02_GumReturns' if day == 'DAY01' else 'UV_v01'
    handoff = revision / 'v09_FamilyReferenceForms' / day / candidate
    sets, parts = ('MC_Enamel', 'MC_Gum'), ('Enamel', 'Gum')
    targets = {part: '/Game/UI/MainMenu3D/Meshes/SK_' + day + '_Reference_v09' for part in parts}
    suffix, static = 'v09', False
manifest_path = handoff / 'Textures/shader_binding.json'
assert hashlib.sha256(manifest_path.read_bytes()).hexdigest() == request['manifest_sha256']
manifest = json.loads(manifest_path.read_text(encoding='utf8'))
assert manifest['normal_format'] == 'DirectX' and manifest['texture_resolution'] == 4096
expected_packet = {
    'INTERIOR_DAY03': '5b3e50d1720c3f05189368291c77bfbe76e8f8b7c7bd88a367076a0a88e3a2eb',
    'FAMILY_DAY01': 'b330346544110cfd1ab55bc44df18884c6acf6c4bfc4ab0ed9dfa61fcefa8a78',
    'FAMILY_DAY02': 'ee735723f4ba067aef9d402e5d599ef34c153a7c291a6ee85b6b4b6146ab3657',
}[kind]
assert manifest['mesh_packet_sha256'] == expected_packet
assert hashlib.sha256((handoff / 'texture_mesh_packet.npz').read_bytes()).hexdigest() == expected_packet
assert set(manifest['texture_sets']) == set(sets)
channels = ('BaseColorTexture', 'RoughnessTexture', 'NormalTexture', 'AOTexture', 'ThicknessTexture')
verified = []
for set_name in sets:
    maps = manifest['map_metadata'][set_name]
    assert set(maps) == set(channels)
    for parameter in channels:
        entry = maps[parameter]
        source = Path(entry['path']).resolve()
        assert source.is_relative_to(handoff.resolve()) and source.suffix.lower() == '.png'
        payload = source.read_bytes()
        assert hashlib.sha256(payload).hexdigest() == entry['sha256'], source
        assert payload[:8] == b'\x89PNG\r\n\x1a\n'
        assert struct.unpack('>II', payload[16:24]) == (4096, 4096)
        assert entry['dimensions'] == [4096, 4096]
        assert entry['srgb'] == (parameter == 'BaseColorTexture')
        if parameter == 'NormalTexture':
            assert entry['compression'] == 'TC_NORMALMAP' and entry['flip_green_channel'] is False
        verified.append((set_name, parameter, source, entry['sha256']))
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0] == '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
meshes, components = {}, {}
for part in parts:
    tag = 'Menu3D_' + day + ('_' + part if static else '')
    actors = u.GameplayStatics.get_all_actors_with_tag(world, tag)
    assert len(actors) == 1
    component = actors[0].get_component_by_class(u.StaticMeshComponent if static else u.SkeletalMeshComponent)
    mesh = u.load_asset(targets[part])
    assert mesh and component.get_editor_property('static_mesh' if static else 'skeletal_mesh_asset') == mesh
    meshes[part], components[part] = mesh, component
assets = u.AssetToolsHelpers.get_asset_tools()
base = '/Game/UI/MainMenu3D/ReferenceFinish'
report = {'status': 'RELEASED_PBR_BOUND_PENDING_NATIVE_APPEARANCE_MOTION_REVIEW', 'kind': kind,
          'manifest_sha256': request['manifest_sha256'], 'textures': [], 'bindings': [],
          'source': str(manifest_path), 'camera_motion_character_startup_changed': False}
textures = {}
for set_name, parameter, source, digest in verified:
    part = set_name.removeprefix('MC_')
    folder = base + '/Textures/' + day + ('/Interior' if static else '')
    name = 'T_' + day + '_' + part + '_' + parameter.removesuffix('Texture') + '_' + suffix
    path = folder + '/' + name
    task = u.AssetImportTask()
    for field, value in [('filename', str(source)), ('destination_path', folder), ('destination_name', name),
                         ('automated', True), ('save', False), ('replace_existing', True), ('replace_existing_settings', True)]:
        task.set_editor_property(field, value)
    assets.import_asset_tasks([task])
    texture = u.load_asset(path)
    assert texture
    normal = parameter == 'NormalTexture'
    texture.set_editor_property('srgb', parameter == 'BaseColorTexture')
    texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP if normal else
                                u.TextureCompressionSettings.TC_DEFAULT if parameter == 'BaseColorTexture' else
                                u.TextureCompressionSettings.TC_MASKS)
    if normal:
        texture.set_editor_property('flip_green_channel', False)
    wanted = 'TEXTUREGROUPCHARACTERNORMALMAP' if normal else 'TEXTUREGROUPCHARACTER'
    enum = [n for n in dir(u.TextureGroup) if n.replace('_', '') == wanted]
    assert len(enum) == 1
    texture.set_editor_property('lod_group', getattr(u.TextureGroup, enum[0]))
    u.EditorAssetLibrary.set_metadata_tag(texture, 'Menu3DSourceSHA', digest)
    assert u.EditorAssetLibrary.save_loaded_asset(texture)
    textures[(part, parameter)] = texture
    report['textures'].append({'asset': path, 'source_sha256': digest, 'srgb': parameter == 'BaseColorTexture'})
for index, part in enumerate(parts):
    path = base + '/Materials/MI_' + day + '_' + part + '_' + suffix
    material = u.load_asset(path)
    parent = u.load_asset(base + '/Materials/M_Menu' + part + '_v08')
    assert material and parent
    u.MaterialEditingLibrary.set_material_instance_parent(material, parent)
    for parameter in channels:
        expected = textures[(part, parameter)]
        u.MaterialEditingLibrary.set_material_instance_texture_parameter_value(material, parameter, expected)
        assert u.MaterialEditingLibrary.get_material_instance_texture_parameter_value(material, parameter) == expected
    u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(material, 'BaseColorTint', u.LinearColor(1., 1., 1., 1.))
    u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(material, 'RoughnessScale', 1.)
    tint = u.MaterialEditingLibrary.get_material_instance_vector_parameter_value(material, 'BaseColorTint')
    assert max(abs(tint.r-1.), abs(tint.g-1.), abs(tint.b-1.)) < 1e-5
    assert abs(u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(material, 'RoughnessScale')-1.) < 1e-5
    assert u.EditorAssetLibrary.save_loaded_asset(material)
    mesh, component = meshes[part], components[part]
    slot = 0 if static else index
    component.set_material(slot, material)
    if static:
        mesh.set_material(0, material)
    else:
        slots = list(mesh.get_editor_property('materials'))
        slots[slot].set_editor_property('material_interface', material)
        mesh.set_editor_property('materials', slots)
    assert u.EditorAssetLibrary.save_loaded_asset(mesh)
    report['bindings'].append({'mesh': mesh.get_path_name(), 'slot': slot, 'material': path})
assert levels.save_current_level()
target = root / ('Tools/Unreal/Menu3D/Evidence/pbr_' + kind.lower() + '_binding_report.json')
target.write_text(json.dumps(report, indent=2), encoding='utf8')
u.log('MC_ADDITIONAL_PBR_BOUND ' + json.dumps(report))
