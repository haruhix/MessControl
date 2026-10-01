"""Save the gameplay camera volume for the wider mouth overview."""
import unreal as u

level = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not level.is_in_play_in_editor(), 'End PIE before tuning the saved camera volume'
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name() == '/Game/Maps/L_Mouth.L_Mouth'
actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
bounds = [a for a in actors if a.actor_has_tag('MCCameraBounds')]
assert len(bounds) == 1, 'Expected one camera bounds actor'
box = bounds[0].get_component_by_class(u.BoxComponent)
assert box and box.get_collision_enabled() == u.CollisionEnabled.NO_COLLISION
with u.ScopedEditorTransaction('Wider mouth camera overview'):
    bounds[0].modify()
    box.modify()
    bounds[0].set_actor_location(u.Vector(-600, -25, 500), False, False)
    box.set_box_extent(u.Vector(1250, 640, 450), False)
assert level.save_current_level()
print('MC_CAMERA_OVERVIEW_SAVED', bounds[0].get_actor_location(), box.get_editor_property('BoxExtent'))
