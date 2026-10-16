"""Native three-mouth accordion. Author ONLY owned WBP after PIE is stopped.

The visible window is3rows,740high. A selected row opens370high and its two
neighbors185high; closed state is3x246.667. Wheel remains one day per event.
"""
from pathlib import Path
import argparse
import json
import urllib.request
from menu_native_client import call, props, ref, task_asset, BASE, BP, UMG, OBJ, ASSET, CLIENT

OUT=Path(__file__).resolve().parent/'Evidence'
parser=argparse.ArgumentParser();parser.add_argument('--start',default='FindStageActors');parser.add_argument('--only',default='');args=parser.parse_args()
w=task_asset('WBP_MainMenu3D');started=False
tree=call(UMG,'GetWidgets',widgetBlueprint=w);items={x['widgetName']:x for x in tree['widgets']}
widget_vars={x['widgetName'] for x in tree['widgets'] if x['bIsVariable']}
for i in range(4,8):
    name=f'Day{i}ComingSoon'
    if name not in widget_vars:
        call(UMG,'ToggleWidgetAsVariable',widgetBlueprint=w,widget=items[name]['widget'],bIsVariable=True)
        widget_vars.add(name)
def g(n):return f'(Variables|{"WBP_MainMenu3D" if n in widget_vars else "Default"}|Get{n})'
def s(n,v):return f'(Variables|Default|Set{n} {v})'
graphs=json.loads((OUT/'logic_functions.json').read_text())
for n in ('CenterSelectedDay','OnMouseWheel','StepDay'):
    graphs[n]=call(BP,'get_graph',blueprint=w,graph_name=n)
existing=set(call(BP,'list_variables',blueprint=w))
for n,t in [('WindowFirstDay','int'),('PendingHoverDay','int'),('LastPointerX','float'),('LastPointerY','float'),('PointerReady','bool'),('HasPointerMotion','bool'),('QueuedPointerScreen','Vector2D'),('StageScroll','float'),('StageScrollTarget','float')]+[(f'Open{i}','float') for i in range(4,8)]:
    if n not in existing:call(BP,'add_variable',blueprint=w,name=n,type_name=t)
for n in ('ResetPointerIntent','UpdatePointerIntent','OnMouseMove'):
    graphs[n]=call(BP,'add_function_graph',blueprint=w,graph_name=n)
for n in ('Character1Actors','Character2Actors','Interior1Actors','Interior2Actors','Tongue1Actors','Tongue2Actors')+tuple(f'Day{i}Actors' for i in range(4,8)):
    if n not in existing:call(BP,'add_object_variable',blueprint=w,name=n,object_class=ref('/Script/Engine.Actor'),container_type='ARRAY')
try:graphs['DriveMouthDay']=call(BP,'get_graph',blueprint=w,graph_name='DriveMouthDay')
except RuntimeError:graphs['DriveMouthDay']=None
if not graphs['DriveMouthDay']:
    graphs['DriveMouthDay']=call(BP,'add_function_graph',blueprint=w,graph_name='DriveMouthDay')
    for name,kind in [('Day','int'),('Open','float'),('Height','float'),('Center','float'),('Hidden','bool'),('Scale','float')]:
        call(BP,'add_function_param',graph=graphs['DriveMouthDay'],param_name=name,param_type=kind,input_param=True)

def long_graph_write(graph,code):
    # The task graph needs~70–120s for native author/layout. The shared CLI
    # helper times out60s; this one request has180s without global changes.
    CLIENT.sequence+=1
    body={'jsonrpc':'2.0','id':CLIENT.sequence,'method':'tools/call','params':{
        'name':'call_tool','arguments':{'toolset_name':BP,'tool_name':'write_graph_dsl','arguments':{'graph':graph,'code':code}}}}
    headers={'Content-Type':'application/json','Accept':'application/json, text/event-stream',
             'Mcp-Session-Id':CLIENT.session,'MCP-Protocol-Version':CLIENT.version}
    request=urllib.request.Request('http://127.0.0.1:8000/mcp',json.dumps(body).encode(),headers)
    with urllib.request.urlopen(request,timeout=180) as response:
        if 'text/event-stream' in response.headers.get('Content-Type',''):
            for line in response:
                if line.startswith(b'data: '):
                    result=json.loads(line[6:])
                    if result.get('id')==CLIENT.sequence:break
        else:result=json.load(response)
    if 'error' in result:raise RuntimeError(result['error'])
    value=result['result']
    if value.get('isError'):raise RuntimeError(value)
    return value

