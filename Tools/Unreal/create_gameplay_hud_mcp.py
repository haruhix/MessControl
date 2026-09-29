"""Create an editable UMG HUD through Epic's native MCP. Existing artist edits are preserved."""
import json
from pathlib import Path
from native_mcp_client import NativeMCP

client = NativeMCP()
UMG = 'UMGToolSet.UMGToolSet'
OBJ = 'editor_toolset.toolsets.object.ObjectTools'
ASSET = 'editor_toolset.toolsets.asset.AssetTools'
PATH = '/Game/Gameplay/UI/WBP_GameplayHUD'

def call(ts, name, **args):
    result = client.tool('call_tool', dict(toolset_name=ts, tool_name=name, arguments=args))
    if result.get('isError'):
        raise RuntimeError(result)
    value = json.loads(result['content'][0]['text'])
    if value.get('error'):
        raise RuntimeError(value)
    return value.get('returnValue')

def ref(path): return dict(refPath=path)

if call('EditorToolset.EditorAppToolset', 'IsPIERunning'):
    raise RuntimeError('Stop PIE before editing the widget blueprint.')
if call(ASSET, 'exists', path=PATH):
    print('Existing WBP_GameplayHUD preserved. Use UMG Designer for follow-up edits.')
    raise SystemExit(0)

bp = call(UMG, 'CreateWidgetBlueprint', folderPath='/Game/Gameplay/UI', assetName='WBP_GameplayHUD', parentClass=ref('/Script/MessControl.MCGameplayHUD'))
tree = call(UMG, 'GetWidgets', widgetBlueprint=bp)
if tree['widgets']:
    assert not tree['widgets'][0]['bInherited']
    call(UMG, 'RemoveWidget', widgetBlueprint=bp, widget=tree['widgets'][0]['widget'])

audit = {}
def props(obj, values):
    # The toolset requires property discovery before every widget/slot mutation.
    key = obj['refPath']
    if key not in audit:
        audit[key] = call(OBJ, 'list_properties', instance=obj)
    schema=json.loads(audit[key])
    for name in values:
        assert name in schema, (key, name, list(schema))
    before = json.loads(call(OBJ, 'get_properties', instance=obj, properties=list(values)))
    def merge(old, new):
        if isinstance(old,dict) and isinstance(new,dict):
            return dict(old, **{k:merge(old.get(k),v) for k,v in new.items()})
        return new
    if not call(OBJ, 'set_properties', instance=obj, values=json.dumps(merge(before,values), ensure_ascii=False)):
        raise RuntimeError((key, values, before))

def add(kind, name, parent=None, rect=None, values=None, anchor=(0,0)):
    cls = '/Script/MessControl.MCHUDIcon' if kind=='Icon' else '/Script/UMG.'+kind
    args = dict(widgetBlueprint=bp, widgetClass=ref(cls), widgetDisplayName=name)
    if parent: args['parentWidget']=parent
    entry = call(UMG, 'AddWidget', **args)
    widget = entry['widget']
    props(widget, dict(visibility='HitTestInvisible', **(values or {})))
    if rect is not None:
        x,y,w,h=rect
        props(entry['slot'], dict(layoutData=dict(offsets=dict(left=x,top=y,right=w,bottom=h),anchors=dict(minimum=dict(x=anchor[0],y=anchor[1]),maximum=dict(x=anchor[0],y=anchor[1])),alignment=dict(x=0,y=0)),bAutoSize=False))
    return widget

def rgba(c): return dict(zip('rgba', list(c)+[1]*(4-len(c))))
def slate(c): return dict(specifiedColor=rgba(c),colorUseRule='UseColor_Specified')
WHITE=(.98,.97,.93,1)
INK=(.012,.020,.033,.90)
MINT=(.26,.91,.71,1)
AMBER=(1,.64,.23,1)
BLUE=(.22,.65,1,1)
MUTED=(.56,.66,.72,1)
COLORS=[BLUE,(1,.27,.31,1),AMBER,MINT]
def brush(c=(1,1,1,1),radius=12):
    return dict(drawAs='RoundedBox',resourceObject='None',resourceName='None',imageType='NoImage',tintColor=slate(c),outlineSettings=dict(roundingType='FixedRadius',cornerRadii=dict(x=radius,y=radius,z=radius,w=radius)))
