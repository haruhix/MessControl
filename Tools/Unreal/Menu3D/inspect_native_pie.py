"""Solo PIE inspection only; no gameplay travel, connection or package save."""
import json
from pathlib import Path
import unreal as u
world = u.EditorLevelLibrary.get_game_world()
assert world
cls = u.load_class(None,'/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C')
library = u.get_default_object(u.load_class(None,'/Script/UMG.WidgetBlueprintLibrary'))
widgets = library.call_method('GetAllWidgetsOfClass',args=(world,cls,False))
report = {'world':world.get_path_name(),'widgets':[], 'actors':[]}
for widget in widgets:
    report['widgets'].append({'path':widget.get_path_name(),'methods':[n for n in dir(widget) if any(s in n for s in ('show_page','select_day','back','stage','page_index','active_day','open3','scroll'))],
        'page':widget.get_editor_property('PageIndex'),'active':widget.get_editor_property('ActiveDay'),'open3':widget.get_editor_property('Open3')})
for actor in u.GameplayStatics.get_all_actors_with_tag(world,'Menu3D_DAY03'):
    comp = actor.get_component_by_class(u.SkeletalMeshComponent)
    report['actors'].append({'name':actor.get_name(),'loc':str(actor.get_actor_location()),'scale':str(actor.get_actor_scale3d()),'morphs':{n:comp.get_morph_target(n) for n in ('Pose_000','Pose_025','Pose_050','Pose_075')}})
out=Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence'
(out/'actual_solo_pie_inspection.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_MENU3D_ACTUAL_PIE '+json.dumps(report))
