"""Small final WBP input/font repair; no level/PIE changes."""
from pathlib import Path
import json
from menu_native_client import call,props,task_asset,BP,UMG,ASSET,BASE
OUT=Path(__file__).resolve().parent/'Evidence'
w=task_asset('WBP_MainMenu3D')
items={x['widgetName']:x for x in call(UMG,'GetWidgets',widgetBlueprint=w)['widgets']}
for i in (1,2):props(items[f'Day{i}CollapseButton_Text']['widget'],{'font':{'size':16}})
code=(OUT/'accordion_OnPreviewKeyDown.dsl').read_text(encoding='utf8')
old='(elif (and (== (Variables|Default|GetPageIndex) 1) (Utilities|String|EqualExactly(String) name "Enter"))'
blocked='false'
for button in ['DaysBackButton']+[f'Day{i}CollapseButton' for i in range(1,4)]:
    blocked=f'(or (Widget|HasKeyboardFocus :self (Variables|WBP_MainMenu3D|Get{button})) {blocked})'
if old in code:
    code=code.replace(old,f'(elif (and (not {blocked}) (and (== (Variables|Default|GetPageIndex) 1) (Utilities|String|EqualExactly(String) name "Enter")))')
    (OUT/'accordion_OnPreviewKeyDown.dsl').write_text(code,encoding='utf8')
    graph=call(BP,'get_graph',blueprint=w,graph_name='OnPreviewKeyDown')
    call(BP,'write_graph_dsl',graph=graph,code=code)
call(BP,'compile_blueprint',blueprint=w,warnings_as_errors=True)
call(ASSET,'save_assets',asset_paths=[BASE+'/WBP_MainMenu3D'])
path=OUT/'three_mouth_accordion_compile_report.json'
report=json.loads(path.read_text());report['focused_back_enter']='Native Back button keeps its Enter action; selected-day Enter never steals it.'
report['compact_back_font']=16
path.write_text(json.dumps(report,indent=2),encoding='utf8')
print('FINAL_ACCORDION_COMPILED_SAVED',flush=True)
