"""Confirm the completed native review restored normal interactive runtime state."""
import json
from pathlib import Path
import unreal as u

assert not getattr(u,'_mc_lower_review_handle',None),'Wait for the review to finish'
world=u.EditorLevelLibrary.get_game_world();assert world
cls=u.load_class(None,'/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C')
lib=u.get_default_object(u.load_class(None,'/Script/UMG.WidgetBlueprintLibrary'))
widgets=lib.call_method('GetAllWidgetsOfClass',args=(world,cls,False));assert len(widgets)==1
widget=widgets[0]
mouth=u.GameplayStatics.get_all_actors_with_tag(world,'Menu3D_DAY03')[0]
mesh=mouth.get_component_by_class(u.SkeletalMeshComponent).get_editor_property('skeletal_mesh_asset')
dilation=u.GameplayStatics.get_global_time_dilation(world)
visibility=widget.get_visibility()
assert abs(dilation-1.)<1e-6,dilation
assert visibility!=u.SlateVisibility.HIT_TEST_INVISIBLE,visibility
assert mesh.get_path_name().endswith('SK_DAY03_LowerReadable_v07.SK_DAY03_LowerReadable_v07')
report={'status':'NORMAL_INTERACTIVE_RUNTIME_RESTORED','mesh':mesh.get_path_name(),
    'global_time_dilation':dilation,'root_visibility':str(visibility),
    'page':widget.get_editor_property('PageIndex'),'selected':widget.get_editor_property('SelectedDay'),
    'active':widget.get_editor_property('ActiveDay'),'open3':widget.get_editor_property('Open3'),
    'scale':str(mouth.get_actor_scale3d()),'children':[]}
for suffix in ('Interior','Tongue','Character'):
    actor=u.GameplayStatics.get_all_actors_with_tag(world,'Menu3D_DAY03_'+suffix)[0]
    component=actor.get_editor_property('root_component')
    report['children'].append({'tag':suffix,'relative_scale':str(component.get_editor_property('relative_scale3d'))})
request=json.loads((Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/lower_review_request.json').read_text(encoding='utf8'))
run_name=request['run_name'];assert Path(run_name).name==run_name
out=Path(u.Paths.project_dir())/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute'/run_name
(out/'runtime_restoration.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.SystemLibrary.execute_console_command(world,'Shot SHOWUI filename='+(out/'final_native.png').as_posix()+' -nosuffix')
u.log('MC_LOWER_REVIEW_RUNTIME '+json.dumps(report))
