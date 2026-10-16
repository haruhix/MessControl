"""Owned WBP: one wheel notch selects one day, centered native smooth snap.

No level, PIE, C++, mesh or material mutations. Existing compiled stage/morph
logic is retained; free scrolling and focus-driven selection are removed.
"""
from pathlib import Path
import argparse
import json
from menu_native_client import call, props, ref, task_asset, BASE, BP, UMG, OBJ, ASSET

OUT=Path(__file__).resolve().parent/'Evidence'
parser=argparse.ArgumentParser();parser.add_argument('--start',default='CenterSelectedDay');args=parser.parse_args()
started=False
w=task_asset('WBP_MainMenu3D')
tree=call(UMG,'GetWidgets',widgetBlueprint=w)
items={x['widgetName']:x for x in tree['widgets']}
widget_vars={x['widgetName'] for x in tree['widgets'] if x['bIsVariable']}
def g(name):return f'(Variables|{"WBP_MainMenu3D" if name in widget_vars else "Default"}|Get{name})'
def s(name,value):return f'(Variables|Default|Set{name} {value})'
def focus(name):return f'(Widget|SetKeyboardFocus :self {g(name)})'
graphs=json.loads((OUT/'logic_functions.json').read_text())
existing=set(call(BP,'list_variables',blueprint=w))
for name,kind in [('SelectedDay','int'),('SnapRemaining','float'),('WheelAccumulator','float')]:
    if name not in existing:call(BP,'add_variable',blueprint=w,name=name,type_name=kind)
for name,param in [('CenterSelectedDay',None),('StepDay','Step')]:
    try:graphs[name]=call(BP,'get_graph',blueprint=w,graph_name=name)
    except RuntimeError:graphs[name]=None
    if not graphs[name]:
        graphs[name]=call(BP,'add_function_graph',blueprint=w,graph_name=name)
        if param:call(BP,'add_function_param',graph=graphs[name],param_name=param,param_type='int',input_param=True)
try:graphs['OnMouseWheel']=call(BP,'get_graph',blueprint=w,graph_name='OnMouseWheel')
except RuntimeError:graphs['OnMouseWheel']=None
if not graphs['OnMouseWheel']:graphs['OnMouseWheel']=call(BP,'add_function_graph',blueprint=w,graph_name='OnMouseWheel')

props(w,{'bIsFocusable':True})
props(items['DayScroll']['widget'],{
    'consumeMouseWheel':'Never','wheelScrollMultiplier':0.,'bAnimateWheelScrolling':False,
    'allowOverscroll':False,'scrollBarVisibility':'Collapsed','alwaysShowScrollbar':False,
    'alwaysShowScrollbarTrack':False,'bAllowRightClickDragScrolling':False,
    'bEnableTouchScrolling':False,'scrollWhenFocusChanges':'NoScroll',
    'bIsFocusable':False,'analogMouseWheelKey':{'keyName':'None'},
    'scrollAnimationInterpolationSpeed':10.0})
# Half-viewport content padding lets the first and last real row center too.
# Natural row height is untouched, and UpdateStage accounts for this padding.
props(items['Day1Size']['slot'],{'padding':{'top':370.}})
props(items['Day7Size']['slot'],{'padding':{'bottom':370.}})
props(items['DaysScrollHint']['widget'],{'text':'WHEEL  ·  ONE DAY PER STEP     ↑ / ↓  SELECT DAY     ESC  BACK'})

def write(name,code,graph=None):
    global started
    if name==args.start:started=True
    if not started:return
    (OUT/('snap_'+name+'.dsl')).write_text(code,encoding='utf8')
    call(BP,'write_graph_dsl',graph=graph or graphs[name],code=code)
    print('GRAPH_WRITTEN',name,flush=True)

center=['(fn CenterSelectedDay ()','  (Widget|ForceLayoutPrepass)',
        f'  (switch int {g("SelectedDay")}', '    (:0)']
for i in range(1,8):
    center.append(f'    (:{i} (Widget|ScrollWidgetIntoView :self {g("DayScroll")} :WidgetToFind {g(f"Day{i}Size")} :AnimateScroll true :ScrollDestination "Center" :Padding 0.0))')
