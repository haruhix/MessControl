"""Record native DAY03 opening/closing, including an explicitly slowed QA pass.

Uses real Blueprint motion and engine screenshots. No poses are synthesized.
Normal game dilation is restored before normal-speed transitions and on exit.
"""
import json
from pathlib import Path
import time
import unreal as u

root=Path(u.Paths.project_dir())
request=json.loads((root/'Tools/Unreal/Menu3D/Evidence/lower_review_request.json').read_text(encoding='utf8'))
run_name=request['run_name']
assert Path(run_name).name==run_name and run_name.startswith('LowerRow_')
out=root/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute'/run_name
out.mkdir(parents=True,exist_ok=True)
assert not (out/'capture_receipt.json').exists(),'Preserve earlier evidence'
world=u.EditorLevelLibrary.get_game_world();assert world
render_previous={}
for cvar,value in request.get('render_overrides',{}).items():
    assert cvar in ('r.AntiAliasingMethod','r.Shadow.Virtual.Cache','r.Shadow.Virtual.Enable','r.OneFrameThreadLag','r.MorphTarget.ForceUpdate'), cvar
    assert isinstance(value,int)
    render_previous[cvar]=u.SystemLibrary.get_console_variable_int_value(cvar)
quality_previous={}
settings=u.GameUserSettings.get_game_user_settings()
for setting,value in request.get('quality_overrides',{}).items():
    assert setting in ('texture_quality','shadow_quality','post_processing_quality','reflection_quality')
    quality_previous[setting]=getattr(settings,'get_'+setting)()
cls=u.load_class(None,'/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C')
lib=u.get_default_object(u.load_class(None,'/Script/UMG.WidgetBlueprintLibrary'))
widgets=lib.call_method('GetAllWidgetsOfClass',args=(world,cls,False));assert len(widgets)==1
widget=widgets[0]
assert not getattr(u,'_mc_lower_review_handle',None)
default_schedule=[(0.,'ShowPage',0),(1.,'catalog',None),(2.,'dilation',.15),
    (2.2,'SelectDay',3),(6.8,'Back',None),(11.4,'dilation',1.),
    (11.6,'SelectDay',3),(14.2,'StepDay',1),(16.5,'StepDay',-1),
    (19.2,'Back',None),(21.,'Back',None),(22.5,'catalog',None),(24.,'SelectDay',3)]
schedule=request.get('schedule',default_schedule)
duration=float(request.get('duration',26.))
state={'started':time.monotonic(),'started_wall':time.time(),'index':0,'actions':[],'samples':[],
    'source_mesh':request.get('source_mesh'),'scope':'Solo PIE, shared BP functions; physical wheel validated separately',
    'slow_pass':'See recorded dilation actions. Normal dilation restored on exit.',
    'requested_schedule':schedule,'duration':duration}
state['render_overrides']=request.get('render_overrides',{})
state['render_previous']=render_previous
state['quality_overrides']=request.get('quality_overrides',{})
state['quality_previous']=quality_previous
capture={'started_monotonic':state['started'],'requested_fps':12.,'frames':[],
    'source':'Engine Shot SHOWUI after real scene draw. Actual file completion timestamps; no generated motion.'}
capture['show_ui']=request.get('show_ui',True)
if not capture['show_ui']:
    capture['source']='Engine Shot direct viewport readback without forced Slate SHOWUI redraw; diagnostic excludes UI.'
mouth=u.GameplayStatics.get_all_actors_with_tag(world,'Menu3D_DAY03')[0]
component=mouth.get_component_by_class(u.SkeletalMeshComponent)
state['actual_mesh']=component.get_editor_property('skeletal_mesh_asset').get_path_name()
all_jaw_components={day:u.GameplayStatics.get_all_actors_with_tag(world,f'Menu3D_DAY{day:02d}')[0].get_component_by_class(u.SkeletalMeshComponent) for day in (1,2,3)}
tick_group_previous=[]
animation_mode_previous=[]
if request.get('single_node_evaluation'):
    for day in (1,2,3):
        jaw=u.GameplayStatics.get_all_actors_with_tag(world,f'Menu3D_DAY{day:02d}')[0].get_component_by_class(u.SkeletalMeshComponent)
        assert jaw.get_anim_instance() is None and jaw.get_editor_property('anim_class') is None
        animation_mode_previous.append((jaw,jaw.get_editor_property('animation_mode')))
    state['temporary_single_node_evaluation']=True
