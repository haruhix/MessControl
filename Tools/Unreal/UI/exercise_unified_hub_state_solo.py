"""Bounded real Hub state actions. No launch, session/network or persistent writes."""
import json
from pathlib import Path
import unreal as u
O=Path('E:/CodexWork/MessControl/Validation/UIHubReward_20261104')
a=(O/'HubAction.txt').read_text().strip()
w=u.EditorLevelLibrary.get_pie_worlds(True)[0];assert u.GameplayStatics.get_current_level_name(w,True)=='Hub'
assert '-mchubtransientprofile' in u.SystemLibrary.get_command_line().lower()
p=u.GameplayStatics.get_player_controller(w,0);h=p.get_controlled_pawn();g=u.GameplayStatics.get_game_state(w)
def obj(cls,viewport=False):return next(x for x in u.ObjectIterator(cls) if x.get_world()==w and x.get_owning_player()==p and (not viewport or x.is_in_viewport()))
def find(x,name):
    t=next(t for t in u.ObjectIterator(u.WidgetTree) if t.get_outer()==x)
    return u.find_object(t,name)
def state():
    return {'phase':str(g.get_editor_property('Phase')),'ready':g.get_ready_count(),'loaded':g.get_loaded_count(),'allLoaded':g.are_all_loaded(),'generation':g.get_editor_property('SceneGeneration'),'moveIgnored':p.is_move_input_ignored(),'lookIgnored':p.is_look_input_ignored(),'screen':find(obj(u.MCUnifiedUIWidget,True),'InterfaceScreens').get_active_widget_index(),'scoreboardsVisible':sum(x.is_visible() for x in u.ObjectIterator(u.MCScoreboardWidget) if x.get_world()==w and x.get_owning_player()==p)}
d={'action':a,'before':state()}
if a=='ready':
    assert 'GATHERING' in d['before']['phase']
    z=next(x for x in u.GameplayStatics.get_all_actors_of_class(w,u.MCHubReadyZone) if x.get_editor_property('bEnabled'))
    h.character_movement.stop_movement_immediately()
    h.set_actor_location(z.get_actor_location(),False,True)
    assert z.contains_pawn(h)
elif a=='select':
    assert d['before']['allLoaded'] and d['before']['scoreboardsVisible']==1,d
    p.request_hub_open_selection()
    assert 'SELECTING' in str(g.get_editor_property('Phase'))
elif a=='selected':
    d['state']=state();assert 'SELECTING' in d['state']['phase'] and d['state']['allLoaded'],d
    assert d['state']['screen']==2 and d['state']['moveIgnored'] and d['state']['lookIgnored'],d
    assert d['state']['scoreboardsVisible']==0,d
    assert find(obj(u.MCUnifiedHubWidget),'HubV2SelectionFooter').is_visible()
elif a=='return':
    v=find(obj(u.MCUnifiedHubWidget),'HubV2Return')
    assert v.get_is_enabled() and v.is_visible()
    find(v,'UniversalSurface').on_clicked.broadcast()
    assert 'GATHERING' in str(g.get_editor_property('Phase'))
elif a=='returned':
    d['state']=state();assert 'GATHERING' in d['state']['phase'] and d['state']['screen']==2,d
    assert not d['state']['moveIgnored'] and not d['state']['lookIgnored'],d
else:raise AssertionError(a)
d['after']=state();d['passed']=True
(O/('Hub_'+a+'.json')).write_text(json.dumps(d,indent=2),encoding='utf-8')
print(json.dumps(d))
