"""Final wet highlights and rounded uvula import, preserving material graphs."""
import unreal as u
from pathlib import Path
lib=u.EditorAssetLibrary;edit=u.MaterialEditingLibrary;root=Path(u.Paths.project_dir()).resolve()
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
task=u.AssetImportTask();task.filename=str(root/'ArtSource/LivingThroat/SM_Uvula.fbx');task.destination_path='/Game/Gameplay/Throat';task.destination_name='SM_Uvula'
task.automated=True;task.replace_existing=True;task.save=True;task.factory=u.FbxFactory()
opt=u.FbxImportUI();opt.import_mesh=True;opt.import_as_skeletal=False;opt.import_materials=False;opt.import_textures=False;opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
opt.static_mesh_import_data.combine_meshes=True;opt.static_mesh_import_data.auto_generate_collision=False
task.options=opt;u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
mesh=lib.load_asset('/Game/Gameplay/Throat/SM_Uvula')
for i in range(len(mesh.get_editor_property('static_materials'))):mesh.set_material(i,lib.load_asset('/Game/Art/Materials/MI_MouthPalate'))
lib.save_loaded_asset(mesh,False)
for name in ('M_LivingTissue','M_ThroatSculpt'):
    mat=lib.load_asset('/Game/Art/Materials/'+name)
    for node in edit.get_material_expressions(mat):
        if isinstance(node,u.MaterialExpressionCustom) and str(node.get_editor_property('description'))=='Wet film':
            node.set_editor_property('code','return clamp(ORM.g+Bias,.12,.36);')
    edit.recompile_material(mat);lib.save_loaded_asset(mat,False)
for name in ('MI_LivingThroat','MI_MouthPalate','MI_MouthCheek'):
    mi=lib.load_asset('/Game/Art/Materials/'+name)
    edit.set_material_instance_scalar_parameter_value(mi,'RoughnessBias',-.085)
    edit.update_material_instance(mi);lib.save_loaded_asset(mi,False)
u.log('MC_MOUTH_HIGHLIGHTS_FINISHED')
