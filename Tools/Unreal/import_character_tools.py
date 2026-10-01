"""Import Character.blend's held props, validate them, then update game profiles."""
import json
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = ROOT / 'ArtSource/CharacterCurrent/Tools'
DEST = '/Game/Gameplay/CharacterCurrent/Tools'
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
report = json.loads((SOURCE / 'ToolReport.json').read_text(encoding='utf8'))
parent = lib.load_asset('/Game/Art/Materials/MM_Standart')
appearance = lib.load_asset('/Game/Data/DA_PlayerAppearance')
equipment = lib.load_asset('/Game/Data/DA_Equipment')
assert parent and appearance and equipment
assert {'ARM', 'Albedo Texture', 'Normal Texture'} <= {
    str(n) for n in edit.get_texture_parameter_names(parent)}
u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
meshes = {}
for entry in report['tools']:
    name = entry['name']
    task = u.AssetImportTask()
    task.filename = str(SOURCE / (name + '.fbx'))
    task.destination_path = DEST
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.replace_existing_settings = True
    task.save = True
    task.factory = u.FbxFactory()
    options = u.FbxImportUI()
    options.import_mesh = True
    options.import_as_skeletal = False
    options.mesh_type_to_import = u.FBXImportType.FBXIT_STATIC_MESH
    options.import_materials = False
    options.import_textures = False
    options.import_animations = False
    data = options.static_mesh_import_data
    data.set_editor_property('combine_meshes', True)
    data.set_editor_property('auto_generate_collision', False)
    data.set_editor_property('normal_import_method', u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    task.options = options
    assets.import_asset_tasks([task])
    mesh = lib.load_asset(DEST + '/' + name)
    assert isinstance(mesh, u.StaticMesh)
    bounds = mesh.get_bounds()
    actual = [bounds.box_extent.x * 2, bounds.box_extent.y * 2, bounds.box_extent.z * 2]
    expected = [hi - lo for lo, hi in zip(*entry['bounds_cm'])]
    assert all(abs(a - b) < .01 for a, b in zip(actual, expected)), (name, actual, expected)
    slots = mesh.get_editor_property('static_materials')
    assert len(slots) == 1
    textures = {}
    for kind, filename in entry['textures'].items():
        texture_task = u.AssetImportTask()
        texture_task.filename = str(SOURCE / filename)
        texture_task.destination_path = DEST + '/Textures'
        texture_task.destination_name = Path(filename).stem
        texture_task.automated = True
        texture_task.replace_existing = True
        texture_task.save = True
        assets.import_asset_tasks([texture_task])
        texture = lib.load_asset(texture_task.destination_path + '/' + texture_task.destination_name)
        assert isinstance(texture, u.Texture2D)
        texture.set_editor_property('srgb', kind == 'BaseColor')
        texture.set_editor_property('compression_settings',
            u.TextureCompressionSettings.TC_NORMALMAP if kind == 'Normal' else
            u.TextureCompressionSettings.TC_MASKS if kind != 'BaseColor' else
            u.TextureCompressionSettings.TC_DEFAULT)
        if kind == 'Normal':
            texture.set_editor_property('flip_green_channel', True)
        assert lib.save_loaded_asset(texture, only_if_is_dirty=False)
        textures[kind] = texture
    material_name = 'MI_' + name[3:]
    path = DEST + '/Materials/' + material_name
    material = lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset(
        material_name, DEST + '/Materials', u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(material, parent)
    for parameter, kind in [('Albedo Texture', 'BaseColor'), ('Normal Texture', 'Normal'),
                            ('ARM', 'OcclusionRoughnessMetallic')]:
        edit.set_material_instance_texture_parameter_value(material, parameter, textures[kind])
        assert edit.get_material_instance_texture_parameter_value(material, parameter) == textures[kind]
    for parameter in ('Metalic', 'Roughness Strength'):
        edit.set_material_instance_scalar_parameter_value(material, parameter, 1)
    edit.update_material_instance(material)
    assert lib.save_loaded_asset(material, only_if_is_dirty=False)
    mesh.set_material(0, material)
    if name == 'SM_Spray':
        socket = mesh.find_socket('SprayNozzle')
        if not socket:
            socket = u.StaticMeshSocket(outer=mesh)
            socket.set_editor_property('socket_name', 'SprayNozzle')
            mesh.add_socket(socket)
        # Emit at the cap, above the artist's bottle, rather than at the old
        # primitive spray can's hard-coded nozzle position.
        socket.set_editor_property('relative_location', u.Vector(0, 0, entry['bounds_cm'][1][2] + 1))
    assert lib.save_loaded_asset(mesh, only_if_is_dirty=False)
    meshes[name] = mesh
    u.log('MC_TOOL_IMPORTED ' + name + ' dimensions_cm=' + str(actual))

# The current brush has exactly the old local bounds and +X/-Z contact axes.
# Keep the existing grip and the procedural bristle point (72,0,-20).
old = lib.load_asset('/Game/Art/Meshes/SM_Brush').get_bounds()
new = meshes['SM_Brush'].get_bounds()
assert old.origin.distance(new.origin) < .01 and old.box_extent.distance(new.box_extent) < .01
appearance.set_editor_property('brush_mesh', meshes['SM_Brush'])
equipment.set_editor_property('pickaxe_mesh', meshes['SM_Pick'])
size = meshes['SM_Pick'].get_bounds().box_extent.x * 2
scale = 85 / size
# Put the palm fifteen centimetres along the handle, with the grip at the
# brush pivot. Artist scene transforms would place the tool metres away.
equipment.set_editor_property('pickaxe_transform', u.Transform(
    location=[-15 * scale, 0, 0], rotation=[0, 0, 0], scale=[scale, scale, scale]))
equipment.set_editor_property('spray_mesh', meshes['SM_Spray'])
equipment.set_editor_property('spray_transform', u.Transform(
    location=[0, 0, -6], rotation=[0, 0, 0], scale=[1, 1, 1]))
assert lib.save_loaded_asset(appearance, only_if_is_dirty=False)
assert lib.save_loaded_asset(equipment, only_if_is_dirty=False)
u.log('MC_TOOLS_IMPORT_PASS brush=profile pickaxe=profile spray=profile')
