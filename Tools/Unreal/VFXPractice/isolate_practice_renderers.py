"""Redirect study Niagara renderer resources only; production assets stay read-only."""
import json,sys
from pathlib import Path
P=Path('E:/DEVGAME/MessControl');OUT=Path('E:/CodexWork/MessControl/Validation/VFXPracticeSandbox_20261030')
sys.path.insert(0,str(P/'Tools/Unreal'))
from native_mcp_client import NativeMCP
client=NativeMCP();TS='NiagaraToolsets.NiagaraToolset_System';trace=[]
def call(name,**args):
    result=client.tool('call_tool',dict(toolset_name=TS,tool_name=name,arguments=args))
    assert not result.get('isError'),result
    data=json.loads(next(x['text'] for x in result['content'] if x['type']=='text'));assert not data.get('error'),data
    value=data.get('returnValue');trace.append(dict(tool=name,arguments=args,result=value))
    (OUT/'RendererIsolationTrace.json').write_text(json.dumps(trace,ensure_ascii=False,indent=2),encoding='utf8')
    return value
manifest=json.loads((OUT/'MapAuthoring.json').read_text(encoding='utf8'))
copies=manifest['source_copies']
systems=sorted({row['system'] for row in manifest['rows']})
def rewrite(value):
    if isinstance(value,dict):
        if 'refPath' in value:
            old=value['refPath'];package=old.split('.',1)[0]
            if package in copies:
                target=copies[package];value['refPath']=target+'.'+target.rsplit('/',1)[-1]
            elif old.startswith('/Game/') and not old.startswith('/Game/Tests/VFXPractice/'):
                unhandled.add(old)
        for item in value.values():rewrite(item)
    elif isinstance(value,list):
        for item in value:rewrite(item)
unhandled=set();edits=[];summaries={}
for system in systems:
    summary=call('GetSystemSummary',system=dict(refPath=system));summaries[system]=summary
    for emitter in summary['emitters']:
        for index,_ in enumerate(emitter['rendererClasses']):
            ref=dict(system=dict(refPath=system),emitterName=emitter['emitterName'],scriptName='',moduleName='',rendererIndex=index,inputNameStack=[])
            data=call('GetRendererData',rendererRef=ref)
            properties=json.loads(data['propertyValues']);before=json.dumps(properties,sort_keys=True)
            rewrite(properties)
            if json.dumps(properties,sort_keys=True)!=before:
                call('SetRendererData',renderer=ref,rendererData=dict(propertyValues=json.dumps(properties)))
                edits.append(dict(system=system,emitter=emitter['emitterName'],renderer=index))
proof=dict(edits=edits,unhandled_project_resources=sorted(unhandled),summaries=summaries)
(OUT/'RendererIsolation.json').write_text(json.dumps(proof,ensure_ascii=False,indent=2),encoding='utf8')
assert not unhandled,'Copy remaining renderer resources before readiness: '+str(unhandled)
for system in systems:
    state=call('GetSystemCompileState',system=dict(refPath=system))
    assert not state['bHasErrors'] and not state['bIsCompiling'],state
print('PRACTICE_RENDERERS_ISOLATED',len(edits))
