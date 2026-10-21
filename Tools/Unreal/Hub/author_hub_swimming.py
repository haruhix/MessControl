"""Turn the existing Hub water plane into a passable swimmable water volume."""
import unreal as u,json,shutil
from pathlib import Path
root=Path(u.Paths.project_dir());out=root/'Saved/HubSwimming_20261021'
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
plane=u.find_object(None,'/Game/Maps/Hub.Hub:PersistentLevel.StaticMeshActor_93')
assert plane and isinstance(plane,u.StaticMeshActor)
comp=plane.static_mesh_component
assert any(m and 'MI_Water_Stylized' in m.get_path_name() for m in comp.get_materials())
existing=[a for a in actors.get_all_level_actors() if a.get_actor_label()=='Hub_SwimmingWater']
assert len(existing)<=1
volume=existing[0] if existing else actors.spawn_actor_from_class(u.PhysicsVolume,plane.get_actor_location()-u.Vector(0,0,500),u.Rotator(yaw=plane.get_actor_rotation().yaw))
volume.set_actor_label('Hub_SwimmingWater')
volume.set_folder_path('Hub/Water')
volume.set_editor_property('water_volume',True)
volume.set_editor_property('priority',10)
volume.set_editor_property('fluid_friction',0.3)
volume.set_editor_property('tags',['HubWater','SafeSwimming'])
volume.set_actor_scale3d(u.Vector(25,25,5))
comp.set_collision_profile_name('NoCollision')
comp.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
comp.set_editor_property('generate_overlap_events',False)
comp.set_cast_shadow(False)
plane.set_folder_path('Hub/Water')
tags=list(plane.tags)
if 'HubWater' not in [str(t) for t in tags]:tags.append('HubWater')
plane.set_editor_property('tags',tags)
origin,extent=volume.get_actor_bounds(False)
assert abs(origin.z+extent.z-plane.get_actor_location().z)<0.05
# Record a pre-save checkpoint of the user's current map edits, including water.
assert u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
report={'surface':plane.get_path_name(),'volume':volume.get_path_name(),
 'surface_z':plane.get_actor_location().z,'volume_origin':list(origin.to_tuple()),'volume_extent':list(extent.to_tuple()),
 'surface_collision':str(comp.get_collision_enabled()),'safe_swimming':True}
(out/'integration.json').write_text(json.dumps(report,indent=2))
print('HUB_SWIMMING_AUTHORED '+json.dumps(report))
