"""Native task-owned menu graphs. No level load/save/spawn or PIE commands."""
from pathlib import Path
import argparse
import json
from menu_native_client import call, props, ref, task_asset, BASE, BP, UMG, OBJ, ASSET

OUT=Path(__file__).resolve().parent/'Evidence'
w=task_asset('WBP_MainMenu3D');pc=task_asset('BP_Menu3DPlayerController');gm=task_asset('BP_Menu3DGameMode')
graphs=json.loads((OUT/'logic_functions.json').read_text())
event_graph=call(BP,'get_graph',blueprint=w,graph_name='EventGraph')
existing=set(call(BP,'list_variables',blueprint=w))
for name in ('LastHoverDay','LastFocusDay'):
    if name not in existing:call(BP,'add_variable',blueprint=w,name=name,type_name='int')
for name in ('InteriorActors','TongueActors','CharacterActors'):
    if name not in existing:call(BP,'add_object_variable',blueprint=w,name=name,object_class=ref('/Script/Engine.Actor'),container_type='ARRAY')
parser=argparse.ArgumentParser();parser.add_argument('--start',default='FindStageActors');args=parser.parse_args()
used=['CallFunction|ShowPage','CallFunction|SelectDay','CallFunction|OpenConnection','CallFunction|Back','CallFunction|UpdateStage','CallFunction|FindStageActors',
      'Widget|Panel|ClearChildren','Utilities|Text|EqualExactly(Text)','Game|GetPlayerController','Math|Vector|MakeVector']
api={t:call(BP,'get_node_type_pins',graph=event_graph,type_id=t) for t in used}
(OUT/'used_custom_api_pins.json').write_text(json.dumps(api,indent=2),encoding='utf8')

widget_vars={x['widgetName'] for x in call(UMG,'GetWidgets',widgetBlueprint=w)['widgets'] if x['bIsVariable']}
def g(name):return f'(Variables|{"WBP_MainMenu3D" if name in widget_vars else "Default"}|Get{"Borderless" if name=="bBorderless" else name})'
def s(name,value):return f'(Variables|Default|Set{"Borderless" if name=="bBorderless" else name} {value})'
def focus(name):return f'(Widget|SetKeyboardFocus :self {g(name)})'
def text(name,value):return f'(Widget|SetText(Text) :self {g(name)} :InText {json.dumps(value,ensure_ascii=False)})'
def write(name,code,graph=None):
    global started
    if name==args.start:started=True
    if not started:return
    path=OUT/(name+'.dsl');path.write_text(code,encoding='utf8')
    result=call(BP,'write_graph_dsl',graph=graph or graphs[name],code=code)
    print('GRAPH_WRITTEN',name,json.dumps(result),flush=True)

started=False

find=['(fn FindStageActors ()']
for n in range(1,4):
    find += [f'  (bind day{n} (Actor|GetAllActorswithTag :Tag "Menu3D_DAY0{n}"))',f'  {s(f"Day{n}Actors",f"day{n}")}']
for var,tag in [('InteriorActors','Interior'),('TongueActors','Tongue'),('CharacterActors','Character')]:
    find += [f'  (bind {var} (Actor|GetAllActorswithTag :Tag "Menu3D_DAY03_{tag}"))',f'  {s(var,var)}']
find+=[')'];write('FindStageActors','\n'.join(find))

page=f'''(fn ShowPage (Index)
  {s('PageIndex','Index')}
  (Switcher|SetActiveWidgetIndex :self {g('Pages')} :Index Index)
  (switch int Index
    (:0 {focus('PlayButton')})
    (:1 {focus('Day3Button')}
        (Widget|ScrollWidgetIntoView :self {g('DayScroll')} :WidgetToFind {g('Day3Size')} :AnimateScroll false :ScrollDestination "Center" :Padding 0.0))
    (:2 {focus('QualityHighButton')})
    (:3 {focus('CreditsBackButton')})
    (:4 {focus('ExitCancelButton')})
    (:5 {focus('LaunchBackButton')}))
)'''
write('ShowPage',page)
select=['(fn SelectDay (Day)',f'  {s("ActiveDay","Day")}',f'  {s("HoverQuiet","0.2")}', '  (switch int Day',f'    (:0 {text("DayCounter","— / 07")})']
for i in range(1,8):select.append(f'    (:{i} {text("DayCounter",f"{i:02} / 07")})')
select+=['  )',')'];write('SelectDay','\n'.join(select))

