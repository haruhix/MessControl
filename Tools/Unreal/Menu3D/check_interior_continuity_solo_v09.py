"""Short native transition check without screenshot-induced frame stalls."""
import json
from pathlib import Path
import time
import unreal as u

world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world()
assert world
cls=u.load_class(None,'/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C')
lib=u.get_default_object(u.load_class(None,'/Script/UMG.WidgetBlueprintLibrary'))
widgets=lib.call_method('GetAllWidgetsOfClass',args=(world,cls,False))
assert len(widgets)==1 and not getattr(u,'_mc_interior_solo_check',None)
widget=widgets[0]
out=Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/v09_interior_own_jaw_mask_continuity_solo.json'
assert not out.exists()
previous=widget.get_visibility()
widget.set_visibility(u.SlateVisibility.HIT_TEST_INVISIBLE)
components={}
for day in (1,2,3):
    for part in ('Interior','Tongue'):
        actors=u.GameplayStatics.get_all_actors_with_tag(world,f'Menu3D_DAY{day:02d}_{part}')
        assert len(actors)==1
        components[(day,part)]=(actors[0],actors[0].get_component_by_class(u.StaticMeshComponent))
schedule=[(0.,'catalog',None),(.6,'SelectDay',3),(1.8,'SelectDay',2),
          (3.,'SelectDay',1),(4.2,'Back',None),(5.4,'SelectDay',3)]
state={'started':time.monotonic(),'index':0,'samples':[],'actions':[],
       'scope':'Solo native PIE, shared hover-selection functions, normal time, no screenshot stalls; physical mouse/wheel not exercised'}

def finish(error=None):
    if u.SystemLibrary.is_valid(widget):widget.set_visibility(previous)
    u.unregister_slate_post_tick_callback(u._mc_interior_solo_check)
    u._mc_interior_solo_check=None
    state['error']=error
    samples=[s for s in state['samples'] if not s['command_before_next_world_tick']]
    failures=[]
    for sample in samples:
        for entry in sample['interiors']:
            if entry['actor_hidden'] or not entry['visible']:
                failures.append({'time':sample['elapsed'],'day':entry['day'],'part':entry['part'],'reason':'unexpected hidden'})
            if abs(entry['z']-(.5+.5*sample['open'][entry['day']-1]))>1e-5:
                failures.append({'time':sample['elapsed'],'day':entry['day'],'part':entry['part'],'reason':'own-jaw envelope mismatch'})
    partials={str(day):sum(.01<s['open'][day-1]<.99 for s in samples) for day in (1,2,3)}
    state['validation']={'failures':failures,'partial_samples_per_day':partials,
                         'max_sample_interval':max((b['elapsed']-a['elapsed'] for a,b in zip(samples,samples[1:])),default=0.),
                         'status':'PASS' if error is None and not failures and all(partials.values()) else 'HOLD'}
    out.write_text(json.dumps(state,indent=2),encoding='utf8')
    u.log('MC_INTERIOR_SOLO_CHECK '+json.dumps(state['validation']))

def tick(delta):
    try:
        elapsed=time.monotonic()-state['started']
        widget.call_method('ResetPointerIntent')
        command=False
        while state['index']<len(schedule) and elapsed>=schedule[state['index']][0]:
            at,action,value=schedule[state['index']]
            state['index']+=1
            if action=='catalog':widget.get_editor_property('PlayButton').on_clicked.broadcast()
            elif value is None:widget.call_method(action)
            else:widget.call_method(action,args=(value,))
            command=True
            state['actions'].append({'time':elapsed,'action':action,'value':value})
        state['samples'].append({'elapsed':elapsed,'command_before_next_world_tick':command,
            'open':[widget.get_editor_property(f'Open{i}') for i in (1,2,3)],
            'height':[widget.get_editor_property(f'Day{i}Size').get_editor_property('height_override') for i in (1,2,3)],
            'interiors':[{'day':day,'part':part,'actor_hidden':actor.get_editor_property('hidden'),
                'visible':mesh.is_visible(),'z':mesh.get_editor_property('relative_scale3d').z}
                for (day,part),(actor,mesh) in components.items()]})
        if elapsed>=6.7:finish()
    except Exception as error:
        finish(str(error));raise

u._mc_interior_solo_check=u.register_slate_post_tick_callback(tick)
