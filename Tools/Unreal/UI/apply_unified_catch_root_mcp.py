"""Append the five new saved preview pages and overlays via the native Unreal MCP."""
import json,os,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True
project=Path('E:/DEVGAME/MessControl')
temporary=Path('E:/CodexTemp/MessControl-UIHubReward');temporary.mkdir(parents=True,exist_ok=True)
os.environ['TEMP']=os.environ['TMP']=str(temporary)
sys.path.insert(0,str(project/'Tools/Unreal'))
from native_mcp_client import NativeMCP
preflight=temporary/'PreflightCatchRoot.py'
preflight.write_text("import unreal as u\nassert not u.EditorLevelLibrary.get_pie_worlds(True)\nassert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()\nassert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()\nassert '-MCUIFoundationOwned' in u.SystemLibrary.get_command_line()\n",encoding='utf-8')
subprocess.run([sys.executable,str(project/'Tools/Unreal/UI/run_owned_ui_recipe.py'),str(preflight)],check=True,capture_output=True,text=True,encoding='utf-8')
recipe=project/'Tools/Unreal/UI/unified_catch_umg_recipe.json'
r=json.loads(recipe.read_text(encoding='utf-8'))
result=NativeMCP().tool('call_tool',{'toolset_name':r['toolset'],'tool_name':r['tool'],'arguments':{'script':r['script']}})
out=Path('E:/CodexWork/MessControl/Validation/UICatch_20261104/RootAuthorReport.json')
out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
assert not result.get('isError'),result
payload=json.loads(result['content'][0]['text'])
print(json.dumps(payload,ensure_ascii=False))
assert 'error' not in payload,str(payload)