back=f'''(fn Back ()
  (bind page {g('PageIndex')})
  (if (== page 0)
    (CallFunction|ShowPage :Index 4)
    (elif (== page 1)
      (if (> {g('ActiveDay')} 0)
        {s('ActiveDay','0')}
        {s('HoverQuiet','0.5')}
        {focus('DaysBackButton')}
        (else (CallFunction|ShowPage :Index 0)))
      (elif (== page 5)
        (Widget|Panel|ClearChildren :self {g('LegacyMenuHost')})
        (CallFunction|ShowPage :Index 1)
        (else (CallFunction|ShowPage :Index 0)))))
)'''
write('Back',back)
connection=f'''(fn OpenConnection ()
  (Widget|Panel|ClearChildren :self {g('LegacyMenuHost')})
  (CallFunction|ShowPage :Index 5)
  (bind owner (Widget|GetOwningPlayer))
  (bind menu (UserInterface|CreateWidget :Class "/Game/Gameplay/UI/WBP_MainMenu.WBP_MainMenu_C" :OwningPlayer owner))
  {s('LegacyMenu','menu')}
  (Widget|AddChildtoOverlay :self {g('LegacyMenuHost')} :Content menu)
)'''
write('OpenConnection',connection)

# Exact registered pose interpolation: Open basis has zero corrective weights.
stage=['(fn UpdateStage ()',f'  (bind page {g("PageIndex")})','  (bind isDays (== page 1))',
       '  (bind dt (Utilities|Time|GetWorldDeltaSeconds))',f'  (bind scroll (Widget|GetScrollOffset :self {g("DayScroll")}))',
       f'  (if (> (Math|Float|Absolute(Float) (- scroll {g("LastScroll")})) 0.1) {s("HoverQuiet","0.18")})',
       f'  {s("LastScroll","scroll")}',f'  {s("HoverQuiet",f"(Math|Float|Clamp(Float) (- {g('HoverQuiet')} dt) 0.0 1.0)")}',
       '  (bind owner (Widget|GetOwningPlayer))']
hover='0';keyboard='0'
for i in range(3,0,-1):
    hover=f'(select (Widget|IsHovered :self {g(f"Day{i}Button")}) {i} {hover})'
    keyboard=f'(select (Widget|HasUserFocus :self {g(f"Day{i}Button")} :PlayerController owner) {i} {keyboard})'
stage += [f'  (bind hovered {hover})',f'  (bind focused {keyboard})',
          f'  (if (and isDays (and (> hovered 0) (and (!= hovered {g("LastHoverDay")}) (<= {g("HoverQuiet")} 0.0)))) (CallFunction|SelectDay :Day hovered))',
          f'  (if (and isDays (and (> focused 0) (!= focused {g("LastFocusDay")}))) (CallFunction|SelectDay :Day focused))',
          f'  {s("LastHoverDay","hovered")}',f'  {s("LastFocusDay","focused")}',f'  (bind active {g("ActiveDay")})']
for i in range(1,4):
    target=f'(select isDays (select (== active {i}) 1.0 0.0) {1.0 if i==3 else 0.0})'
    stage += [f'  (bind open{i} (Math|Interpolation|FInterpTo :Current {g(f"Open{i}")} :Target {target} :DeltaTime dt :InterpSpeed 9.0))',
              f'  {s(f"Open{i}",f"open{i}")}',f'  (bind height{i} (+ 427.0 (* open{i} 254.0)))',
              f'  (Layout|SizeBox|SetHeightOverride :self {g(f"Day{i}Size")} :InHeightOverride height{i})',
              f'  (if (and isDays (> open{i} 0.72)) (Widget|SetVisibility :self {g(f"Day{i}CavityControls")} :InVisibility "SelfHitTestInvisible") (else (Widget|SetVisibility :self {g(f"Day{i}CavityControls")} :InVisibility "Collapsed")))']
