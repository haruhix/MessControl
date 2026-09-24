"""Import the Blender rig and build the authored seven-body Physics Asset. Run in Unreal."""
from pathlib import Path
import unreal as ue

root = Path(ue.Paths.project_dir()).resolve()
lib = ue.EditorAssetLibrary
assets = ue.AssetToolsHelpers.get_asset_tools()
lib.make_directory('/Game/Art/Rig')
ue.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
task = ue.AssetImportTask()
task.filename = str(root / 'ArtSource/Exports/SK_ToothHero.fbx')
task.destination_path = '/Game/Art/Rig'
task.destination_name = 'SK_ToothHero'
task.automated = True
task.replace_existing = True
task.save = True
options = ue.FbxImportUI()
options.import_mesh = True
options.import_as_skeletal = True
options.mesh_type_to_import = ue.FBXImportType.FBXIT_SKELETAL_MESH
options.import_materials = False
options.import_textures = False
options.import_animations = False
options.create_physics_asset = False
options.skeletal_mesh_import_data.set_editor_property('import_morph_targets', True)
options.skeletal_mesh_import_data.set_editor_property('import_meshes_in_bone_hierarchy', True)
options.skeletal_mesh_import_data.set_editor_property('use_t0_as_ref_pose', False)
task.options = options
task.factory = ue.FbxFactory()
assets.import_asset_tasks([task])
mesh = lib.load_asset('/Game/Art/Rig/SK_ToothHero')
if not isinstance(mesh, ue.SkeletalMesh):
    raise RuntimeError('Skeletal import failed')
slots = mesh.get_editor_property('materials')
for i,slot in enumerate(slots):
    name = str(slot.get_editor_property('material_slot_name'))
    material = lib.load_asset('/Game/Art/Materials/M_' + name)
    ue.log('MC_RIG_MATERIAL: '+name+' -> '+str(material))
    if material:
        ue.MaterialEditingLibrary.set_base_material_usage(material, ue.MaterialUsage.MATUSAGE_SKELETAL_MESH)
        ue.MaterialEditingLibrary.set_base_material_usage(material, ue.MaterialUsage.MATUSAGE_MORPH_TARGETS)
        lib.save_loaded_asset(material, only_if_is_dirty=False)
        slot.set_editor_property('material_interface', material)
        slots[i]=slot
mesh.set_editor_property('materials', slots)
physics = ue.MCPhysicsAssetBuilder.build_tooth_physics_asset(mesh)
if not physics:
    raise RuntimeError('Physics Asset generation failed')
lib.save_loaded_asset(mesh, only_if_is_dirty=False)
if not lib.does_asset_exist('/Game/Data/DA_ToothPhysics'):
    factory = ue.DataAssetFactory()
    factory.set_editor_property('data_asset_class', ue.MCPhysicsProfile)
    profile = assets.create_asset('DA_ToothPhysics', '/Game/Data', ue.MCPhysicsProfile, factory)
    lib.save_loaded_asset(profile)
lib.save_directory('/Game/Art/Rig', only_if_is_dirty=True)
ue.log('MC_RIG_IMPORTED: skeletal mesh, skeleton, morphs and Physics Asset saved')
