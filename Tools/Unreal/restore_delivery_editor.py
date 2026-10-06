"""Restore the editor camera and inspect new native guides without saving the map."""
import json
from pathlib import Path
import unreal

state = json.loads((Path(unreal.Paths.project_saved_dir()) / 'DeliveryZonesEditorState.json').read_text(encoding='utf-8'))
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
editor.set_level_viewport_camera_info(unreal.Vector(*state['location']), unreal.Rotator(*state['rotation']))
rows = []
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if 'Throat' in actor.get_class().get_name() or 'FoodDisposal' in actor.get_class().get_name():
        guides = actor.get_components_by_class(unreal.load_class(None, '/Script/MessControl.MCDeliveryZoneVisualComponent'))
        rows.append(dict(actor=actor.get_actor_label(), native_guide=len(guides)))
        assert guides, actor.get_path_name()
assert unreal.load_asset('/Game/Gameplay/Delivery/M_DeliveryZoneGuide')
unreal.log('MC_DELIVERY_EDITOR_READY ' + json.dumps(rows))
