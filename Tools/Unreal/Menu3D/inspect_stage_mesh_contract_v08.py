"""Read current saved stage mesh bounds and rig identities without authoring."""
import json
from pathlib import Path
import unreal as u

world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world
result = {'world': world.get_path_name(), 'mouths': []}
for day in (1, 2, 3):
    actors = u.GameplayStatics.get_all_actors_with_tag(world, 'Menu3D_DAY' + str(day).zfill(2))
    assert len(actors) == 1
    component = actors[0].get_component_by_class(u.SkeletalMeshComponent)
    mesh = component.get_editor_property('skeletal_mesh_asset')
    bounds = mesh.get_bounds()
    result['mouths'].append({'day': day, 'mesh': mesh.get_path_name(),
                             'skeleton': mesh.get_editor_property('skeleton').get_path_name(),
                             'bounds_origin': [bounds.origin.x, bounds.origin.y, bounds.origin.z],
                             'bounds_extent': [bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z],
                             'morphs': [str(n) for n in mesh.get_all_morph_target_names()]})
out = Path(u.Paths.project_dir()) / 'Tools/Unreal/Menu3D/Evidence/v08_stage_mesh_contract.json'
out.write_text(json.dumps(result, indent=2), encoding='utf8')
u.log('MC_STAGE_MESH_CONTRACT ' + json.dumps(result))
