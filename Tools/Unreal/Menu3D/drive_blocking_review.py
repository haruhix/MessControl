"""Shared actual Blueprint routing for recorded solo menu review, no game travel."""
import json
from pathlib import Path
import unreal as u

world=u.EditorLevelLibrary.get_game_world()
assert world,'Start solo PIE before this bounded review'
cls=u.load_class(None,'/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C')
lib=u.get_default_object(u.load_class(None,'/Script/UMG.WidgetBlueprintLibrary'))
widgets=lib.call_method('GetAllWidgetsOfClass',args=(world,cls,False))
assert len(widgets)==1
widget=widgets[0]
command=Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/review_command.json'
args=json.loads(command.read_text(encoding='utf8'))
action=args['action']
if action=='page':widget.call_method('ShowPage',args=(int(args['index']),))
elif action=='catalog':widget.get_editor_property('PlayButton').on_clicked.broadcast()
elif action=='select':widget.call_method('SelectDay',args=(int(args['day']),))
elif action=='step':widget.call_method('StepDay',args=(int(args['direction']),))
elif action=='back':widget.call_method('Back')
elif action=='connection':widget.call_method('OpenConnection')
elif action=='focus':
    target=widget.get_editor_property(args['widget']) if args.get('widget') else widget
    target.call_method('SetKeyboardFocus')
elif action=='pose':
    # A bounded freeze-frame pose check, separate from menu interaction proof.
    widget.set_editor_property('Open3',float(args['amount']))
elif action=='view':
    camera=u.GameplayStatics.get_all_actors_with_tag(world,'Menu3D_Camera')[0]
    location=u.Vector(float(args.get('x',0)),float(args.get('y',2200)),float(args.get('z',0)))
    camera.set_actor_location(location,False,False)
    camera.set_actor_rotation(u.MathLibrary.find_look_at_rotation(location,u.Vector(0,0,0)),False)
elif action!='inspect':raise RuntimeError(action)
report={'action':action,'page':widget.get_editor_property('PageIndex'),
        'active_day':widget.get_editor_property('ActiveDay'),'selected_day':widget.get_editor_property('SelectedDay'),
        'openness':[widget.get_editor_property('Open'+str(i)) for i in range(1,8)],
        'window_first_day':widget.get_editor_property('WindowFirstDay'),
        'day_counter':str(widget.get_editor_property('DayCounter').get_text()),
        'wheel_accumulator':widget.get_editor_property('WheelAccumulator'),
        'scroll_offset':widget.get_editor_property('DayScroll').get_scroll_offset(),
        'actual_row_heights':[widget.get_editor_property('Day'+str(i)+'Size').get_editor_property('height_override') for i in range(1,8)],
        'mouths':[]}
for day in range(1,8):
    for actor in u.GameplayStatics.get_all_actors_with_tag(world,'Menu3D_DAY0'+str(day)):
        mesh=actor.get_component_by_class(u.SkeletalMeshComponent)
        report['mouths'].append({'day':day,'location':str(actor.get_actor_location()),'scale':str(actor.get_actor_scale3d()),
          'pose_weights':{name:mesh.get_morph_target(name) for name in ('Pose_000','Pose_025','Pose_050','Pose_075')}})
path=command.parent/('actual_review_'+args.get('label',action)+'.json')
path.write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_BLOCKING_ACTUAL_ACTION '+json.dumps(report))