def write(n,code,graph=None):
    global started
    if args.only:
        if n not in args.only.split(','):return
        started=True
    if n==args.start:started=True
    if not started:return
    (OUT/('accordion_'+n+'.dsl')).write_text(code,encoding='utf8')
    long_graph_write(graph or graphs[n],code)
    print('GRAPH_WRITTEN',n,flush=True)

props(items['Day1Size']['slot'],{'padding':{'top':0.}})
props(items['Day7Size']['slot'],{'padding':{'bottom':0.}})
for i in range(1,8):
    props(items[f'Day{i}Size']['widget'],{'heightOverride':246.666667})
    props(items[f'Day{i}Canvas']['widget'],{'clipping':'ClipToBoundsAlways'})
    # Cream labels sit at the edge of the central3D column.
    props(items[f'Day{i}Tag']['slot'],{'layoutData':{'offsets':{'left':300.,'top':16.,'right':310.,'bottom':44.}}})
    props(items[f'Day{i}Label']['slot'],{'layoutData':{'offsets':{'left':313.,'top':24.,'right':285.,'bottom':30.}}})
    props(items[f'Day{i}Label']['widget'],{'font':{'size':17}})
    if i>3:
        # Generic real3D blocking appears behind this ComingSoon label. It is
        # explicitly unassigned content; no inner Play is added for these days.
        props(items[f'Day{i}FutureBackdrop']['widget'],{'visibility':'Collapsed'})
        props(items[f'Day{i}FutureBackdrop']['slot'],{'layoutData':{'anchors':{'minimum':{'x':0.,'y':0.},'maximum':{'x':1.,'y':1.}},'offsets':{'left':0.,'top':0.,'right':0.,'bottom':0.}}})
        props(items[f'Day{i}ComingSoon']['slot'],{'layoutData':{'offsets':{'left':500.,'top':180.,'right':600.,'bottom':48.}}})
        continue
    positions={1:[(154,28),(184,18),(207,40),(254,26)],
               2:[(111,30),(145,18),(178,45),(230,25)],
               3:[(93,36),(131,27),(167,52),(228,37)]}[i]
    for n,rect in [(f'Day{i}CavityControls',(0,0,1600,370)),
                   (f'Day{i}CavityHeading',(600,positions[0][0],400,positions[0][1])),
                   (f'Day{i}CavitySpecies',(600,positions[1][0],400,positions[1][1])),
                   (f'Day{i}PlayButton',(675,positions[2][0],250,positions[2][1])),
                   (f'Day{i}CollapseButton',(728,positions[3][0],144,positions[3][1]))]:
        x,y,width,height=rect
        props(items[n]['slot'],{'layoutData':{'offsets':{'left':x,'top':y,'right':width,'bottom':height}}})
    props(items[f'Day{i}CavityHeading']['widget'],{'font':{'size':{1:24,2:26,3:30}[i]}})
    props(items[f'Day{i}CavitySpecies']['widget'],{'font':{'size':12 if i<3 else 16}})
    if i<3:props(items[f'Day{i}CollapseButton_Text']['widget'],{'font':{'size':16}})
props(items['DaysScrollHint']['widget'],{'text':'WHEEL  ·  ONE DAY PER STEP     ↑ / ↓  SELECT DAY     ESC  BACK'})

