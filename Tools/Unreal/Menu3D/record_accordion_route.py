"""Drive shared menu BP functions on real UE ticks for a native motion review.

No fake renders or frame compositing; capture the live PIE window separately.
Input injection is validated separately. Never blocks the editor with sleeps.
"""
import json
from pathlib import Path
import time
import unreal as u

world=u.EditorLevelLibrary.get_game_world()
assert world
cls=u.load_class(None,'/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C')
lib=u.get_default_object(u.load_class(None,'/Script/UMG.WidgetBlueprintLibrary'))
widgets=lib.call_method('GetAllWidgetsOfClass',args=(world,cls,False))
assert len(widgets)==1
widget=widgets[0]
out=Path(u.Paths.project_dir())/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v02_ThreeMouths'
assert not getattr(u,'_mc_accordion_review_handle',None),'Existing review callback must finish first'

schedule=[(0.,'ShowPage',0),(2.,'catalog',None),(4.5,'SelectDay',2),
    (7.5,'SelectDay',3),(10.5,'StepDay',1),(13.,'StepDay',1),
    (15.,'StepDay',1),(17.,'StepDay',1),(20.,'StepDay',-1),
    (23.,'SelectDay',3),(26.,'Back',None),(28.5,'Back',None)]
state={'started':time.monotonic(),'index':0,'actions':[],'samples':[],'last_sample':-1.}

def sample(elapsed):
    first=widget.get_editor_property('WindowFirstDay')
    heights=[widget.get_editor_property('Day'+str(i)+'Size').get_editor_property('height_override') for i in range(1,8)]
    state['samples'].append({'elapsed':elapsed,'page':widget.get_editor_property('PageIndex'),
        'selected':widget.get_editor_property('SelectedDay'),'active':widget.get_editor_property('ActiveDay'),
        'window_first':first,'scroll_offset':widget.get_editor_property('DayScroll').get_scroll_offset(),
        'heights':heights,'three_row_sum':sum(heights[max(0,first-1):max(0,first-1)+3]),
        'openness':[widget.get_editor_property('Open'+str(i)) for i in range(1,8)]})

def finish(error=None):
    u.unregister_slate_post_tick_callback(u._mc_accordion_review_handle)
    u._mc_accordion_review_handle=None
    state['purpose']='Actual native PIE; shared BP functions drive review. Physical wheel checked separately.'
    state['error']=error
    (out/'native_route_actual_states.json').write_text(json.dumps(state,indent=2),encoding='utf8')
    u.log('MC_ACCORDION_NATIVE_REVIEW_FINISHED '+str(error))

def tick(delta):
    try:
        elapsed=time.monotonic()-state['started']
        while state['index']<len(schedule) and elapsed>=schedule[state['index']][0]:
            at,action,value=schedule[state['index']]
            if action=='catalog':
                widget.call_method('ShowPage',args=(1,))
                widget.set_editor_property('ActiveDay',0)
                widget.set_editor_property('SelectedDay',1)
                widget.set_editor_property('WindowFirstDay',1)
                for i in range(1,8):widget.set_editor_property('Open'+str(i),0.)
                widget.call_method('CenterSelectedDay')
            elif value is None:widget.call_method(action)
            else:widget.call_method(action,args=(value,))
            # Keep the pointer from stealing an automated review selection.
            widget.set_editor_property('HoverQuiet',2.)
            state['actions'].append({'elapsed':elapsed,'action':action,'value':value})
            state['index']+=1
        if elapsed-state['last_sample']>=.25:
            sample(elapsed);state['last_sample']=elapsed
        if elapsed>=31.:finish()
    except Exception as error:
        finish(str(error));raise

u._mc_accordion_review_handle=u.register_slate_post_tick_callback(tick)
u.log('MC_ACCORDION_NATIVE_REVIEW_STARTED')