center+=['  )',')'];write('CenterSelectedDay','\n'.join(center))

step=f'''(fn StepDay (Step)
  (bind previous (Math|Integer|Clamp(Integer) :Value {g('SelectedDay')} :Min 1 :Max 7))
  (bind next (Math|Integer|Clamp(Integer) :Value (+ previous Step) :Min 1 :Max 7))
  (if (!= next previous) (CallFunction|SelectDay :Day next))
)'''
write('StepDay',step)

select=['(fn SelectDay (Day)',f'  (bind selected (Math|Integer|Clamp(Integer) :Value Day :Min 1 :Max 7))',
        f'  {s("SelectedDay","selected")}',f'  {s("ActiveDay","selected")}',f'  {s("HoverQuiet","1.2")}',
        f'  {s("SnapRemaining","1.0")}', '  (CallFunction|CenterSelectedDay)', '  (switch int selected',
        f'    (:0 (Widget|SetText(Text) :self {g("DayCounter")} :InText "— / 07"))']
for i in range(1,8):select.append(f'    (:{i} (Widget|SetText(Text) :self {g("DayCounter")} :InText "{i:02} / 07"))')
select+=['  )',')'];write('SelectDay','\n'.join(select))

wheel=f'''(fn OnMouseWheel (MyGeometry MouseEvent)
  (if (== {g('PageIndex')} 1)
    (bind delta (Input|PointerEvent|GetWheelDelta :Input MouseEvent))
    (if (>= (Math|Float|Absolute(Float) delta) 0.5)
      {s('WheelAccumulator','0.0')}
      (CallFunction|StepDay :Step (select (< delta 0.0) 1 -1))
      (else
        (bind accumulated (+ (select (< (* {g('WheelAccumulator')} delta) 0.0) 0.0 {g('WheelAccumulator')}) delta))
        {s('WheelAccumulator','accumulated')}
        (if (>= (Math|Float|Absolute(Float) accumulated) 0.5)
          (CallFunction|StepDay :Step (select (< accumulated 0.0) 1 -1))
          {s('WheelAccumulator','0.0')})))
    (return (Widget|EventReply|Handled))
    (else (return (Widget|EventReply|Unhandled))))
)'''
write('OnMouseWheel',wheel)

# Derive stage from the maintained authored generator, changing only its input
# policy and adding the final native centering request after size overrides.
source=(OUT.parent/'write_menu_blueprint_logic.py').read_text(encoding='utf8')
stage_source=source[source.index("stage=['(fn UpdateStage"):source.index("key='''")]
captured={}
scope={'g':g,'s':s,'write':lambda name,code:captured.update({name:code})}
exec(stage_source,scope)
stage=captured['UpdateStage']
# The source generator starts its measured row stack at zero; pad it by370.
stage=stage.replace('(bind center1 (+ 80.0 (- (+ 0.0 ', '(bind center1 (+ 80.0 (- (+ 370.0 ')
stage=stage.replace('(bind center2 (+ 80.0 (- (+ (+ 0.0 ', '(bind center2 (+ 80.0 (- (+ (+ 370.0 ')
stage=stage.replace('(bind center3 (+ 80.0 (- (+ (+ (+ 0.0 ', '(bind center3 (+ 80.0 (- (+ (+ (+ 370.0 ')
# Wheel owns SelectedDay through a transition. Merely moving rows beneath a
# stationary pointer or moving existing keyboard focus cannot change it.
lines=stage.splitlines()
lines=[line for line in lines if not ('(bind focused ' in line or 'HasUserFocus' in line or 'CallFunction|SelectDay :Day focused' in line or 'SetLastFocusDay' in line)]
lines=[f'  (if (and isDays (and (<= {g("SnapRemaining")} 0.0) (and (> hovered 0) (and (!= hovered {g("LastHoverDay")}) (<= {g("HoverQuiet")} 0.0))))) (CallFunction|SelectDay :Day hovered))' if 'CallFunction|SelectDay :Day hovered' in line else line for line in lines]
# Native layout resolves after size changes; request the selected child every
# transition tick, so its changing natural height never shifts the endpoint.
lines[-1:-1]=[f'  {s("SnapRemaining",f"(Math|Float|Clamp(Float) (- {g('SnapRemaining')} dt) 0.0 2.0)")}',
             f'  (if (and isDays (> {g("SnapRemaining")} 0.0)) (CallFunction|CenterSelectedDay))']
