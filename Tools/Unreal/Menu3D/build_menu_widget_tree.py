"""Author only the task-owned screen UMG over a real viewport 3D stage."""
from pathlib import Path
import argparse
import json
from menu_native_client import call, props, ref, UMG, BP, OBJ, task_asset

OUT = Path(__file__).resolve().parent / 'Evidence'
bp = task_asset('WBP_MainMenu3D')
tree = call(UMG, 'GetWidgets', widgetBlueprint=bp)
if tree['widgets']:
    parser=argparse.ArgumentParser();parser.add_argument('--replace-task-tree',action='store_true');args=parser.parse_args()
    if not args.replace_task_tree or tree['info']['inheritedWidgetCount']:
        raise RuntimeError('Existing task tree found; inspect before replacing')
    call(UMG,'RemoveWidget',widgetBlueprint=bp,widget=tree['widgets'][0]['widget'])
widgets, buttons = {}, []


def rgba(c): return dict(zip('rgba', list(c)+[1]*(4-len(c))))
def slate(c): return dict(specifiedColor=rgba(c), colorUseRule='UseColor_Specified')
INK=(.018,.028,.035,1);CREAM=(.96,.93,.85,1);MINT=(.20,.93,.73,1);MUTED=(.62,.68,.68,1)


def add(kind, name, parent=None, rect=None, values=None, variable=False):
    args = dict(widgetBlueprint=bp, widgetClass=ref('/Script/UMG.'+kind), widgetDisplayName=name)
    if parent: args['parentWidget']=parent
    item=call(UMG,'AddWidget',**args)
    widget=item['widget'];widgets[name]=widget
    props(widget, {'visibility':'SelfHitTestInvisible' if kind in ('CanvasPanel','ScaleBox','SizeBox','WidgetSwitcher','VerticalBox','Overlay') else 'HitTestInvisible', **(values or {})})
    if rect is not None:
        x,y,w,h=rect
        props(item['slot'],dict(layoutData=dict(offsets=dict(left=x,top=y,right=w,bottom=h),anchors=dict(minimum=dict(x=0,y=0),maximum=dict(x=0,y=0)),alignment=dict(x=0,y=0)),bAutoSize=False))
    if variable:
        call(UMG,'ToggleWidgetAsVariable',widgetBlueprint=bp,widget=widget,bIsVariable=True)
    return widget


def fill(item):
    node=next(x for x in call(UMG,'GetWidgets',widgetBlueprint=bp)['widgets'] if x['widget']==item)
    props(node['slot'],dict(layoutData=dict(offsets=dict(left=0,top=0,right=0,bottom=0),anchors=dict(minimum=dict(x=0,y=0),maximum=dict(x=1,y=1)))))


def brush(c,r=14):
    return dict(drawAs='RoundedBox',resourceObject='None',resourceName='None',imageType='NoImage',tintColor=slate(c),outlineSettings=dict(roundingType='FixedRadius',cornerRadii=dict(x=r,y=r,z=r,w=r)))


def box(name,parent,rect,color,radius=0):
    return add('Image',name,parent,rect,dict(brush=brush((1,1,1,1),radius),colorAndOpacity=rgba(color)))


def text(name,parent,rect,value,size=22,color=CREAM,center=False,variable=False):
    return add('TextBlock',name,parent,rect,dict(text=value,font=dict(size=size,typefaceFontName='Bold'),colorAndOpacity=slate(color),justification='Center' if center else 'Left',autoWrapText=True),variable)


def button(name,parent,rect,label,primary=False,quiet=False):
    color=MINT if primary else (.025,.046,.055,.95)
    if quiet:color=(.015,.022,.028,.45)
    style=dict(normal=brush(color),hovered=brush((.38,.99,.80,1) if primary else (.07,.13,.15,.98)),pressed=brush((.11,.60,.48,1) if primary else (.10,.18,.20,1)),disabled=brush((.025,.035,.04,.6)))
    b=add('Button',name,parent,rect,dict(visibility='Visible',widgetStyle=style,isFocusable=True),True)
    text(name+'_Text',b,None,label,23,INK if primary else CREAM,True)
    buttons.append(name)
    return b


root=add('CanvasPanel','RootCanvas')
scale=add('ScaleBox','ViewportScale',root,(0,0,0,0),dict(stretch='ScaleToFit'))
fill(scale)
design=add('SizeBox','DesignFrame',scale,values=dict(widthOverride=1600,heightOverride=900))
pages=add('WidgetSwitcher','Pages',design,variable=True)
main=add('CanvasPanel','MainPage',pages)
days=add('CanvasPanel','DaysPage',pages)
settings=add('CanvasPanel','SettingsPage',pages)
credits=add('CanvasPanel','CreditsPage',pages)
exit_page=add('CanvasPanel','ExitPage',pages)
launch=add('CanvasPanel','LaunchPage',pages)
for page,name in [(main,'Main'),(settings,'Settings'),(credits,'Credits'),(exit_page,'Exit'),(launch,'Launch')]:
    box(name+'Shade',page,(0,0,1600,900),(.012,.020,.027,.64))
text('MainBrand',main,(110,180,1000,110),'MESS / CONTROL',74)
text('MainTagline',main,(115,296,900,45),'Четыре маленьких зуба. Один большой беспорядок.',22,MUTED)
button('PlayButton',main,(115,390,350,76),'PLAY',True)
button('SettingsButton',main,(115,486,350,62),'SETTINGS')
button('CreditsButton',main,(115,562,350,62),'CREDITS')
button('ExitButton',main,(115,638,350,62),'EXIT')
text('MainFooter',main,(115,822,1000,30),'1–4 PLAYERS',16,MUTED)