find=['(fn FindStageActors ()']
for i in range(1,8):
    find+=[f'  (bind day{i} (Actor|GetAllActorswithTag :Tag "Menu3D_DAY0{i}"))',f'  {s(f"Day{i}Actors",f"day{i}")}']
for var,tag in [('InteriorActors','Menu3D_DAY03_Interior'),('TongueActors','Menu3D_DAY03_Tongue'),
                ('CharacterActors','Menu3D_DAY03_Character'),('Character1Actors','Menu3D_DAY01_Character'),('Character2Actors','Menu3D_DAY02_Character'),
                ('Interior1Actors','Menu3D_DAY01_Interior'),('Interior2Actors','Menu3D_DAY02_Interior'),
                ('Tongue1Actors','Menu3D_DAY01_Tongue'),('Tongue2Actors','Menu3D_DAY02_Tongue')]:
    find+=[f'  (bind {var} (Actor|GetAllActorswithTag :Tag "{tag}"))',f'  {s(var,var)}']
find+=[')'];write('FindStageActors','\n'.join(find))

center=['(fn CenterSelectedDay ()',
        f'  {s("StageScrollTarget",f"(* (- {g('WindowFirstDay')} 1) 246.666667)")}',')']
write('CenterSelectedDay','\n'.join(center))

# A hover event can be caused by a moving card beneath a stationary pointer.
# Only actual viewport-pointer displacement creates a pending hover intent.
reset=f'''(fn ResetPointerIntent ()
  (bind pointer (Viewport|GetMousePositionOnViewport))
  (bind (mx my) (Math|Vector2D|BreakVector2D :InVec pointer))
  {s('LastPointerX','mx')}
  {s('LastPointerY','my')}
  {s('PointerReady','true')}
  {s('HasPointerMotion','false')}
  {s('PendingHoverDay','0')}
)'''
write('ResetPointerIntent',reset)
move=f'''(fn OnMouseMove (MyGeometry MouseEvent)
  (bind delta (Input|PointerEvent|GetCursorDelta :Input MouseEvent))
  (bind (dx dy) (Math|Vector2D|BreakVector2D :InVec delta))
  (if (> (+ (* dx dx) (* dy dy)) 0.0001)
    {s('QueuedPointerScreen','(Input|PointerEvent|GetScreenSpacePosition :Input MouseEvent)')}
    {s('HasPointerMotion','true')})
  (return (Widget|EventReply|Unhandled))
)'''
write('OnMouseMove',move)
hover='0'
for i in range(7,0,-1):hover=f'(select (UserInterface|Geometry|IsUnderLocation :Geometry row{i}Geometry :AbsoluteCoordinate absolutePointer) {i} {hover})'
hover=f'(select (UserInterface|Geometry|IsUnderLocation :Geometry clipGeometry :AbsoluteCoordinate absolutePointer) {hover} 0)'
geometry_reads='\n'.join(f'    (bind row{i}Geometry (Widget|GetCachedGeometry :self {g(f"Day{i}Button")}))' for i in range(1,8))
pointer=f'''(fn UpdatePointerIntent ()
  (if (== {g('PageIndex')} 1)
    (bind pointer (Viewport|GetMousePositionOnViewport))
    (bind (mx my) (Math|Vector2D|BreakVector2D :InVec pointer))
    (bind platformPointer (Viewport|GetMousePositionOnPlatform))
    (bind absolutePointer (select {g('HasPointerMotion')} {g('QueuedPointerScreen')} platformPointer))
    (bind clipGeometry (Widget|GetCachedGeometry :self {g('DayScroll')}))
{geometry_reads}
    (bind hovered {hover})
    (if {g('PointerReady')}
      (if (and {g('HasPointerMotion')} (> (+ (* (- mx {g('LastPointerX')}) (- mx {g('LastPointerX')})) (* (- my {g('LastPointerY')}) (- my {g('LastPointerY')}))) 4.0))
        {s('PendingHoverDay','hovered')}
        {s('LastPointerX','mx')}
        {s('LastPointerY','my')})
      (else (CallFunction|ResetPointerIntent)))
    {s('HasPointerMotion','false')}
    (if (and (<= {g('SnapRemaining')} 0.0) (<= {g('HoverQuiet')} 0.0))
      (if (and (> {g('PendingHoverDay')} 0) (and (== {g('PendingHoverDay')} hovered) (!= {g('PendingHoverDay')} {g('ActiveDay')})))
        (CallFunction|SelectDay :Day {g('PendingHoverDay')})
        (else {s('PendingHoverDay','0')})))
    (else (CallFunction|ResetPointerIntent)))
)'''
write('UpdatePointerIntent',pointer)