def panel(name,parent,rect,anchor=(0,0)):
    return add('CanvasPanel',name,parent,rect,anchor=anchor)
def box(name,parent,rect,color=INK,radius=12):
    return add('Image',name,parent,rect,dict(brush=brush(radius=radius),colorAndOpacity=rgba(color)))
def text(name,parent,rect,value,size=14,color=WHITE):
    return add('TextBlock',name,parent,rect,dict(text=value,font=dict(size=size),colorAndOpacity=slate(color),autoWrapText=False))
def bar(name,parent,rect,color=MINT,value=.5,background=(.004,.008,.013,.95),radius=6):
    return add('ProgressBar',name,parent,rect,dict(percent=value,barFillType='LeftToRight',barFillStyle='Scale',borderPadding=dict(x=0,y=0),fillColorAndOpacity=rgba(color),widgetStyle=dict(backgroundImage=brush(background,radius),fillImage=brush(radius=radius),enableFillAnimation=False)))
def icon(name,parent,rect,slot=0,tooth=False,color=WHITE):
    return add('Icon',name,parent,rect,dict(toolSlot=slot,bTooth=tooth,tint=rgba(color),bIsVolatile=tooth))

scale=add('ScaleBox','ViewportScale',values=dict(stretch='ScaleToFit'))
design=add('SizeBox','DesignSize',scale,values=dict(widthOverride=1536,heightOverride=864,bOverride_WidthOverride=True,bOverride_HeightOverride=True))
root=add('CanvasPanel','HUDRoot',design)
left=panel('ObjectivesPanel',root,(22,20,304,300))
box('DayBG',left,(0,0,304,82)); text('DayTitle',left,(16,4,272,44),'ДЕНЬ 1',32)
text('EventTitle',left,(17,48,270,25),'УБРАТЬ ОСТАТКИ',15,MINT)
for y,prefix,title,color,idx in [(93,'Task','ТЕКУЩАЯ ЗАДАЧА',AMBER,1),(164,'Clean','ЧИСТОТА РТА',BLUE,0),(235,'Health','ЗДОРОВЬЕ ЗУБОВ',MINT,4)]:
    card=panel(prefix+'Card',left,(0,y,304,65)); box(prefix+'BG',card,(0,0,304,65))
    icon(prefix+'Icon',card,(7,7,45,45),idx,idx==4,color)
    text(prefix+'Label',card,(59,9,221,22),title)
    bar(prefix+'Progress',card,(59,37,157,12),color,.7)
    text(prefix+'Value',card,(227,31,75,27),'7/16' if idx==1 else '70%',15)

timer=panel('TimerPanel',root,(-100,20,200,100),(.5,0))
box('TimerBG',timer,(0,0,200,81)); text('TimerValue',timer,(40,4,150,44),'00:28',32)
text('TimerLabel',timer,(17,48,185,25),'ДО СЛЕД. СОБЫТИЯ',11,MUTED)
bar('TimerProgress',timer,(0,86,200,10),AMBER,.45)

players=panel('PlayersPanel',root,(-326,20,304,100),(1,0))
for i,color in enumerate(COLORS):
    n='Player'+str(i+1); card=panel(n,players,(76*i,0,66,100))
    box(n+'Frame',card,(0,0,66,85),color,15); box(n+'BG',card,(3,3,60,64),INK,13)
    icon(n+'Face',card,(4,6,58,58),tooth=True)
    text(n+'Name',card,(22,63,40,24),'P'+str(i+1),13)
    bar(n+'Health',card,(6,89,54,5),color,1,radius=2)

