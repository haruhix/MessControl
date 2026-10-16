"""Read-only saved/runtime cleanup receipt for the interior continuity repair."""
import json
from pathlib import Path
import unreal as u

root=Path(u.Paths.project_dir())
evidence=root/'Tools/Unreal/Menu3D/Evidence'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
assert all(not getattr(u,name,None) for name in ('_mc_lower_review_handle','_mc_organic_detail_handle','_mc_interior_solo_check'))
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0]=='/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
motion=json.loads((evidence/'v09_interior_own_jaw_mask_continuity_solo.json').read_text(encoding='utf8'))
assert motion['validation']['status']=='PASS'
window=json.loads((evidence/'v09_review_window_own_jaw_session.json').read_text(encoding='utf8'))
assert window['restored']
vsm=u.SystemLibrary.get_console_variable_int_value('r.Shadow.Virtual.Enable')
assert vsm==0
report={'status':'INTERIOR_CONTINUITY_REPAIR_SAVED_PENDING_FINAL_ART_APPROVAL',
    'world':world.get_path_name(),'pie_running':False,'active_review_callbacks':False,
    'vsm_restored':vsm,'session_window_preferences_restored':True,
    'dirty_maps':[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
    'dirty_content':[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
    'normal_speed_numeric_check':motion['validation'],
    'render_review':'LowerRow_v09_InteriorEnvelope05_OwnMask_r06 (controlled functions, temporary VSM1, mixed slow/normal time)',
    'physical_mouse_wheel':'Not exercised; computer-use stopped by physical Escape',
    'production_startup_unchanged':True,'build_cook_network_tests_run':False}
assert not report['dirty_maps']
(evidence/'v09_interior_own_jaw_mask_final_checkpoint.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_INTERIOR_FINAL_CHECKPOINT '+json.dumps(report))
