import unreal as u
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith('/Game/UI/MainMenu3D/L_MainMenu3D')
for actor in u.GameplayStatics.get_all_actors_with_tag(world,'Menu3D_Camera'):
    actor.set_actor_rotation(u.Rotator(pitch=0.,yaw=-90.,roll=0.),False)
    levels.pilot_level_actor(actor)
for actor in u.GameplayStatics.get_all_actors_with_tag(world,'Menu3D_DAY03_Character'):
    actor.set_actor_rotation(u.Rotator(pitch=0.,yaw=195.,roll=0.),False)
    component = actor.get_component_by_class(u.SkeletalMeshComponent)
    clip = u.load_asset('/Game/Gameplay/Character/Animations/Teeth3/A_Teeth_Hello1')
    component.override_animation_data(clip,True,True,.75,1.)
assert levels.save_current_level()
u.log('MC_MENU3D_NATIVE_CAMERA_REPAIRED')
