import unreal as u
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();lib=u.EditorAssetLibrary;actors=u.get_editor_subsystem(u.EditorActorSubsystem)
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
for side in ('L','R'):
    name='SM_ReferenceGum_'+side;task=u.AssetImportTask();task.filename=str(root/'ArtSource/LivingThroat'/(name+'.fbx'))
    task.destination_path='/Game/Gameplay/Throat';task.destination_name=name;task.automated=True;task.replace_existing=True;task.save=True;task.factory=u.FbxFactory()
    opt=u.FbxImportUI();opt.import_mesh=True;opt.import_as_skeletal=False;opt.import_materials=False;opt.import_textures=False;opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
    opt.static_mesh_import_data.combine_meshes=True;opt.static_mesh_import_data.auto_generate_collision=False;task.options=opt
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);mesh=lib.load_asset('/Game/Gameplay/Throat/'+name)
    for i in range(len(mesh.get_editor_property('static_materials'))):mesh.set_material(i,lib.load_asset('/Game/Art/Materials/MI_MouthGum'))
    lib.save_loaded_asset(mesh,False)
    label='ART | Continuous gums '+side
    actor=next((a for a in actors.get_all_level_actors() if a.get_actor_label()==label),None)
    if actor is None:actor=actors.spawn_actor_from_class(u.StaticMeshActor,u.Vector())
    actor.set_actor_label(label);actor.set_folder_path('Art/Mouth/Gingiva');actor.static_mesh_component.set_static_mesh(mesh)
    actor.static_mesh_component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
for a in actors.get_all_level_actors():
    if a.get_actor_label() in ('SM_Gum','SM_Gum2') or a.get_actor_label().startswith('ART | Gingiva '):
        a.set_actor_hidden_in_game(True);a.set_is_temporarily_hidden_in_editor(True)
u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level();u.log('MC_CONTINUOUS_GUMS_IMPORTED')
