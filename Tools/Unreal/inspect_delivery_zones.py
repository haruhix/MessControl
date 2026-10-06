"""Read-only inspection of the currently open MessControl editor and delivery bounds."""
import json
import unreal

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
rows = []
for actor in actors:
    cls = actor.get_class().get_name()
    if any(key in cls for key in ('Throat', 'Tongue', 'FoodDisposal', 'Mouth', 'ArenaDemo')):
        center, extent = actor.get_actor_bounds(False)
        row = dict(name=actor.get_name(), label=actor.get_actor_label(), cls=cls,
                   location=str(actor.get_actor_location()), rotation=str(actor.get_actor_rotation()), scale=str(actor.get_actor_scale3d()),
                   bounds_center=str(center), bounds_extent=str(extent))
        for key in ('b_brush_bin', 'zone_center', 'zone_radius', 'zone_height'):
            try:
                row[key] = str(actor.get_editor_property(key))
            except Exception:
                pass
        for comp in actor.get_components_by_class(unreal.BoxComponent):
            row[comp.get_name()] = dict(location=str(comp.get_world_location()),
                                      extent=str(comp.get_scaled_box_extent()))
        rows.append(row)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
dirty += [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
unreal.log('MC_DELIVERY_INSPECTION ' + json.dumps(dict(world=world.get_path_name() if world else None, dirty=dirty, actors=rows)))
