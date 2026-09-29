"""Reimport only the refined uvula, preserving the level and tissue materials."""
import unreal as u
from pathlib import Path

root = Path(u.Paths.project_dir()).resolve()
lib = u.EditorAssetLibrary
u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
task = u.AssetImportTask()
task.filename = str(root/'ArtSource/LivingThroat/SM_Uvula.fbx')
task.destination_path = '/Game/Gameplay/Throat'
task.destination_name = 'SM_Uvula'
task.automated = True
task.replace_existing = True
task.save = True
task.factory = u.FbxFactory()
opt = u.FbxImportUI()
opt.import_mesh = True
opt.import_as_skeletal = False
opt.import_materials = False
opt.import_textures = False
opt.mesh_type_to_import = u.FBXImportType.FBXIT_STATIC_MESH
opt.static_mesh_import_data.combine_meshes = True
opt.static_mesh_import_data.auto_generate_collision = False
opt.static_mesh_import_data.normal_import_method = u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
task.options = opt
u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
assert task.imported_object_paths, 'Uvula import failed'
mesh = lib.load_asset('/Game/Gameplay/Throat/SM_Uvula')
for i in range(len(mesh.get_editor_property('static_materials'))):
    mesh.set_material(i, lib.load_asset('/Game/Art/Materials/MI_MouthPalate'))
lib.save_loaded_asset(mesh, only_if_is_dirty=False)
u.log('MC_UVULA_ANATOMY_IMPORTED')
