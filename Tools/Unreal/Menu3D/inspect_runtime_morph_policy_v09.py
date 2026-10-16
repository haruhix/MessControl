"""Read real BeginPlay tick policy, not editor defaults."""
import json
from pathlib import Path
import unreal as u
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world()
assert world and not getattr(u,'_mc_lower_review_handle',None)
pc=u.GameplayStatics.get_player_controller(world,0)
report={'pc':pc.get_path_name(),'pc_tick_group':str(pc.get_editor_property('primary_actor_tick').get_editor_property('TickGroup')),'rows':[]}
for day in (1,2,3):
    actor=u.GameplayStatics.get_all_actors_with_tag(world,f'Menu3D_DAY{day:02d}')[0]
    mesh=actor.get_component_by_class(u.SkeletalMeshComponent)
    tick=mesh.get_editor_property('primary_component_tick')
    report['rows'].append({'day':day,'tick_group':str(tick.get_editor_property('TickGroup')),
        'end_tick_group':str(tick.get_editor_property('EndTickGroup')),
        'visibility_based_anim_tick_option':str(mesh.get_editor_property('visibility_based_anim_tick_option')),
        'URO':mesh.get_editor_property('enable_update_rate_optimizations'),
        'tick_enabled':mesh.is_component_tick_enabled(),'anim_instance':str(mesh.get_anim_instance()),
        'animation_mode':str(mesh.get_editor_property('animation_mode'))})
(Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/v09_runtime_morph_policy.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_RUNTIME_MORPH_POLICY '+json.dumps(report))
