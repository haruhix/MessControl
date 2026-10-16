"""Replace the closed-only DAY03 enlargement with measured uniform XYZ fitting.

Run with PIE stopped. Only UpdateStage is authored; no mesh, map or input edits.
"""
import hashlib
import json
import argparse
import urllib.request
from pathlib import Path
from menu_native_client import call,task_asset,BP,ASSET,BASE

folder=Path(__file__).resolve().parent
parser=argparse.ArgumentParser();parser.add_argument('--finalize-written',action='store_true');args=parser.parse_args()
root=folder.parents[2]
source=folder/'Evidence/accordion_UpdateStage.dsl'
old='(* 0.54 (- 1.12676056338 (* open3 0.12676056338)))'
new='(* 0.54 (+ 0.881020618494 (* open3 0.058839521506)))'
code=source.read_text(encoding='utf8')
assert code.count(old)==1
code=code.replace(old,new,1)
pc=root/'Content/UI/MainMenu3D/BP_Menu3DPlayerController.uasset'
pc_hash=hashlib.sha256(pc.read_bytes()).hexdigest()
widget=task_asset('WBP_MainMenu3D')
graph=call(BP,'get_graph',blueprint=widget,graph_name='UpdateStage')
if not args.finalize_written:
    # The large existing graph needs longer than the shared client's60s limit.
    from menu_native_client import CLIENT
    CLIENT.sequence+=1
    body={'jsonrpc':'2.0','id':CLIENT.sequence,'method':'tools/call','params':{'name':'call_tool','arguments':{
        'toolset_name':BP,'tool_name':'write_graph_dsl','arguments':{'graph':graph,'code':code}}}}
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
    assert not result['result'].get('isError'),result
call(BP,'compile_blueprint',blueprint=widget,warnings_as_errors=True)
assert hashlib.sha256(pc.read_bytes()).hexdigest()==pc_hash
call(ASSET,'save_assets',asset_paths=[BASE+'/WBP_MainMenu3D'])
source.write_text(code,encoding='utf8')
author=folder/'implement_three_mouth_accordion.py'
text=author.read_text(encoding='utf8')
assert old in text
author.write_text(text.replace(old,new).replace("'.54*lerp(1.12676056338,1,Open3)'","'.54*lerp(.881020618494,.939860140,Open3)'"),encoding='utf8')
asset=root/'Content/UI/MainMenu3D/WBP_MainMenu3D.uasset'
report={'status':'UNIFORM_WIDTH_FIT_COMPILED_SAVED_PENDING_NATIVE_SCREENSHOTS',
    'only_authored_graph':'UpdateStage','compiled_warnings_as_errors':True,
    'scale_xyz':'.54*lerp(.881020618494,.939860140,Open3)',
    'basis':'Run04 measured crown arc widths: DAY01 672px, DAY03 Closed845px/Open715px; remove prior1.12676056338 Closed enlargement',
    'teeth_geometry_stretched_or_modified':False,
    'pending':'Native projected-width check and separate lateral gum geometry repair',
    'unchanged_pc_sha256':pc_hash,'saved_widget_sha256':hashlib.sha256(asset.read_bytes()).hexdigest()}
(folder/'Evidence/vampire_width_compile_report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report,indent=2),flush=True)
