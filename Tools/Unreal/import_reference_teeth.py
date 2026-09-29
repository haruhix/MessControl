"""Use the refined enamel meshes with the existing brushing and damage material."""
import unreal as u
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve(); lib=u.EditorAssetLibrary; assets=u.AssetToolsHelpers.get_asset_tools(); edit=u.MaterialEditingLibrary
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
folder='/Game/Gameplay/Throat/Teeth'; tasks=[]
for file in (root/'ArtSource/LivingThroat/Teeth').glob('*.fbx'):
    task=u.AssetImportTask(); task.filename=str(file); task.destination_path=folder; task.destination_name=file.stem
    task.automated=True; task.replace_existing=True; task.save=True; task.factory=u.FbxFactory()
    opt=u.FbxImportUI(); opt.import_mesh=True; opt.import_as_skeletal=False; opt.import_materials=False; opt.import_textures=False; opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
    opt.static_mesh_import_data.combine_meshes=True; opt.static_mesh_import_data.auto_generate_collision=False
    task.options=opt; tasks.append(task)
assets.import_asset_tasks(tasks)
name='MI_ArenaToothReference'; path='/Game/Art/Materials/'+name
mat=lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset(name,'/Game/Art/Materials',u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
edit.set_material_instance_parent(mat,lib.load_asset('/Game/Gameplay/Care/M_ArenaToothCare'))
edit.set_material_instance_vector_parameter_value(mat,'Tint',u.LinearColor(.98,.95,.87,1))
edit.set_material_instance_scalar_parameter_value(mat,'Roughness',.20)
edit.update_material_instance(mat); lib.save_loaded_asset(mat,only_if_is_dirty=False)
for task in tasks:
    mesh=lib.load_asset(folder+'/'+task.destination_name); mesh.set_editor_property('allow_cpu_access',True)
    mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    for i in range(len(mesh.get_editor_property('static_materials'))): mesh.set_material(i,mat)
    lib.save_loaded_asset(mesh,only_if_is_dirty=False)
profile=lib.load_asset('/Game/Data/DA_ArenaTooth'); profile.set_editor_property('gameplay_material',mat); lib.save_loaded_asset(profile,only_if_is_dirty=False)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
for a in actors.get_all_level_actors():
    if isinstance(a,u.MCArenaToothSocket):
        c=a.get_editor_property('preview'); old=c.static_mesh.get_name(); name=old.replace('SM_ArenaTooth','SM_ReferenceTooth')
        mesh=lib.load_asset(folder+'/'+name)
        if mesh:c.set_static_mesh(mesh); c.set_material(0,mat)
    elif a.get_actor_label().startswith('ARENA | Far'):
        c=a.get_component_by_class(u.StaticMeshComponent)
        old=c.static_mesh.get_bounds().box_extent; mesh=lib.load_asset(folder+'/SM_ReferenceTooth_05'); new=mesh.get_bounds().box_extent
        scale=c.get_relative_scale3d(); c.set_relative_scale3d(u.Vector(scale.x*old.x/new.x,scale.y*old.y/new.y,scale.z*old.z/new.z*.77))
        c.set_static_mesh(mesh); c.set_material(0,mat)
u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
u.log('MC_REFERENCE_TEETH_IMPORTED')
