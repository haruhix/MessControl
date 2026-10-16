"""Read actual seven-mouth keepalive/collision policy after capture cleanup."""
import json
from pathlib import Path
import unreal as u
assert not getattr(u,'_mc_lower_review_handle',None)
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world();assert world
rows=[]
for day in range(1,8):
    actors=u.GameplayStatics.get_all_actors_with_tag(world,f'Menu3D_DAY{day:02d}')
    assert len(actors)==1
    actor=actors[0];mesh=actor.get_component_by_class(u.SkeletalMeshComponent)
    row={'day':day,'actor_hidden':actor.get_editor_property('hidden'),
        'component_hidden':mesh.get_editor_property('hidden_in_game'),
        'component_visible':mesh.is_visible(),'collision':str(mesh.get_collision_enabled()),
        'mesh':mesh.get_editor_property('skeletal_mesh_asset').get_path_name(),
        'materials':[]}
    for material in mesh.get_materials():
        row['materials'].append({'path':material.get_path_name(),
            'half_height':material.get_scalar_parameter_value('CardHalfHeight')})
    rows.append(row)
result={'rows':rows,'force_update':u.SystemLibrary.get_console_variable_int_value('r.MorphTarget.ForceUpdate'),
    'morph_threshold':u.SystemLibrary.get_console_variable_float_value('r.MorphTarget.WeightThreshold'),
    'vsm_after_capture':u.SystemLibrary.get_console_variable_int_value('r.Shadow.Virtual.Enable')}
assert all(not r['actor_hidden'] and not r['component_hidden'] and r['component_visible'] for r in rows)
assert all('NO_COLLISION' in r['collision'] for r in rows)
assert result['force_update']==0 and result['vsm_after_capture']==0
target=Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/v09_mouth_keepalive_runtime_policy.json'
assert not target.exists()
target.write_text(json.dumps(result,indent=2),encoding='utf8')
u.log('MC_KEEPALIVE_RUNTIME '+json.dumps(result))
