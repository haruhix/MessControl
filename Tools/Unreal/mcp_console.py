"""Issue an editor console command through Epic's native Slate MCP toolset."""
import json,re,sys
from native_mcp_client import NativeMCP
c=NativeMCP(); ts='SlateInspectorToolset.SlateInspectorToolset'
def call(name,args):
    r=c.tool('call_tool',dict(toolset_name=ts,tool_name=name,arguments=args))
    if r.get('isError'): raise RuntimeError(r)
    return json.loads(r['content'][0]['text'])['returnValue']
call('Observe',dict(ref='',maxDepth=35))
snapshot=call('Snapshot',dict(ref='',maxDepth=35,bIncludeSourceLocations=False))
match=re.search(r'text "Cmd".*?textbox[^\n]*\[ref=(tb\d+)\]',snapshot,re.S)
if not match: raise RuntimeError('Editor console is not visible; inspect Slate before continuing')
ref=match.group(1)
call('Click',dict(ref=ref,button='left',doubleClick=False,modifiers=dict(bShift=False,bCtrl=False,bAlt=False,bCmd=False)))
call('PressKey',dict(key='Ctrl+A'))
if not call('Type',dict(ref=ref,text=sys.argv[1],submit=True)): raise RuntimeError('Console input was rejected')
print('Native MCP console command submitted.')
