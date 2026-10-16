"""Two actual engine-render screenshots for projected mouth-width inspection."""
import json
import time
from pathlib import Path
import unreal as u
world=u.EditorLevelLibrary.get_game_world();assert world
cls=u.load_class(None,'/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C')
lib=u.get_default_object(u.load_class(None,'/Script/UMG.WidgetBlueprintLibrary'))
widgets=lib.call_method('GetAllWidgetsOfClass',args=(world,cls,False));assert len(widgets)==1
widget=widgets[0]
request_path=Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/width_capture_request.json'
request=json.loads(request_path.read_text(encoding='utf8')) if request_path.is_file() else {}
run_name=request.get('run_name','WidthFit03')
assert Path(run_name).name==run_name and run_name.startswith('WidthFit')
out=Path(u.Paths.project_dir())/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute'/run_name
out.mkdir(parents=True,exist_ok=True)
assert not (out/'capture_states.json').exists(),'Preserve prior capture evidence; choose a fresh run name'
assert not getattr(u,'_mc_width_fit_handle',None)
state={'started':time.monotonic(),'index':0,'actions':[]}
schedule=[(0.,'catalog'),(1.5,'closed'),(2.5,'open3'),(4.,'opened'),(5.,'finish')]
def tick(delta):
    try:
        elapsed=time.monotonic()-state['started']
        widget.call_method('ResetPointerIntent')
        while state['index']<len(schedule) and elapsed>=schedule[state['index']][0]:
            at,action=schedule[state['index']];state['index']+=1
            state['actions'].append({'action':action,'time':elapsed})
            if action=='catalog':widget.get_editor_property('PlayButton').on_clicked.broadcast()
            elif action=='open3':widget.call_method('SelectDay',args=(3,))
            elif action=='finish':
                u.unregister_slate_post_tick_callback(u._mc_width_fit_handle);u._mc_width_fit_handle=None
                (out/'capture_states.json').write_text(json.dumps(state,indent=2),encoding='utf8')
            else:
                state[action]={'open3':widget.get_editor_property('Open3'),
                    'scale':[str(a.get_actor_scale3d()) for a in u.GameplayStatics.get_all_actors_with_tag(world,'Menu3D_DAY03')]}
                u.SystemLibrary.execute_console_command(world,'Shot SHOWUI filename='+(out/(action+'.png')).as_posix()+' -nosuffix')
    except Exception:
        u.unregister_slate_post_tick_callback(u._mc_width_fit_handle);u._mc_width_fit_handle=None
        raise
u._mc_width_fit_handle=u.register_slate_post_tick_callback(tick)