select=['(fn SelectDay (Day)',f'  (bind selected (Math|Integer|Clamp(Integer) :Value Day :Min 1 :Max 7))',
        '  (CallFunction|ResetPointerIntent)',f'  {s("WheelAccumulator","0.0")}',
        f'  {s("SelectedDay","selected")}',f'  {s("ActiveDay","selected")}',
        f'  {s("WindowFirstDay","(Math|Integer|Clamp(Integer) :Value (- selected 2) :Min 1 :Max 5)")}',
        f'  {s("HoverQuiet","1.2")}',f'  {s("SnapRemaining","1.0")}',
        '  (CallFunction|CenterSelectedDay)','  (switch int selected',
        f'    (:0 (Widget|SetText(Text) :self {g("DayCounter")} :InText "— / 07"))']
for i in range(1,8):select.append(f'    (:{i} (Widget|SetText(Text) :self {g("DayCounter")} :InText "{i:02} / 07"))')
select+=['  )',')'];write('SelectDay','\n'.join(select))

step=(OUT/'snap_StepDay.dsl').read_text(encoding='utf8').replace('(fn StepDay (Step)','(fn StepDay (Step)\n  (CallFunction|ResetPointerIntent)\n  (Variables|Default|SetWheelAccumulator 0.0)')
write('StepDay',step)
wheel=(OUT/'snap_OnMouseWheel.dsl').read_text(encoding='utf8')
wheel=wheel.replace('(bind delta (Input|PointerEvent|GetWheelDelta :Input MouseEvent))','(CallFunction|ResetPointerIntent)\n    (bind delta (Input|PointerEvent|GetWheelDelta :Input MouseEvent))')
# DSL bind is a pure pin alias, not an evaluated temporary. Read the stored
# accumulator AFTER its setter, otherwise the same delta is added twice.
wheel=wheel.replace('Absolute(Float) accumulated','Absolute(Float) (Variables|Default|GetWheelAccumulator)').replace('(< accumulated 0.0)','(< (Variables|Default|GetWheelAccumulator) 0.0)')
write('OnMouseWheel',wheel)

opened='false'
for i in range(3,0,-1):opened=f'(or (and (== {g("SelectedDay")} {i}) (> {g(f"Open{i}")} 0.72)) {opened})'
connection=(OUT/'OpenConnection.dsl').read_text(encoding='utf8')
body=connection[connection.index('\n')+1:connection.rfind(')')]
connection=f'''(fn OpenConnection ()
  (if (and (== {g('PageIndex')} 1) (and (>= {g('SelectedDay')} 1) (and (<= {g('SelectedDay')} 3) (and (== {g('ActiveDay')} {g('SelectedDay')}) {opened}))))
{body}
  )
)'''
write('OpenConnection',connection)
back=(OUT/'snap_Back.dsl').read_text(encoding='utf8').replace('(fn Back ()','(fn Back ()\n  (CallFunction|ResetPointerIntent)\n  (Variables|Default|SetWheelAccumulator 0.0)')
write('Back',back)