stage='\n'.join(lines)
assert stage.count('370.0')==3
write('UpdateStage',stage)

page=(OUT/'ShowPage.dsl').read_text(encoding='utf8')
old=f'''(:1 {focus('Day3Button')}
        (Widget|ScrollWidgetIntoView :self {g('DayScroll')} :WidgetToFind {g('Day3Size')} :AnimateScroll false :ScrollDestination "Center" :Padding 0.0))'''
assert old in page
page=page.replace(old,'(:1 (Widget|SetKeyboardFocus) (CallFunction|CenterSelectedDay))')
write('ShowPage',page)

back=(OUT/'Back.dsl').read_text(encoding='utf8')
back=back.replace(s('HoverQuiet','0.5'),s('HoverQuiet','1.2')+'\n        '+s('SnapRemaining','1.0'))
write('Back',back)

events=(OUT/'EventGraph.dsl').read_text(encoding='utf8')
events=events.replace(s('Open3','1.0'),s('Open3','1.0')+'\n  '+s('SelectedDay','3')+'\n  '+s('SnapRemaining','0.0')+'\n  '+s('WheelAccumulator','0.0'))
events=events.replace(s('ActiveDay','3')+' (CallFunction|ShowPage :Index 1)','(CallFunction|ShowPage :Index 1) (CallFunction|SelectDay :Day 3)')
events=events.replace(s('HoverQuiet','0.5'),s('HoverQuiet','1.2')+' '+s('SnapRemaining','1.0'))
write('EventGraph',events,call(BP,'get_graph',blueprint=w,graph_name='EventGraph'))

key=f'''(fn OnPreviewKeyDown (MyGeometry InKeyEvent)
  (bind key (Input|KeyEvent|GetKey :Input InKeyEvent))
  (bind display (Input|Key|GetKeyDisplayName :Key key :bLongDisplayName true))
  (bind name (Utilities|String|ToString(Text) :InText display))
  (if (or (Utilities|String|EqualExactly(String) name "Escape") (Utilities|String|EqualExactly(String) name "Gamepad Face Button Right"))
    (CallFunction|Back) (return (Widget|EventReply|Handled))
    (elif (and (== {g('PageIndex')} 1) (or (Utilities|String|EqualExactly(String) name "Down") (Utilities|String|EqualExactly(String) name "Gamepad D-pad Down")))
      (CallFunction|StepDay :Step 1) (return (Widget|EventReply|Handled))
      (elif (and (== {g('PageIndex')} 1) (or (Utilities|String|EqualExactly(String) name "Up") (Utilities|String|EqualExactly(String) name "Gamepad D-pad Up")))
        (CallFunction|StepDay :Step -1) (return (Widget|EventReply|Handled))
        (else (return (Widget|EventReply|Unhandled))))))
)'''
write('OnPreviewKeyDown',key)

call(BP,'compile_blueprint',blueprint=w,warnings_as_errors=True)
call(ASSET,'save_assets',asset_paths=[BASE+'/WBP_MainMenu3D'])
(OUT/'snap_wheel_compile_report.json').write_text(json.dumps({
    'status':'COMPILED_SAVED_PENDING_ROOT_SOLO_PIE','asset':BASE+'/WBP_MainMenu3D',
    'free_scroll':False,'wheel_contract':'One normal detent/event -> exactly one adjacent day; SelectedDay clamped1..7 immediately. Fractional trackpad packets accumulate to0.5.',
    'centering':'Native ScrollWidgetIntoView Center+AnimateScroll during natural-height transition;370px content padding at both ends.',
    'selection_lock':'No focus-driven selection; hover quiet1.2sec and edge state updated while snapping.',
    'focusable':True,'level_PIE_material_geometry_mutations':False},indent=2),encoding='utf8')
print('SNAP_WHEEL_COMPILED_SAVED',flush=True)
