"""Import a verified Painter packet and bind it in the isolated native v08 map.

Run through remote_python.py after independent geometry/UV checks. This script
does not create missing art, change startup maps, or alter menu input/animation.
"""
import hashlib
import json
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
request_path = root / 'Tools/Unreal/Menu3D/Evidence/v08_painter_binding_request.json'
request = json.loads(request_path.read_text(encoding='utf8'))
assert request['revision'] == 'v08b_SeamUV', 'The fragmented v08 UVs were rejected; use the reviewed replacement'
handoff = root / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff' / request['revision']
assert request['candidate'] == 'Candidates/Final96_GumMinStretch_LocalFangRepair'
handoff = (handoff / request['candidate']).resolve()
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor(), 'Stop this task\'s PIE before binding saved materials'
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world and world.get_path_name().split('.')[0] == '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map edits first'
assert request['normal_convention'] == 'DirectX'
assert request['resolution'] == 4096
mesh = u.load_asset(request['mesh'])
assert mesh and 'Reference' in request['mesh'] and request['mesh'].startswith('/Game/UI/MainMenu3D/')
source_fbx = handoff / 'SK_DAY03.fbx'
assert hashlib.sha256(source_fbx.read_bytes()).hexdigest() == request['fbx_sha256']

# Validate the entire source packet before any Unreal asset is created/replaced.
channels = ('BaseColorTexture', 'RoughnessTexture', 'NormalTexture', 'AOTexture', 'ThicknessTexture')
verified = []
for set_name in ('MC_Enamel', 'MC_Gum'):
    info = request['texture_sets'][set_name]
    assert set(info['maps']) == set(channels)
    for parameter, entry in info['maps'].items():
        source = Path(entry['path']).resolve()
        assert source.is_relative_to(handoff.resolve()), str(source)
        assert source.suffix.lower() == '.png' and source.is_file()
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        assert digest == entry['sha256'], (source, digest)
        assert entry['size'] == [4096, 4096]
        verified.append((set_name, parameter, source, digest))

base = '/Game/UI/MainMenu3D/ReferenceFinish'
assets = u.AssetToolsHelpers.get_asset_tools()
report = {'status': 'PBR_BOUND_PENDING_NATIVE_VISUAL_REVIEW', 'world': world.get_path_name(),
          'mesh': mesh.get_path_name(), 'source_fbx_sha256': request['fbx_sha256'],
          'textures': [], 'instances': []}
imported = {}
for set_name, parameter, source, digest in verified:
    short = set_name.removeprefix('MC_')
    asset_name = 'T_DAY03_' + short + '_' + parameter.removesuffix('Texture') + '_v08'
    folder = base + '/Textures/DAY03'
    path = folder + '/' + asset_name
    task = u.AssetImportTask()
    for key, value in [('filename', str(source)), ('destination_path', folder),
                       ('destination_name', asset_name), ('automated', True),
                       ('save', False), ('replace_existing', True), ('replace_existing_settings', True)]:
        task.set_editor_property(key, value)
    assets.import_asset_tasks([task])
    tex = u.load_asset(path)
    assert tex, path
    is_normal = parameter == 'NormalTexture'
    tex.set_editor_property('srgb', parameter == 'BaseColorTexture')
    tex.set_editor_property('compression_settings',
        u.TextureCompressionSettings.TC_NORMALMAP if is_normal else
        u.TextureCompressionSettings.TC_DEFAULT if parameter == 'BaseColorTexture' else
        u.TextureCompressionSettings.TC_MASKS)
    if is_normal:
        # Painter already exported DX green; flipping it here would invert twice.
        tex.set_editor_property('flip_green_channel', False)
    # Resolve the reflected enum spelling on this engine (NORMAL_MAP vs
    # NORMALMAP differs across Python name-generation versions).
    wanted = 'TEXTUREGROUPCHARACTERNORMALMAP' if is_normal else 'TEXTUREGROUPCHARACTER'
    group_names = [n for n in dir(u.TextureGroup) if n.replace('_', '') == wanted]
    assert len(group_names) == 1, group_names
    tex.set_editor_property('lod_group', getattr(u.TextureGroup, group_names[0]))
    assert u.EditorAssetLibrary.save_loaded_asset(tex)
    imported[(set_name, parameter)] = tex
    report['textures'].append({'asset': path, 'source': str(source), 'sha256': digest,
                               'srgb': parameter == 'BaseColorTexture', 'normal_dx': is_normal})

instances = []
for set_name in ('MC_Enamel', 'MC_Gum'):
    short = set_name.removeprefix('MC_')
    path = base + '/Materials/MI_DAY03_' + short + '_v08'
    parent = u.load_asset(base + '/Materials/M_Menu' + short + '_v08')
    assert parent
    instance = u.load_asset(path)
    if not instance:
        instance = assets.create_asset(path.rsplit('/', 1)[1], base + '/Materials',
                                       u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
    assert instance
    u.MaterialEditingLibrary.set_material_instance_parent(instance, parent)
    for parameter in channels:
        # UE5.8 source returns false unconditionally from these setters; the
        # effective parameter readback is the actual success condition.
        expected = imported[(set_name, parameter)]
        u.MaterialEditingLibrary.set_material_instance_texture_parameter_value(instance, parameter, expected)
        assert u.MaterialEditingLibrary.get_material_instance_texture_parameter_value(instance, parameter) == expected
    u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(instance, 'BaseColorTint', u.LinearColor(1., 1., 1., 1.))
    actual = u.MaterialEditingLibrary.get_material_instance_vector_parameter_value(instance, 'BaseColorTint')
    assert max(abs(actual.r-1.), abs(actual.g-1.), abs(actual.b-1.)) < 1e-5
    u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance, 'RoughnessScale', 1.)
    assert abs(u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(instance, 'RoughnessScale')-1.) < 1e-5
    assert u.EditorAssetLibrary.save_loaded_asset(instance)
    instances.append(instance)
    report['instances'].append(path)

actors = u.GameplayStatics.get_all_actors_with_tag(world, 'Menu3D_DAY03')
assert len(actors) == 1
component = actors[0].get_component_by_class(u.SkeletalMeshComponent)
assert component.get_editor_property('skeletal_mesh_asset') == mesh
assert component.get_num_materials() == 2
report['previous_component_materials'] = [component.get_material(i).get_path_name() for i in range(2)]
for slot, material in enumerate(instances):
    component.set_material(slot, material)
slots = list(mesh.get_editor_property('materials'))
assert len(slots) == 2
report['previous_mesh_default_materials'] = [
    slot.get_editor_property('material_interface').get_path_name()
    if slot.get_editor_property('material_interface') else None for slot in slots]
for slot, material in zip(slots, instances):
    slot.set_editor_property('material_interface', material)
mesh.set_editor_property('materials', slots)
assert u.EditorAssetLibrary.save_loaded_asset(mesh)
assert levels.save_current_level()
report['validation'] = 'Painter hashes/colorspaces and saved binding checked; native look/motion still pending.'
(root / 'Tools/Unreal/Menu3D/Evidence/v08_pbr_binding_report.json').write_text(
    json.dumps(report, indent=2), encoding='utf8')
u.log('MC_V08_PBR_BOUND ' + json.dumps(report))
