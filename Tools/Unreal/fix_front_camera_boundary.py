"""Keep the pawn boundary solid while allowing the front overview camera behind it."""
import unreal as u

actors = u.get_editor_subsystem(u.EditorActorSubsystem)
boundary = next(a for a in actors.get_all_level_actors()
                if a.get_actor_label() == 'COLLISION | Front boundary')
component = boundary.static_mesh_component
component.modify()
component.set_collision_profile_name('BlockAll')
component.set_collision_response_to_channel(u.CollisionChannel.ECC_CAMERA, u.CollisionResponseType.ECR_IGNORE)
assert component.get_collision_response_to_channel(u.CollisionChannel.ECC_PAWN) == u.CollisionResponseType.ECR_BLOCK
assert component.get_collision_response_to_channel(u.CollisionChannel.ECC_CAMERA) == u.CollisionResponseType.ECR_IGNORE
assert u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
u.log('MC_FRONT_CAMERA_BOUNDARY_SAVED')