actors=g('Day7Actors')
for i in range(6,0,-1):actors=f'(select (== Day {i}) {g(f"Day{i}Actors")} {actors})'
drive=['(fn DriveMouthDay (Day Open Height Center Hidden Scale)',
       '  (bind isStage (or (== (Variables|Default|GetPageIndex) 1) (== (Variables|Default|GetPageIndex) 5)))',
       '  (bind effectiveHidden (or Hidden (and isStage (or (<= (+ Center (* Height 0.5)) 80.5) (>= (- Center (* Height 0.5)) 819.5)))))',
       f'  (bind actors {actors})',
       '  (for actor actors',
       '    (Rendering|SetActorHiddenInGame :self actor :bNewHidden effectiveHidden)',
       '    (bind loc (Transformation|GetActorLocation :self actor))',
       '    (Transformation|SetActorLocation :self actor :NewLocation (Math|Vector|MakeVector :X (.x loc) :Y (.y loc) :Z (select (or (== (Variables|Default|GetPageIndex) 1) (== (Variables|Default|GetPageIndex) 5)) (- 450.0 Center) 0.0)) :bSweep false :bTeleport true)',
       '    (Transformation|SetActorScale3D :self actor :NewScale3D (Math|Vector|MakeVector Scale Scale Scale))',
       '    (bind sk (Utilities|Casting|CastToSkeletalMeshActor :Object actor)',
       '      (:then (bind mesh (Class|SkeletalMeshActor|GetSkeletalMeshComponent :self sk))',
       '        (if (!= (Class|SceneComponent|GetHiddeninGame :self mesh) effectiveHidden) (Development|SetHiddeninGame :self mesh :NewHidden effectiveHidden :bPropagateToChildren true))']
for suffix,k in [('000',0.),('025',.25),('050',.5),('075',.75)]:
    value=f'(Math|Float|Clamp(Float) (- 1.0 (Math|Float|Absolute(Float) (* (- Open {k}) 4.0))) 0.0 1.0)'
    drive+=[f'        (Components|SkeletalMesh|SetMorphTarget :self mesh :MorphTargetName "Pose_{suffix}" :Value {value} :bRemoveZeroWeight true)']
drive+=['        (Rendering|Material|SetScalarParameterValueonMaterials :self mesh :ParameterName "OpenAmount" :ParameterValue Open)',
        '        (Rendering|Material|SetScalarParameterValueonMaterials :self mesh :ParameterName "CardHalfHeight" :ParameterValue (/ (select (or (== (Variables|Default|GetPageIndex) 1) (== (Variables|Default|GetPageIndex) 5)) Height 370.0) (* 2.0 Scale))))',
        '      (:CastFailed))','  )',')']
write('DriveMouthDay','\n'.join(drive))

stage=['(fn UpdateStage ()',f'  (bind page {g("PageIndex")})','  (bind isDays (== page 1))',
       '  (bind isStage (or isDays (== page 5)))',
       '  (bind dt (Utilities|Time|GetWorldDeltaSeconds))',
       f'  {s("HoverQuiet",f"(Math|Float|Clamp(Float) (- {g('HoverQuiet')} dt) 0.0 2.0)")}',
       f'  {s("SnapRemaining",f"(Math|Float|Clamp(Float) (- {g('SnapRemaining')} dt) 0.0 2.0)")}',
       '  (CallFunction|UpdatePointerIntent)',
       f'  {s("StageScroll",f"(Math|Interpolation|FInterpTo :Current {g('StageScroll')} :Target {g('StageScrollTarget')} :DeltaTime dt :InterpSpeed 12.0)")}',
       f'  (bind scroll {g("StageScroll")})',
       f'  (Widget|SetScrollOffset :self {g("DayScroll")} :NewScrollOffset scroll)',
       f'  (bind first (Math|Integer|Clamp(Integer) :Value {g("WindowFirstDay")} :Min 1 :Max 5))',f'  (bind active {g("ActiveDay")})']
