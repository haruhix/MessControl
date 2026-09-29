import unreal as u
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve(); lib=u.EditorAssetLibrary; assets=u.AssetToolsHelpers.get_asset_tools()
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
task=u.AssetImportTask(); task.filename=str(root/'ArtSource/LivingThroat/SM_SoftPalate.fbx'); task.destination_path='/Game/Gameplay/Throat'; task.destination_name='SM_SoftPalate'
task.automated=True; task.replace_existing=True; task.save=True; task.factory=u.FbxFactory()
opt=u.FbxImportUI(); opt.import_mesh=True; opt.import_as_skeletal=False; opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
opt.import_materials=False; opt.import_textures=False
opt.static_mesh_import_data.combine_meshes=True; opt.static_mesh_import_data.auto_generate_collision=False
task.options=opt; assets.import_asset_tasks([task])
mesh=lib.load_asset('/Game/Gameplay/Throat/SM_SoftPalate')
for slot in range(len(mesh.get_editor_property('static_materials'))): mesh.set_material(slot,lib.load_asset('/Game/Art/Materials/MI_MouthPalate'))
lib.save_loaded_asset(mesh,only_if_is_dirty=False)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
for a in actors.get_all_level_actors():
    if a.get_actor_label() in ('SM_Wall_01','SM_Hole'):
        a.set_actor_hidden_in_game(True); a.set_is_temporarily_hidden_in_editor(True)
actor=next((a for a in actors.get_all_level_actors() if a.get_actor_label()=='ART | Sculpted palate and cheeks'),None)
if actor is None: actor=actors.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,-30,-240))
actor.set_actor_location(u.Vector(0,-30,-240),False,True)
actor.set_actor_label('ART | Sculpted palate and cheeks'); actor.set_folder_path('Art/Mouth')
actor.static_mesh_component.set_static_mesh(mesh); actor.static_mesh_component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
u.log('MC_SOFT_PALATE_IMPORTED')
