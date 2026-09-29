"""Make the visual mouth shell stop the camera without changing player collision."""
import unreal as u
lib=u.EditorAssetLibrary
mesh=lib.load_asset('/Game/Gameplay/Throat/SM_SoftPalate')
body=mesh.get_editor_property('body_setup')
body.set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
body.set_editor_property('double_sided_geometry',True)
lib.save_loaded_asset(mesh,False)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
for a in actors.get_all_level_actors():
    if a.get_actor_label()=='ART | Sculpted palate and cheeks':
        c=a.static_mesh_component
        c.set_collision_enabled(u.CollisionEnabled.QUERY_ONLY)
        c.set_collision_response_to_all_channels(u.CollisionResponseType.ECR_IGNORE)
        c.set_collision_response_to_channel(u.CollisionChannel.ECC_CAMERA,u.CollisionResponseType.ECR_BLOCK)
        c.recreate_physics_state() if hasattr(c,'recreate_physics_state') else None
u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
u.log('MC_MOUTH_CAMERA_COLLISION_FIXED')