for i in range(1,8):
    target=f'(select isStage (select (== active {i}) 1.0 0.0) {1.0 if i==3 else 0.0})'
    stage+=[f'  {s(f"Open{i}",f"(Math|Interpolation|FInterpTo :Current {g(f'Open{i}')} :Target {target} :DeltaTime dt :InterpSpeed 9.0)")}',
            f'  (bind open{i} {g(f"Open{i}")})',f'  (bind inWindow{i} (and (>= {i} first) (< {i} (+ first 3))))']
total='0.0'
for i in range(1,8):total=f'(+ {total} (select inWindow{i} open{i} 0.0))'
stage+=[f'  (bind windowOpen {total})']
for i in range(1,8):
    stage+=[f'  (bind height{i} (select inWindow{i} (+ 246.666667 (- (* open{i} 185.0) (* windowOpen 61.666667))) 246.666667))',
            f'  (Layout|SizeBox|SetHeightOverride :self {g(f"Day{i}Size")} :InHeightOverride height{i})']
    if i<4:
        stage+=[f'  (if (and isDays (> open{i} {0.98 if i==3 else 0.72})) (Widget|SetVisibility :self {g(f"Day{i}CavityControls")} :InVisibility "SelfHitTestInvisible") (else (Widget|SetVisibility :self {g(f"Day{i}CavityControls")} :InVisibility "{ "Hidden" if i==3 else "Collapsed" }")))']
    else:
        stage+=[f'  (if (and isDays (> open{i} 0.72)) (Widget|SetVisibility :self {g(f"Day{i}ComingSoon")} :InVisibility "HitTestInvisible") (else (Widget|SetVisibility :self {g(f"Day{i}ComingSoon")} :InVisibility "Collapsed")))']
prior='0.0'
for i in range(1,8):
    stage+=[f'  (bind center{i} (+ 80.0 (- (+ {prior} (* height{i} 0.5)) scroll)))',
            f'  (bind hidden{i} (select isStage (or (< (+ center{i} (* height{i} 0.5)) 80.0) (> (- center{i} (* height{i} 0.5)) 820.0)) {"false" if i==3 else "true"}))',
            f'  (bind scale{i} {"(* 0.54 (+ 0.9956556266900379 (* open3 0.0)))" if i==3 else "(* 1.0 0.54)"})',
            f'  (CallFunction|DriveMouthDay :Day {i} :Open open{i} :Height height{i} :Center center{i} :Hidden hidden{i} :Scale scale{i})']
    prior=f'(+ {prior} height{i})'
for var,day,threshold in [('InteriorActors',3,.05),('TongueActors',3,.05),('Interior1Actors',1,.05),('Interior2Actors',2,.05),
                          ('Tongue1Actors',1,.05),('Tongue2Actors',2,.05),
                          ('CharacterActors',3,.72),('Character1Actors',1,.72),('Character2Actors',2,.72)]:
    stage+=[f'  (for child {g(var)}',f'    (Rendering|SetActorHiddenInGame :self child :bNewHidden (or hidden{day} (< open{day} {threshold})))']
    if var.startswith(('Interior','Tongue')):
        stage+=['    (bind staticActor (Utilities|Casting|CastToStaticMeshActor :Object child)',
                '      (:then (bind staticMesh (Class|StaticMeshActor|GetStaticMeshComponent :self staticActor))',
                f'        (Rendering|Material|SetScalarParameterValueonMaterials :self staticMesh :ParameterName "CardHalfHeight" :ParameterValue (/ (select isStage height{day} 370.0) (* {2.6666666666666665 if day==3 else 2.0} scale{day}))))',
                '      (:CastFailed))']
    stage+=['  )']
stage+=[')']
stage='\n'.join(stage)
write('UpdateStage',stage)

