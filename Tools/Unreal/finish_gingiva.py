import unreal as u
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve(); lib=u.EditorAssetLibrary; assets=u.AssetToolsHelpers.get_asset_tools()
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
task=u.AssetImportTask(); task.filename=str(root/'ArtSource/LivingThroat/SM_GingivalCuff.fbx'); task.destination_path='/Game/Gameplay/Throat'; task.destination_name='SM_GingivalCuff'
task.automated=True; task.replace_existing=True; task.save=True; task.factory=u.FbxFactory()
opt=u.FbxImportUI(); opt.import_mesh=True; opt.import_as_skeletal=False; opt.import_materials=False; opt.import_textures=False; opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
opt.static_mesh_import_data.combine_meshes=True; opt.static_mesh_import_data.auto_generate_collision=False
task.options=opt; assets.import_asset_tasks([task]); mesh=lib.load_asset('/Game/Gameplay/Throat/SM_GingivalCuff')
for i in range(len(mesh.get_editor_property('static_materials'))):mesh.set_material(i,lib.load_asset('/Game/Art/Materials/MI_MouthGum'))
lib.save_loaded_asset(mesh,only_if_is_dirty=False)
actors=u.get_editor_subsystem(u.EditorActorSubsystem); allactors=actors.get_all_level_actors()
for a in allactors:
    if a.get_actor_label()=='Plane2':
        # Keep the existing containment plane, hide its debug grid material in the tunnel.
        a.set_actor_hidden_in_game(True); a.set_is_temporarily_hidden_in_editor(True)
    c=None
    if isinstance(a,u.MCArenaToothSocket):c=a.get_editor_property('preview')
    elif a.get_actor_label().startswith(('ART | Far tooth','ART | Reference row')):
        c=a.static_mesh_component
        name=c.static_mesh.get_name().replace('SM_ArenaTooth','SM_ReferenceTooth')
        replacement=lib.load_asset('/Game/Gameplay/Throat/Teeth/'+name)
        if replacement:c.set_static_mesh(replacement);c.set_material(0,lib.load_asset('/Game/Art/Materials/MI_ArenaToothReference'))
    if c:
        label='ART | Gingiva '+a.get_name()
        cuff=next((x for x in allactors if x.get_actor_label()==label),None)
        if cuff is None:cuff=actors.spawn_actor_from_class(u.StaticMeshActor,c.get_world_location(),c.get_world_rotation())
        cuff.set_actor_label(label);cuff.set_folder_path('Art/Mouth/Gingiva')
        p=c.get_world_location(); cuff.set_actor_location(u.Vector(p.x,p.y,p.z-75),False,False);cuff.set_actor_rotation(c.get_world_rotation(),False)
        b=c.static_mesh.get_bounds().box_extent;s=c.get_world_scale()
        cuff.set_actor_scale3d(u.Vector(b.x*abs(s.x)/100,b.y*abs(s.y)/100,1.25))
        cuff.static_mesh_component.set_static_mesh(mesh);cuff.static_mesh_component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
u.log('MC_GINGIVA_FINISHED')
