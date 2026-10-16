"""Record native Slate frames into a local review artifact; no desktop scraping."""
import argparse
import base64
import json
from pathlib import Path
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from native_mcp_client import NativeMCP

p=argparse.ArgumentParser()
p.add_argument('--out',required=True)
p.add_argument('--ref',default='')
p.add_argument('--seconds',type=float,default=0.)
p.add_argument('--fps',type=float,default=8.)
opt=p.parse_args()
out=Path(opt.out).resolve()
allowed=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review').resolve()
assert out.is_relative_to(allowed)
out.mkdir(parents=True,exist_ok=True)
client=NativeMCP()

def tool(name,args):
    result=client.tool('call_tool',{'toolset_name':'SlateInspectorToolset.SlateInspectorToolset','tool_name':name,'arguments':args})
    if result.get('isError'):raise RuntimeError(result)
    value=json.loads(result['content'][0]['text'])
    return value['returnValue']

started=time.monotonic()
frames=[]
while True:
    now=time.monotonic()-started
    item=tool('Screenshot',{'ref':opt.ref})
    assert item['mimeType']=='image/png'
    path=out/f'frame_{len(frames):04d}.png'
    path.write_bytes(base64.b64decode(item['data']))
    frames.append({'file':path.name,'time':now})
    if now>=opt.seconds:break
    remaining=len(frames)/opt.fps-(time.monotonic()-started)
    if remaining>0:time.sleep(min(remaining,1.))
(out/'capture_receipt.json').write_text(json.dumps({'source':'Actual Unreal Slate Screenshot tool, real running game viewport',
    'started_monotonic':started,'requested_fps':opt.fps,'frames':frames},indent=2),encoding='utf8')
print(json.dumps({'frames':len(frames),'elapsed':round(time.monotonic()-started,2),'out':str(out)}))