events=(OUT/'snap_EventGraph.dsl').read_text(encoding='utf8')
events=events.replace(s('SelectedDay','3'),s('SelectedDay','3')+'\n  '+s('WindowFirstDay','1'))
events=events.replace('(event UserInterface|EventTick (MyGeometry InDeltaTime) (CallFunction|UpdateStage))','')
events=events.replace('(CallFunction|ShowPage :Index 1)\n  (CallFunction|CenterSelectedDay)',f'''{s('WheelAccumulator','0.0')}
  {s('StageScroll','0.0')}
  {s('StageScrollTarget','0.0')}
  (Widget|SetScrollOffset :self {g('DayScroll')} :NewScrollOffset 0.0)
  (Widget|SetText(Text) :self {g('DayCounter')} :InText "01 / 07")
  (CallFunction|ShowPage :Index 1)
  (CallFunction|ResetPointerIntent)
  (CallFunction|CenterSelectedDay)''')
events=events.replace('(Variables|Default|SetActiveDay 0) (Variables|Default|SetHoverQuiet 1.2)','(CallFunction|ResetPointerIntent) (Variables|Default|SetActiveDay 0) (Variables|Default|SetHoverQuiet 1.2)')
write('EventGraph',events,call(BP,'get_graph',blueprint=w,graph_name='EventGraph'))
key=(OUT/'snap_OnPreviewKeyDown.dsl').read_text(encoding='utf8')
# Enter opens a known closed row; once open, it activates that row's Play.
# Future placeholders never launch. Keep current Esc and arrow behavior.
old='(else (return (Widget|EventReply|Unhandled))))))'
opened='false'
for i in range(3,0,-1):opened=f'(or (and (== {g("SelectedDay")} {i}) (> {g(f"Open{i}")} 0.72)) {opened})'
backFocused='false'
for button in ['DaysBackButton']+[f'Day{i}CollapseButton' for i in range(1,4)]:
    backFocused=f'(or (Widget|HasKeyboardFocus :self {g(button)}) {backFocused})'
new=f'''(elif (and (not {backFocused}) (and (== {g('PageIndex')} 1) (Utilities|String|EqualExactly(String) name "Enter")))
          (if (<= {g('SelectedDay')} 3)
            (if (and (== {g('ActiveDay')} {g('SelectedDay')}) {opened})
              (CallFunction|OpenConnection)
              (else (CallFunction|SelectDay :Day {g('SelectedDay')}))))
          (return (Widget|EventReply|Handled))
          (else (return (Widget|EventReply|Unhandled)))))))'''
assert old in key
key=key.replace(old,new)
write('OnPreviewKeyDown',key)
call(BP,'compile_blueprint',blueprint=w,warnings_as_errors=True)

# PlayerController Tick precedes the viewport scene draw. A late UserWidget
# Tick produced new Slate labels over the previous frame's 3D transforms.
pc=task_asset('BP_Menu3DPlayerController')
if 'MenuWidget' not in call(BP,'list_variables',blueprint=pc):
    call(BP,'add_object_variable',blueprint=pc,name='MenuWidget',object_class=ref(BASE+'/WBP_MainMenu3D.WBP_MainMenu3D_C'))
pc_events=(OUT/'PC_EventGraph.dsl').read_text(encoding='utf8')
pc_events=pc_events.replace('  (UserInterface|Viewport|AddtoViewport','  (Variables|Default|SetMenuWidget menu)\n  (UserInterface|Viewport|AddtoViewport')
tick_setup=[]
for i in range(1,8):
    tick_setup+=[f'  (bind mouths{i} (Actor|GetAllActorswithTag :Tag "Menu3D_DAY0{i}"))',
                 f'  (for actor mouths{i}',
                 '    (bind sk (Utilities|Casting|CastToSkeletalMeshActor :Object actor)',
                 '      (:then (bind mesh (Class|SkeletalMeshActor|GetSkeletalMeshComponent :self sk))',
                 '        (Class|SkinnedMeshComponent|SetVisibilityBasedAnimTickOption :self mesh :VisibilityBasedAnimTickOption "AlwaysTickPoseAndRefreshBones")',
                 '        (Class|SkinnedMeshComponent|SetEnableUpdateRateOptimizations :self mesh :bEnableUpdateRateOptimizations false)',
                 '        (Components|Tick|AddTickPrerequisiteActor :self mesh :PrerequisiteActor self))',
                 '      (:CastFailed)))']
