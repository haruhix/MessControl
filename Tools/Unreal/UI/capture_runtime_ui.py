"""Capture the active standalone PIE window through the native Slate MCP."""
import base64,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from native_mcp_client import NativeMCP
client=NativeMCP()
result=client.tool('call_tool',{'tool_name':'Screenshot','toolset_name':'SlateInspectorToolset.SlateInspectorToolset','arguments':{'ref':sys.argv[1]}})
def pixels(value):
    if isinstance(value,dict):
        if (value.get('mimeType','').startswith('image/') or value.get('type')=='image') and isinstance(value.get('data'),str): return value['data']
        for entry in value.values():
            found=pixels(entry)
            if found:return found
    if isinstance(value,list):
        for entry in value:
            found=pixels(entry)
            if found:return found
    if isinstance(value,str) and value.startswith('{'):
        try:return pixels(json.loads(value))
        except (ValueError,TypeError):pass
    return None
encoded=pixels(result)
assert encoded,'No native screenshot pixels'
output=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else Path('E:/DEVGAME/MessControl/Saved/GameplayUI/RuntimeHUD_Solo.png')
assert output.drive.upper()=='E:' and output.suffix.lower()=='.png'
output.parent.mkdir(parents=True,exist_ok=True)
output.write_bytes(base64.b64decode(encoded))
print(str(output))
