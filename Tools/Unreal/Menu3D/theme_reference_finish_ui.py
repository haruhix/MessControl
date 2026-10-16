"""Apply the reference's burgundy/cream UI palette without changing menu logic."""
import hashlib
import json
from pathlib import Path
from menu_native_client import call,props,task_asset,UMG,BP,ASSET,BASE

root=Path(__file__).resolve().parents[3]
tree=call(UMG,'GetWidgets',widgetBlueprint=task_asset('WBP_MainMenu3D'))
items={item['widgetName']:item for item in tree['widgets']}
def linear(value):
    v=int(value,16)/255.
    return v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4
def color(hex_value,alpha):return dict(zip(('r','g','b','a'),[linear(hex_value[i:i+2]) for i in (0,2,4)]+[alpha]))
changes=[]
for name in ('DaysHeader','DaysFooter','MainShade','SettingsShade','CreditsShade','ExitShade','LaunchShade'):
    widget=items[name]['widget']
    # ObjectTools schema must be checked before each property write.
    schema=json.loads(call('editor_toolset.toolsets.object.ObjectTools','list_properties',instance=widget))
    assert 'colorAndOpacity' in schema
    old=json.loads(call('editor_toolset.toolsets.object.ObjectTools','get_properties',instance=widget,properties=['colorAndOpacity']))
    alpha=old['colorAndOpacity']['a']
    target=color('1C080E' if name.startswith('Days') else '10060B',alpha)
    props(widget,{'colorAndOpacity':target})
    changes.append({'widget':name,'before':old['colorAndOpacity'],'after':target})
call(BP,'compile_blueprint',blueprint=task_asset('WBP_MainMenu3D'),warnings_as_errors=True)
call(ASSET,'save_assets',asset_paths=[BASE+'/WBP_MainMenu3D'])
out=root/'Tools/Unreal/Menu3D/Evidence/v08_ui_palette_report.json'
out.write_text(json.dumps({'status':'SAVED_PALETTE_PENDING_NATIVE_LOOK_REVIEW','changes':changes,
    'graph_or_input_changes':False,'widget_sha256':hashlib.sha256((root/'Content/UI/MainMenu3D/WBP_MainMenu3D.uasset').read_bytes()).hexdigest()},indent=2),encoding='utf8')
print(json.dumps({'status':'saved','widgets':len(changes)}))
