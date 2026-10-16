"""Complete only the owned WBP cavity controls and matching body framing."""
from pathlib import Path
import json
from menu_native_client import call, props, ref, task_asset, BP, UMG
OUT=Path(__file__).resolve().parent/'Evidence';bp=task_asset('WBP_MainMenu3D')
tree=call(UMG,'GetWidgets',widgetBlueprint=bp);items={x['widgetName']:x for x in tree['widgets']}
def rgba(c):return dict(zip('rgba',list(c)+[1]*(4-len(c))))
def slate(c):return dict(specifiedColor=rgba(c),colorUseRule='UseColor_Specified')
def brush(c,r=14):return dict(drawAs='RoundedBox',resourceObject='None',tintColor=slate(c),outlineSettings=dict(roundingType='FixedRadius',cornerRadii=dict(x=r,y=r,z=r,w=r)))
def add(kind,name,parent,rect=None,values=None,variable=False):
    n=call(UMG,'AddWidget',widgetBlueprint=bp,widgetClass=ref('/Script/UMG.'+kind),widgetDisplayName=name,parentWidget=parent)
    props(n['widget'],{'visibility':'SelfHitTestInvisible' if kind=='CanvasPanel' else 'HitTestInvisible',**(values or {})})
    if rect:
        x,y,w,h=rect
        props(n['slot'], {'layoutData': {'offsets':dict(left=x,top=y,right=w,bottom=h), 'anchors':dict(minimum=dict(x=0,y=0),maximum=dict(x=0,y=0))}})
    if variable:call(UMG,'ToggleWidgetAsVariable',widgetBlueprint=bp,widget=n['widget'],bIsVariable=True)
    return n['widget']
def button(name,parent,rect,label,primary=False):
    c=(.20,.93,.73,1) if primary else (.018,.028,.035,.9)
    b=add('Button',name,parent,rect,dict(visibility='Visible',isFocusable=True,widgetStyle=dict(normal=brush(c),hovered=brush((.4,1,.82,1) if primary else (.08,.15,.17,1)),pressed=brush((.1,.65,.5,1)))),True)
    add('TextBlock',name+'_Text',b,values=dict(text=label,font=dict(size=22,typefaceFontName='Bold'),colorAndOpacity=slate((.01,.02,.025,1) if primary else (.96,.93,.85,1)),justification='Center'))
    return b
def rect(name,value):
    x,y,w,h=value
    props(items[name]['slot'], {'layoutData': {'offsets':dict(left=x,top=y,right=w,bottom=h)}})
rect('DayScroll',(0,80,1600,740));rect('DaysHeader',(0,0,1600,80));rect('DaysFooter',(0,820,1600,80))
rect('DaysBrand',(35,17,650,49));rect('DaysHint',(1050,27,490,38));rect('DaysBackButton',(35,835,170,50));rect('DaysScrollHint',(340,847,1000,30));rect('DayCounter',(1410,844,150,35))
props(items['FullscreenButton_Text']['widget'],dict(text='WINDOWED / BORDERLESS'))
for i in range(1,8):
    props(items[f'Day{i}Button']['slot'], {'layoutData': {'offsets':dict(left=0,top=0,right=0,bottom=0), 'anchors':dict(minimum=dict(x=0,y=0),maximum=dict(x=1,y=1))}})
    if i>3:continue
    if f'Day{i}CavityControls' in items:raise RuntimeError('Controls already authored; preserve them')
    panel=add('CanvasPanel',f'Day{i}CavityControls',items[f'Day{i}Canvas']['widget'],(0,0,1600,681),dict(visibility='Collapsed'),True)
    species=['HUMAN','PREDATOR','VAMPIRE'][i-1]
    add('TextBlock',f'Day{i}CavityHeading',panel,(590,202,420,60),dict(text=f'DAY {i:02}',font=dict(size=40,typefaceFontName='Bold'),colorAndOpacity=slate((.96,.93,.85,1)),justification='Center'))
    add('TextBlock',f'Day{i}CavitySpecies',panel,(590,262,420,40),dict(text=species,font=dict(size=19,typefaceFontName='Bold'),colorAndOpacity=slate((.96,.93,.85,1)),justification='Center'))
    button(f'Day{i}PlayButton',panel,(650,329,300,68),'PLAY',True)
    button(f'Day{i}CollapseButton',panel,(720,420,160,48),'BACK')
call(UMG,'CompileWidgetBlueprint',widgetBlueprint=bp)
for i in range(1,4):
    for suffix in ('PlayButton','CollapseButton'):
        assert call(UMG,'BindToEventProperty',widgetBlueprint=bp,eventName='OnClicked',propertyName=f'Day{i}{suffix}',propertyClass=ref('/Script/UMG.Button'))
call(BP,'add_variable',blueprint=bp,name='bBorderless',type_name='bool')
graph=call(BP,'get_graph',blueprint=bp,graph_name='EventGraph')
selected=['Math|Float|Clamp(Float)','Math|Float|Absolute(Float)','Widget|HasUserFocus','Settings|SetOverallScalabilityLevel','Settings|ApplySettings','Settings|SaveSettings','Settings|SetFullscreenMode',
          'Input|KeyEvent|GetKey','Input|Key|GetKeyDisplayName','Widget|EventReply|Handled','Widget|EventReply|Unhandled','Game|Player|SetViewTargetwithBlend','Transformation|GetActorLocation','Utilities|IsValid']
pins={t:call(BP,'get_node_type_pins',graph=graph,type_id=t) for t in selected}
(OUT/'logic_more_pins.json').write_text(json.dumps(pins,indent=2),encoding='utf8')
(OUT/'complete_widget_tree.json').write_text(json.dumps(call(UMG,'GetWidgets',widgetBlueprint=bp),indent=2),encoding='utf8')
(OUT/'all_click_events.dsl').write_text(call(BP,'read_graph_dsl',graph=graph),encoding='utf8')
print(json.dumps({t:{k:[(p['name'],p['type_id']) for p in v[k]] for k in ('input_pins','output_pins')} for t,v in pins.items()},indent=2),flush=True)