events=panel('EventsPanel',root,(-282,144,260,254),(1,0))
text('EventsLabel',events,(8,0,240,22),'СОБЫТИЯ',12,MUTED)
for i in range(3):
    n='Timeline'+str(i); card=panel(n,events,(0,25+79*i,260,69))
    box(n+'BG',card,(0,0,260,69))
    bar(n+'Fill',card,(0,0,260,69),(.04,.24,.24,.66),[1,.4,0][i],(0,0,0,0),12)
    box(n+'Accent',card,(0,10,3,49),MINT if i==1 else MUTED,1)
    text(n+'Label',card,(15,9,242,17),['ВЫПОЛНЕНО','СЕЙЧАС','ДАЛЕЕ'][i],10,MUTED)
    text(n+'Title',card,(15,31,242,27),['ЗАВТРАК ПАДАЕТ','УБРАТЬ ОСТАТКИ','ГОРЯЧИЙ КОФЕ'][i],14)

inventory=panel('InventoryPanel',root,(22,-278,247,258),(0,1))
box('ToolTitleBG',inventory,(0,0,247,42)); text('ToolTitle',inventory,(15,11,222,25),'ЩЁТКА',16,MINT)
offsets=[(42,145),(103,83),(164,145),(103,207)]
for i,(x,y) in enumerate(offsets):
    n='Tool'+str(i+1); card=panel(n,inventory,(x-35,y-35,70,80))
    box(n+'Frame',card,(0,0,70,70),MINT if i==0 else (.15,.23,.29,.8),35)
    box(n+'BG',card,(3,3,64,64),INK,32)
    icon(n+'Icon',card,(7,5,56,56),i)
    box(n+'KeyBG',card,(25,59,20,20),WHITE,5); text(n+'Key',card,(30,59,18,20),str(i+1),12,INK)
cool=panel('SprayCooldown',inventory,(77,197,52,38))
box('CooldownBG',cool,(0,0,52,26),INK,6); text('CooldownValue',cool,(12,2,45,25),'6.9',15,BLUE)
bar('CooldownProgress',cool,(2,29,48,5),BLUE,.2,radius=2)
props(cool,dict(visibility='Collapsed'))
text('ToolHelp',root,(23,-25,380,23),'1–4  ИНСТРУМЕНТЫ     E  ВЗЯТЬ / ТАЩИТЬ',10,MUTED)
# Bottom anchor for the compact control legend.
legend=call(UMG,'GetWidgets',widgetBlueprint=bp)
for entry in legend['widgets']:
    if entry['widgetName']=='ToolHelp': props(entry['slot'],dict(layoutData=dict(anchors=dict(minimum=dict(x=0,y=1),maximum=dict(x=0,y=1)))))
hint=panel('HintPanel',root,(-225,-73,450,47),(.5,1)); box('HintBG',hint,(0,0,450,47))
text('ActionHint',hint,(20,11,424,29),'ЛКМ · ЧИСТИТЬ',14)
bar('ContactProgress',hint,(20,38,410,5),MINT,0,radius=2)
results=panel('ResultsPanel',root,(-245,0,490,111),(.5,.44))
box('ResultsBG',results,(0,0,490,111)); text('ResultTitle',results,(26,15,448,42),'ДЕНЬ ЗАВЕРШЁН',24,MINT)
text('ResultDetail',results,(26,57,448,29),'Незавершённых событий: 0   ·   R — новый забег',13)
props(results,dict(visibility='Collapsed'))

assert call(UMG,'CompileWidgetBlueprint',widgetBlueprint=bp)
assert call(ASSET,'save_assets',asset_paths=[PATH])
final=call(UMG,'GetWidgets',widgetBlueprint=bp)
Path('Saved/HUDMcpPropertyAudit.json').write_text(json.dumps(audit,ensure_ascii=False),encoding='utf-8')
Path('Artifacts/Inventory/UMGHierarchy.json').write_text(json.dumps(final,ensure_ascii=False,indent=2),encoding='utf-8')
print('MC_UMG_CREATED', final['info'])