prior='0.0'
for i in range(1,4):
    stage += [f'  (bind center{i} (+ 80.0 (- (+ {prior} (* height{i} 0.5)) scroll)))',
              f'  (bind hidden{i} (select isDays (or (< (+ center{i} (* height{i} 0.5)) 80.0) (> (- center{i} (* height{i} 0.5)) 820.0)) {"false" if i==3 else "true"}))',
              f'  (for actor {g(f"Day{i}Actors")}',
              f'    (Rendering|SetActorHiddenInGame :self actor :bNewHidden hidden{i})',
              '    (bind loc (Transformation|GetActorLocation :self actor))',
              f'    (Transformation|SetActorLocation :self actor :NewLocation (Math|Vector|MakeVector :X (.x loc) :Y (.y loc) :Z (select isDays (- 450.0 center{i}) 0.0)) :bSweep false :bTeleport true)']
    if i==3:
        stage += [f'    (bind scale (- 1.12676056338 (* open{i} 0.12676056338)))',
                  '    (Transformation|SetActorScale3D :self actor :NewScale3D (Math|Vector|MakeVector scale scale scale))']
    stage += ['    (bind sk (Utilities|Casting|CastToSkeletalMeshActor :Object actor)',
              '      (:then (bind mesh (Class|SkeletalMeshActor|GetSkeletalMeshComponent :self sk))']
    for suffix,k in [('000',0.0),('025',.25),('050',.5),('075',.75)]:
        weight=f'(Math|Float|Clamp(Float) (- 1.0 (Math|Float|Absolute(Float) (* (- open{i} {k}) 4.0))) 0.0 1.0)'
        stage.append(f'        (Components|SkeletalMesh|SetMorphTarget :self mesh :MorphTargetName "Pose_{suffix}" :Value {weight} :bRemoveZeroWeight true)')
    stage += [f'        (Rendering|Material|SetScalarParameterValueonMaterials :self mesh :ParameterName "OpenAmount" :ParameterValue open{i}))',
              '      (:CastFailed))', '  )']
    prior=f'(+ {prior} height{i})'
for var,threshold in [('InteriorActors',.05),('TongueActors',.05),('CharacterActors',.75)]:
    stage.append(f'  (for child {g(var)} (Rendering|SetActorHiddenInGame :self child :bNewHidden (or hidden3 (< open3 {threshold}))))')
stage+=[')'];write('UpdateStage','\n'.join(stage))

key='''(fn OnPreviewKeyDown (MyGeometry InKeyEvent)
  (bind key (Input|KeyEvent|GetKey :Input InKeyEvent))
  (bind display (Input|Key|GetKeyDisplayName :Key key :bLongDisplayName true))
  (bind name (Utilities|String|ToString(Text) :InText display))
  (if (or (Utilities|String|EqualExactly(String) name "Escape") (Utilities|String|EqualExactly(String) name "Gamepad Face Button Right"))
    (CallFunction|Back)
    (return (Widget|EventReply|Handled))
    (else (return (Widget|EventReply|Unhandled))))
)'''
write('OnPreviewKeyDown',key)

events=[f'''(event UserInterface|EventConstruct
  {s('PageIndex','0')}
  {s('ActiveDay','0')}
  {s('Open3','1.0')}
  (CallFunction|FindStageActors)
  (CallFunction|ShowPage :Index 0))''',
        '(event UserInterface|EventTick (MyGeometry InDeltaTime) (CallFunction|UpdateStage))']
click={
    'PlayButton':f'{s("ActiveDay","3")} (CallFunction|ShowPage :Index 1)',
    'SettingsButton':'(CallFunction|ShowPage :Index 2)', 'CreditsButton':'(CallFunction|ShowPage :Index 3)',
    'ExitButton':'(CallFunction|ShowPage :Index 4)', 'ExitCancelButton':'(CallFunction|ShowPage :Index 0)',
    'SettingsBackButton':'(CallFunction|ShowPage :Index 0)', 'CreditsBackButton':'(CallFunction|ShowPage :Index 0)',
    'DaysBackButton':'(CallFunction|ShowPage :Index 0)', 'LaunchBackButton':'(CallFunction|Back)',
    'ExitConfirmButton':'(Game|QuitGame :SpecificPlayer (Widget|GetOwningPlayer) :QuitPreference "Quit" :bIgnorePlatformRestrictions false)'}
