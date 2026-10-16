"""Save the runtime-reviewed late pose tick on the three menu mouth components."""
import json
from pathlib import Path
import unreal as u

root=Path(u.Paths.project_dir())
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0]=='/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
out=root/'Tools/Unreal/Menu3D/Evidence/v09_mouth_morph_tick_sync_report.json'
assert not out.exists()
candidate=root/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute/LowerRow_v09_InteriorSync06_PostUpdate/native_route_actual_states.json'
review=json.loads(candidate.read_text(encoding='utf8'))
assert review['error'] is None and review['temporary_mouth_tick_group']=='TG_POST_UPDATE_WORK'

def snapshot(actor,mesh):
    return {'actor_transform':str(actor.get_actor_transform()),
            'relative_transform':str(mesh.get_relative_transform()),
            'mesh':mesh.get_editor_property('skeletal_mesh_asset').get_path_name(),
            'materials':[mesh.get_material(i).get_path_name() for i in range(mesh.get_num_materials())]}

rows=[]
for day in (1,2,3):
    actor=u.GameplayStatics.get_all_actors_with_tag(world,f'Menu3D_DAY{day:02d}')[0]
    mesh=actor.get_component_by_class(u.SkeletalMeshComponent)
    previous=mesh.get_editor_property('primary_component_tick').get_editor_property('TickGroup')
    assert previous!=u.TickingGroup.TG_POST_UPDATE_WORK
    rows.append((day,actor,mesh,previous,snapshot(actor,mesh)))
changes=[]
for day,actor,mesh,previous,before in rows:
    actor.modify();mesh.modify()
    mesh.set_tick_group(u.TickingGroup.TG_POST_UPDATE_WORK)
    actual=mesh.get_editor_property('primary_component_tick').get_editor_property('TickGroup')
    assert actual==u.TickingGroup.TG_POST_UPDATE_WORK
    assert snapshot(actor,mesh)==before
    changes.append({'day':day,'component':mesh.get_path_name(),'before':str(previous),'after':str(actual),
                    'transforms_mesh_materials_unchanged':True})
assert levels.save_current_level()
report={'status':'THREE_MOUTH_LATE_POSE_TICKS_SAVED_FRESH_NATIVE_REVIEW_PENDING','changes':changes,
        'scope':'Only three work-map skeletal mouth component TickGroup values; no character/animation/graph/config/C++ changes',
        'existing_PC_tick_prerequisites_retained':True,
        'actual_runtime_candidate':'LowerRow_v09_InteriorSync06_PostUpdate'}
out.write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_MOUTH_MORPH_TICK_SYNC_SAVED '+json.dumps(report))