if request.get('mouth_tick_group'):
    assert request['mouth_tick_group']=='TG_POST_UPDATE_WORK'
    for day in (1,2,3):
        jaw=u.GameplayStatics.get_all_actors_with_tag(world,f'Menu3D_DAY{day:02d}')[0].get_component_by_class(u.SkeletalMeshComponent)
        previous_group=jaw.get_editor_property('primary_component_tick').get_editor_property('TickGroup')
        tick_group_previous.append((jaw,previous_group))
    state['temporary_mouth_tick_group']=request['mouth_tick_group']
interior_components={}
for day in range(1,4):
    for part in ('Interior','Tongue'):
        actors=u.GameplayStatics.get_all_actors_with_tag(world,f'Menu3D_DAY{day:02d}_{part}')
        assert len(actors)==1
        actor=actors[0]
        interior_components[f'day{day}_{part}']=(actor,actor.get_component_by_class(u.StaticMeshComponent))
previous_visibility=widget.get_visibility()
widget.set_visibility(u.SlateVisibility.HIT_TEST_INVISIBLE)
state['pointer_isolation']='Root widget HitTestInvisible only during controlled render review; restored on exit. Shared BP functions still run. Physical input is not tested here.'
pending=None;last_request=-1.;last_sample=-1.;dilation=1.

def finish(error=None):
    for jaw,previous_mode in animation_mode_previous:jaw.set_animation_mode(previous_mode)
    for jaw,previous_group in tick_group_previous:jaw.set_tick_group(previous_group)
    for cvar,value in render_previous.items():u.SystemLibrary.execute_console_command(world,cvar+' '+str(value))
    for setting,value in quality_previous.items():getattr(settings,'set_'+setting)(value)
    if quality_previous:settings.apply_non_resolution_settings()
    u.GameplayStatics.set_global_time_dilation(world,1.)
    if u.SystemLibrary.is_valid(widget):widget.set_visibility(previous_visibility)
    if getattr(u,'_mc_lower_review_handle',None):u.unregister_slate_post_tick_callback(u._mc_lower_review_handle)
    u._mc_lower_review_handle=None
    state['error']=error;capture['error']=error
    (out/'native_route_actual_states.json').write_text(json.dumps(state,indent=2),encoding='utf8')
    (out/'capture_receipt.json').write_text(json.dumps(capture,indent=2),encoding='utf8')
    u.log('MC_LOWER_REVIEW_FINISHED '+str(error))