for i in range(1,8):click[f'Day{i}Button']=f'(CallFunction|SelectDay :Day {i})'
for i in range(1,4):
    click[f'Day{i}PlayButton']='(CallFunction|OpenConnection)'
    click[f'Day{i}CollapseButton']=f'{s("ActiveDay","0")} {s("HoverQuiet","0.5")} {focus("DaysBackButton")}'
for label,level in [('Low',0),('High',2),('Epic',3)]:
    click[f'Quality{label}Button']=f'''(bind settings (Settings|GetGameUserSettings))
  (Settings|SetOverallScalabilityLevel :self settings :Value {level})
  (Settings|ApplySettings :self settings :bCheckForCommandLineOverrides false)
  (Settings|SaveSettings :self settings)
  {text('SettingsStatus',f'Качество {label} применено и сохранено.')}'''
click['FullscreenButton']=f'''(bind settings (Settings|GetGameUserSettings))
  (if {g('bBorderless')}
    (Settings|SetFullscreenMode :self settings :InFullscreenMode "Windowed")
    {s('bBorderless','false')}
    (else (Settings|SetFullscreenMode :self settings :InFullscreenMode "WindowedFullscreen") {s('bBorderless','true')}))
  (Settings|ApplySettings :self settings :bCheckForCommandLineOverrides false)
  (Settings|SaveSettings :self settings)'''
for name,body in click.items():events.append(f'(event OnClicked({name})\n  {body}\n)')
write('EventGraph','\n\n'.join(events),event_graph)

pc_graph=call(BP,'get_graph',blueprint=pc,graph_name='EventGraph')
pc_code=f'''(event EventBeginPlay
  (bind menu (UserInterface|CreateWidget :Class "{BASE}/WBP_MainMenu3D.WBP_MainMenu3D_C" :OwningPlayer self))
  (UserInterface|Viewport|AddtoViewport :self menu :ZOrder 50)
  (Input|SetInputModeUIOnly :PlayerController self :InWidgetToFocus menu :InMouseLockMode "DoNotLock" :bFlushInput true)
  (Variables|MouseInterface|SetShowMouseCursor :bShowMouseCursor true)
  (bind cameras (Actor|GetAllActorswithTag :Tag "Menu3D_Camera"))
  (for camera cameras (Game|Player|SetViewTargetwithBlend :self self :NewViewTarget camera :BlendTime 0.0))
)'''
write('PC_EventGraph',pc_code,pc_graph)

# Only task BP CDOs are edited. Existing runtime/module sources remain untouched.
for blueprint,values in [(gm,{'playerControllerClass':ref(BASE+'/BP_Menu3DPlayerController.BP_Menu3DPlayerController_C'),'defaultPawnClass':'None','HUDClass':'None'}),
                         (pc,{'bAutoManageActiveCameraTarget':False})]:
    schema=json.loads(call(OBJ,'list_properties',instance=blueprint))
    keys={key.lower():key for key in schema}
    props(blueprint,{keys[name.lower()]:value for name,value in values.items()})
for blueprint in (w,pc,gm):call(BP,'compile_blueprint',blueprint=blueprint,warnings_as_errors=True)
paths=[BASE+'/WBP_MainMenu3D',BASE+'/BP_Menu3DPlayerController',BASE+'/BP_Menu3DGameMode']
call(ASSET,'save_assets',asset_paths=paths)
report={'status':'TASK_NATIVE_MENU_BLUEPRINTS_COMPILED_SAVED_PENDING_ROOT_PIE','assets':paths,
        'level_or_PIE_mutation':False,'native_cpp_modified':False,'full_graphs_folder':str(OUT),
        'actor_contract':'Menu3D_DAY01/02/03 +DAY03_Interior/Tongue/Character attached; Menu3D_Camera',
        'frame':[1600,900],'body_y':[80,820],'closed_open_row_heights':[427,681],
        'morph_targets':['Pose_000','Pose_025','Pose_050','Pose_075'],'material_parameter':'OpenAmount',
        'gameplay_limit':'Existing connection/lobby travel is reused. This UI adds no selected-day game-rule backend.'}
(OUT/'native_menu_compile_report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report,indent=2),flush=True)