box('DaysHeader',days,(0,0,1600,110),INK)
box('DaysFooter',days,(0,800,1600,100),INK)
text('DaysBrand',days,(40,25,650,54),'MESS / CONTROL',34)
text('DaysHint',days,(1050,34,490,44),'SELECT DAY  ·  1–4 PLAYERS',19,MUTED,True)
scroll=add('ScrollBox','DayScroll',days,(0,110,1600,690),dict(visibility='Visible',orientation='Orient_Vertical',bAnimateWheelScrolling=True,scrollbarThickness=dict(x=7,y=7),bEnableTouchScrolling=True,scrollWhenFocusChanges='AnimatedScroll',navigationDestination='IntoView'),True)
rows=add('VerticalBox','DayRows',scroll)
for i in range(1,8):
    size=add('SizeBox',f'Day{i}Size',rows,values=dict(widthOverride=1600,heightOverride=427),variable=True)
    panel=add('CanvasPanel',f'Day{i}Canvas',size)
    if i>3:box(f'Day{i}FutureBackdrop',panel,(0,0,1600,427),(.08,.022,.034,1))
    row=button(f'Day{i}Button',panel,(0,0,1600,427),'',quiet=True)
    # The transparent row is a native hit/focus surface over real world meshes.
    props(row,dict(widgetStyle=dict(normal=brush((0,0,0,0),0),hovered=brush((.04,.13,.12,.08),0),pressed=brush((.04,.13,.12,.13),0))))
    label='HUMAN' if i==1 else 'PREDATOR' if i==2 else 'VAMPIRE' if i==3 else 'COMING SOON'
    box(f'Day{i}Tag',panel,(32,25,390,58),CREAM,10)
    text(f'Day{i}Label',panel,(47,36,365,36),f'DAY {i:02}  |  {label}',20,INK)
    if i>3:
        text(f'Day{i}ComingSoon',panel,(500,174,600,64),'COMING SOON',32,MUTED,True)
button('DaysBackButton',days,(40,822,170,52),'BACK')
text('DaysScrollHint',days,(350,836,900,32),'SCROLL / ↑ ↓   ·   ENTER — SELECT   ·   ESC — BACK',16,MUTED,True)
text('DayCounter',days,(1410,834,150,35),'01 / 07',20,MINT,True,True)

text('SettingsTitle',settings,(110,135,1000,90),'SETTINGS',58)
text('SettingsDescription',settings,(115,242,950,50),'Настройки применяются к игре Unreal Engine.',22,MUTED)
button('QualityLowButton',settings,(115,350,260,66),'QUALITY: LOW')
button('QualityHighButton',settings,(395,350,260,66),'QUALITY: HIGH')
button('QualityEpicButton',settings,(675,350,260,66),'QUALITY: EPIC')
button('FullscreenButton',settings,(115,446,400,66),'TOGGLE FULLSCREEN')
button('SettingsBackButton',settings,(115,660,260,66),'BACK',True)
text('SettingsStatus',settings,(115,550,1200,58),'',22,MINT,variable=True)
text('CreditsTitle',credits,(110,135,1100,90),'MESS / CONTROL',58)
text('CreditsBody',credits,(115,260,1150,250),'Кооперативная игра про маленькие зубы и большой беспорядок во рту.\n\nСоздано на Unreal Engine.\nUnreal Engine — товарный знак Epic Games, Inc.',26)
button('CreditsBackButton',credits,(115,660,260,66),'BACK',True)
text('ExitTitle',exit_page,(110,235,1200,100),'ВЫЙТИ ИЗ ИГРЫ?',58)
button('ExitCancelButton',exit_page,(115,410,320,76),'BACK',True)
button('ExitConfirmButton',exit_page,(460,410,320,76),'EXIT GAME')
text('LaunchTitle',launch,(40,25,1100,65),'PLAY',40,variable=True)
button('LaunchBackButton',launch,(1320,28,235,58),'BACK')
text('LaunchInfo',launch,(55,99,1490,44),'Создай комнату или подключись к друзьям.',21,MUTED)
host=add('Overlay','LegacyMenuHost',launch,(400,150,800,700),variable=True)

call(UMG,'CompileWidgetBlueprint',widgetBlueprint=bp)
bindings=[]
for name in buttons:
    assert call(UMG,'BindToEventProperty',widgetBlueprint=bp,eventName='OnClicked',propertyName=name,propertyClass=ref('/Script/UMG.Button'))
    bindings.append(name)
graph=call(BP,'get_graph',blueprint=bp,graph_name='EventGraph')
read=call(BP,'read_graph_dsl',graph=graph)
(OUT/'widget_tree.json').write_text(json.dumps(call(UMG,'GetWidgets',widgetBlueprint=bp),indent=2),encoding='utf8')
(OUT/'widget_click_events.dsl').write_text(read,encoding='utf8')
(OUT/'widget_names.json').write_text(json.dumps({'widgets':widgets,'buttons':buttons},indent=2),encoding='utf8')
print(json.dumps({'buttons':len(buttons),'widgets':len(widgets),'event_dsl':str(OUT/'widget_click_events.dsl')},indent=2))