def tick(delta):
    global pending,last_request,last_sample,dilation
    try:
        elapsed=time.monotonic()-state['started']
        widget.call_method('ResetPointerIntent')
        if pending:
            path=out/pending['file']
            if path.is_file():
                capture['frames'].append(dict(pending,time=path.stat().st_mtime-state['started_wall']))
                pending=None
            elif elapsed-last_request>5:raise RuntimeError('Screenshot timed out')
        while state['index']<len(schedule) and elapsed>=schedule[state['index']][0]:
            at,action,value=schedule[state['index']];state['index']+=1
            if action=='catalog':widget.get_editor_property('PlayButton').on_clicked.broadcast()
            elif action=='footer':widget.get_editor_property('DaysBackButton').on_clicked.broadcast()
            elif action=='dilation':
                dilation=value;u.GameplayStatics.set_global_time_dilation(world,value)
            elif action=='morph_warmup_end':
                assert 'r.MorphTarget.ForceUpdate' in render_previous
                u.SystemLibrary.execute_console_command(world,'r.MorphTarget.ForceUpdate '+str(render_previous['r.MorphTarget.ForceUpdate']))
            elif value is None:widget.call_method(action)
            else:widget.call_method(action,args=(value,))
            state['actions'].append({'elapsed':elapsed,'action':action,'value':value})
        if elapsed-last_sample>=.05:
            heights=[widget.get_editor_property(f'Day{i}Size').get_editor_property('height_override') for i in range(1,8)]
            first=widget.get_editor_property('WindowFirstDay')
            command_now=any(abs(a['elapsed']-elapsed)<1e-7 for a in state['actions'])
            state['samples'].append({'elapsed':elapsed,'observation_phase':'command_before_next_world_tick' if command_now else 'after_world_tick',
                'page':widget.get_editor_property('PageIndex'),
                'selected':widget.get_editor_property('SelectedDay'),'active':widget.get_editor_property('ActiveDay'),
                'openness':[widget.get_editor_property(f'Open{i}') for i in range(1,8)],
                'heights':heights,'three_row_sum':sum(heights[first-1:first+2]),'window_first':first,
                'day_counter':str(widget.get_editor_property('DayCounter').get_text()),
                'scale':str(mouth.get_actor_scale3d()),'world_z':mouth.get_actor_location().z,
                'pose_weights':{n:component.get_morph_target(n) for n in ('Pose_000','Pose_025','Pose_050','Pose_075')},
                'all_pose_weights':{str(day):{n:jaw.get_morph_target(n) for n in ('Pose_000','Pose_025','Pose_050','Pose_075')} for day,jaw in all_jaw_components.items()},
                'interiors':{name:{'actor_hidden':actor.get_editor_property('hidden'),
                    'component_visible':mesh.is_visible(),
                    'mesh':mesh.get_editor_property('static_mesh').get_path_name(),
                    'relative_scale':[mesh.get_editor_property('relative_scale3d').x,
                        mesh.get_editor_property('relative_scale3d').y,mesh.get_editor_property('relative_scale3d').z]}
                    for name,(actor,mesh) in interior_components.items()},
                'dilation':dilation,'vsm_enabled':u.SystemLibrary.get_console_variable_int_value('r.Shadow.Virtual.Enable'),
                'morph_force_update':u.SystemLibrary.get_console_variable_int_value('r.MorphTarget.ForceUpdate')})
            last_sample=elapsed
        if elapsed>=duration:
            if pending is None:finish()
        elif pending is None and elapsed-last_request>=1/capture['requested_fps']:
            pending={'file':f'frame_{len(capture["frames"]):04d}.png','request_time':elapsed}
            last_request=elapsed
            u.SystemLibrary.execute_console_command(world,'Shot '+('SHOWUI ' if capture['show_ui'] else '')+'filename='+(out/pending['file']).as_posix()+' -nosuffix')
    except Exception as error:
        finish(str(error));raise

try:
    # Apply reversible renderer/quality overrides only after all route/widget
    # preflight and cleanup state exist. No settings are saved to disk.
    for cvar,value in request.get('render_overrides',{}).items():
        u.SystemLibrary.execute_console_command(world,cvar+' '+str(value))
    for jaw,previous_group in tick_group_previous:
        jaw.set_tick_group(getattr(u.TickingGroup,request['mouth_tick_group']))
    for jaw,previous_mode in animation_mode_previous:
        jaw.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE)
        assert jaw.get_anim_instance() is not None
    for setting,value in request.get('quality_overrides',{}).items():
        getattr(settings,'set_'+setting)(int(value))
    if quality_previous:settings.apply_non_resolution_settings()
    state['started']=time.monotonic();state['started_wall']=time.time()
    capture['started_monotonic']=state['started']
    u._mc_lower_review_handle=u.register_slate_post_tick_callback(tick)
    u.log('MC_LOWER_REVIEW_STARTED '+run_name)
except Exception as error:
    finish(str(error))
    raise
