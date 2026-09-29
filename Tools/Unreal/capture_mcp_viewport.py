"""Capture Epic's native MCP viewport without printing image payloads."""
import base64, json, sys
from pathlib import Path
from native_mcp_client import NativeMCP
c = NativeMCP()
r = c.tool('call_tool',dict(toolset_name='EditorToolset.EditorAppToolset',tool_name='GetCameraTransform',arguments={}))
transform=json.loads(r['content'][0]['text'])['returnValue']
r = c.tool('call_tool',dict(toolset_name='EditorToolset.EditorAppToolset',tool_name='CaptureViewport',arguments=dict(captureTransform=transform,bShowUI=False,annotations=dict(gridSpacing=0,gridExtent=0,gridHeight=0,maxLabelDistance=0,classFilter={'refPath':'/Script/Engine.Actor'},maxLabels=0))))
if r.get('isError'): raise RuntimeError(r)
v = json.loads(r['content'][0]['text'])['returnValue']
path = Path(sys.argv[1]); path.parent.mkdir(parents=True,exist_ok=True)
path.write_bytes(base64.b64decode(v['image']['data']))
print(path)
