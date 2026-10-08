"""Set Camera = Ignore on placed teeth/gums without changing gameplay collision."""
import json
from pathlib import Path
import unreal as u

editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
assert not editor.get_game_world(), 'Stop PIE before editing saved level collision'
world = editor.get_editor_world()
assert world and world.get_path_name() == '/Game/Maps/L_Mouth.L_Mouth', str(world)
actors = u.GameplayStatics.get_all_actors_of_class(world, u.Actor)
channels = [getattr(u.CollisionChannel, name) for name in dir(u.CollisionChannel)
            if name.startswith('ECC_') and name != 'ECC_MAX']
rows = []
for actor in actors:
    for component in actor.get_components_by_class(u.StaticMeshComponent):
        mesh = component.static_mesh
        if not mesh or not (mesh.get_name() == 'SM_Gum' or mesh.get_name().startswith('SM_Teeth')):
            continue
        before = [component.get_collision_response_to_channel(channel) for channel in channels]
        enabled, object_type = component.get_collision_enabled(), component.get_collision_object_type()
        actor.modify()
        component.modify()
        # Default mesh collision would replace the custom response on registration.
        component.set_editor_property('use_default_collision', False)
        component.set_collision_response_to_channel(u.CollisionChannel.ECC_CAMERA, u.CollisionResponseType.ECR_IGNORE)
        after = [component.get_collision_response_to_channel(channel) for channel in channels]
        assert all(a == b for channel, a, b in zip(channels, before, after) if channel != u.CollisionChannel.ECC_CAMERA)
        assert component.get_collision_enabled() == enabled and component.get_collision_object_type() == object_type
        rows.append({'actor':actor.get_actor_label(), 'component':component.get_name(), 'mesh':mesh.get_path_name(),
                     'camera_before':str(before[channels.index(u.CollisionChannel.ECC_CAMERA)]), 'camera_after':str(after[channels.index(u.CollisionChannel.ECC_CAMERA)]),
                     'collision_enabled':str(enabled), 'other_channels_preserved':True})
assert any('/SM_Gum.' in row['mesh'] for row in rows), 'No gum mesh found'
assert any('/SM_Teeth.' in row['mesh'] for row in rows), 'No placed tooth mesh found'
assert u.EditorLoadingAndSavingUtils.save_map(world, '/Game/Maps/L_Mouth')
report = Path(u.Paths.project_dir()).resolve()/'Artifacts/Camera/20261009/LocationIgnore/SavedCollision.json'
report.parent.mkdir(parents=True, exist_ok=True)
report.write_text(json.dumps({'map':world.get_path_name(), 'saved':True, 'components':rows}, indent=2), encoding='utf-8')
print('MC_TEETH_GUM_CAMERA_IGNORE', json.dumps({'saved':True, 'components':len(rows)}))