for i in range(1,4):
    tick_setup+=[f'  (bind characters{i} (Actor|GetAllActorswithTag :Tag "Menu3D_DAY0{i}_Character"))',
                 f'  (for actor characters{i}',
                 '    (bind sk (Utilities|Casting|CastToSkeletalMeshActor :Object actor)',
                 '      (:then (bind mesh (Class|SkeletalMeshActor|GetSkeletalMeshComponent :self sk))',
                 '        (Components|Tick|AddTickPrerequisiteActor :self mesh :PrerequisiteActor self))',
                 '      (:CastFailed)))']
pc_events=pc_events.replace('  (bind cameras ', '\n'.join(tick_setup)+'\n  (bind cameras ',1)
pc_events+='''\n(event AddEvent|EventTick (DeltaSeconds)
  (Utilities|IsValid (Variables|Default|GetMenuWidget)
    (:"Is Valid" (Class|WBPMainMenu3D|UpdateStage :self (Variables|Default|GetMenuWidget)))
    (:"Is Not Valid")))\n'''
write('PC_EventGraph',pc_events,call(BP,'get_graph',blueprint=pc,graph_name='EventGraph'))
call(BP,'compile_blueprint',blueprint=pc,warnings_as_errors=True)
call(ASSET,'save_assets',asset_paths=[BASE+'/WBP_MainMenu3D',BASE+'/BP_Menu3DPlayerController'])
report={'status':'THREE_MOUTH_ACCORDION_COMPILED_SAVED_PENDING_ROOT_PIE','asset':BASE+'/WBP_MainMenu3D',
        'assets':[BASE+'/WBP_MainMenu3D',BASE+'/BP_Menu3DPlayerController'],
        'compiled_warnings_as_errors':True,
        'body_height':740,'visible_rows':3,'window_first':'clamp(SelectedDay-2,1,5)',
        'row_heights':{'all_closed':246.666667,'selected_open':370,'other_two':185},
        'row_formula':'246.666667 +185*ownOpen -61.666667*sumVisibleOpen; constant740 total',
        'base_actor_scale':.54,'DAY03_scale':'.5376540384126205 constant uniform XYZ',
        'material_card_half_height':'RowHeight/(2*ActorScale)',
        'wheel':'One event at abs(delta)>=.5 advances one day; fractional packets accumulate from stored value, not pure pin reevaluation',
        'scroll_sync':'PC Tick owns UpdateStage; StageScroll FInterp speed12; identical SetScrollOffset and world centers; no WidgetTick/deferredScrollIntoView',
        'skeletal_tick_order':'BeginPlay: seven mouth components depend on PC tick, AlwaysTickPoseAndRefreshBones/UROoff; three character components dependency only',
        'hover':'Actual OnMouseMove queues screen position; PC consumes motion>2px, cached row geometry with DayScroll clip; deferred intent only if same row; select/wheel/Back clear it',
        'catalog_reset':'Counter01/07, selected1 active0, zero scroll and wheel accumulator, viewport pointer baseline',
        'connection':'Known selected1..3, active matching, ownOpen>.72; Page5 preserves entire staged pose/layout/visibility',
        'characters':'Own-day tag cached; visible own Open>.72 and mouth visible',
        'world_actor_days':7,'shared_actor_driver':'DriveMouthDay primitive-input function; one native pose implementation reused for all7',
        'outgoing_visibility':'Actual row viewport intersection, not immediate logical-window hiding',
        'future_content':'Generic unassigned3D blocking under ComingSoon labels; no inner Play',
        'enter':'Known selected1..3 opens, then activates Play when open; future never launches',
        'level_PIE_material_geometry_mutations':False}
(OUT/'three_mouth_accordion_compile_report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
(OUT/'accordion_input_sync_compile_report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report,indent=2),flush=True)
