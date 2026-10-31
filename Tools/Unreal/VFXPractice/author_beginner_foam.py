"""Create a separate first-lesson Niagara asset with stock modules and no inline HLSL.

Run externally while Editor is idle. Existing gameplay/study systems are read-only.
The root separately attaches this new asset to the clean Free Play station.
"""
import json,sys
from pathlib import Path
P=Path('E:/DEVGAME/MessControl')
R=Path('E:/CodexWork/MessControl/Validation/VFXBeginnerControls_20261030')
sys.path.insert(0,str(P/'Tools/Unreal'))
from native_mcp_client import NativeMCP
C=NativeMCP();TS='NiagaraToolsets.NiagaraToolset_System'
TARGET='/Game/Tests/VFXPractice/Effects/NS_Practice_FoamBasics'
SOURCE='/Game/Tests/VFXPractice/Effects/NS_Practice_Trail'
trace=[]
def obj(path):return dict(refPath=path+'.'+path.rsplit('/',1)[-1])
def call(name,**arguments):
    result=C.tool('call_tool',dict(toolset_name=TS,tool_name=name,arguments=arguments))
    assert not result.get('isError'),result
    raw=next(c['text'] for c in result['content'] if c['type']=='text')
    data=json.loads(raw);assert not data.get('error'),data
    value=data.get('returnValue');trace.append(dict(tool=name,arguments=arguments,result=value))
    (R/'AuthorTrace.json').write_text(json.dumps(trace,ensure_ascii=False,indent=2),encoding='utf8')
    return value
def ref(script='',module='',name=None,renderer=-1):
    return dict(system=obj(TARGET),emitterName='SimpleSpriteBurst',scriptName=script,moduleName=module,
        rendererIndex=renderer,inputNameStack=[name] if name else [])
def put(script,module,name,kind,value):
    return call('SetStackInputData',stackInputRef=ref(script,module,name),inputData=dict(struct=dict(refPath=kind),value=value))
def number(script,module,name,value):return put(script,module,name,'/Script/Niagara.NiagaraFloat',dict(value=value))
def main():
    marker=R/'Created.json'
    if marker.exists():
        assert json.loads(marker.read_text())==dict(target=TARGET,source=SOURCE),'Resume only this owned new asset'
    else:
        assert not (P/'Content/Tests/VFXPractice/Effects/NS_Practice_FoamBasics.uasset').exists(),'Do not overwrite a learner asset'
        call('CreateNiagaraSystem',assetName=TARGET.rsplit('/',1)[-1],assetPath=TARGET.rsplit('/',1)[0],templateSystem=obj(SOURCE))
        marker.write_text(json.dumps(dict(target=TARGET,source=SOURCE)),encoding='utf8')
    top=call('GetEmitterTopology',emitterRef=ref())
    for script,key in [('EmitterUpdateScript','emitterUpdateScript'),('ParticleSpawnScript','particleSpawnScript'),('ParticleUpdateScript','particleUpdateScript')]:
        for module in top[key]['modules']:
            if module['bIsSetParametersModule'] or module['moduleName'].startswith('SpawnPerUnit'):
                call('RemoveModule',moduleToRemove=ref(script,module['moduleName']))
    existing=[m for m in top['emitterUpdateScript']['modules'] if m['moduleName'].startswith('SpawnRate')]
    spawn=existing[0] if existing else call('AddModule',moduleLocationRef=ref('EmitterUpdateScript'),moduleAsset=dict(refPath='/Niagara/Modules/Emitter/SpawnRate.SpawnRate'))
    number('EmitterUpdateScript',spawn['moduleName'],'SpawnRate',10.)
    init='InitializeParticle';stage='ParticleSpawnScript'
    number(stage,init,'Lifetime',1.6)
    put(stage,init,'Color','/Script/CoreUObject.LinearColor',dict(r=.75,g=1.,b=.9,a=.55))
    put(stage,init,'Position Offset','/Script/CoreUObject.Vector3f',dict(x=0.,y=0.,z=40.))
    for field,value in [('Uniform Sprite Size Min',35.),('Uniform Sprite Size Max',65.)]:number(stage,init,field,value)
    shapes=[m for m in top['particleSpawnScript']['modules'] if m['moduleName'].startswith('ShapeLocation')]
    shape=shapes[0] if shapes else call('AddModule',moduleLocationRef=ref(stage),moduleAsset=dict(refPath='/Niagara/Modules/Spawn/Location/V2/ShapeLocation.ShapeLocation'))
    number(stage,shape['moduleName'],'Sphere Radius',22.)
    put(stage,shape['moduleName'],'Shape Origin','/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked',
        dict(linkedVariable=dict(name='Particles.Position',type=dict(classStructOrEnum=dict(refPath='/Script/Niagara.NiagaraPosition')))))
    # Built-in curve input opens an ordinary Niagara curve editor, not an expression.
    put('ParticleUpdateScript','ScaleColor','Scale Alpha','/Script/NiagaraEditor.NiagaraExt_StackInputData_DynamicInput',
        dict(dynamicInputAsset=dict(refPath='/Niagara/DynamicInputs/ValueFromCurve/FloatFromCurve.FloatFromCurve')))
    curve_ref=ref('ParticleUpdateScript','ScaleColor');curve_ref['inputNameStack']=['Scale Alpha','FloatCurve']
    curve=call('GetStackInputData',stackInputRef=curve_ref)
    data=json.loads(curve['value']['propertyValues']);key=data['Curve']['keys'][0]
    data['Curve']['keys']=[dict(key,interpMode='RCIM_Cubic',time=t,value=v,arriveTangent=0,leaveTangent=0) for t,v in [(0,0),(.12,1),(.45,.85),(1,0)]]
    call('SetStackInputData',stackInputRef=curve_ref,inputData=dict(struct=dict(refPath='/Script/NiagaraEditor.NiagaraExt_StackInputData_DataInterface'),value=dict(propertyValues=json.dumps(data))))
    renderer=json.loads(call('GetRendererData',rendererRef=ref(renderer=0))['propertyValues'])
    renderer.update(FacingMode='FaceCamera',Alignment='Unaligned',bCastShadows=False)
    call('SetRendererData',renderer=ref(renderer=0),rendererData=dict(propertyValues=json.dumps(renderer)))
    summary=call('GetSystemSummary',system=obj(TARGET))
    if summary['userVariables']:call('RemoveUserVariables',system=obj(TARGET),variablesToRemove=[dict(name=v['name'],type=v['type']) for v in summary['userVariables']])
    modules=call('GetEmitterInputValues',emitterRef=ref())
    assert 'NiagaraExt_StackInputData_HlslExpression' not in json.dumps(modules),'Beginner example still has an inline expression'
    (R/'ModuleReadback.json').write_text(json.dumps(modules,ensure_ascii=False,indent=2),encoding='utf8')
    compile_state=call('GetSystemCompileState',system=obj(TARGET))
    assert not compile_state['bHasErrors'] and not compile_state['bIsCompiling'],compile_state
    (R/'AuthorResult.json').write_text(json.dumps(dict(target=TARGET,source=SOURCE,compile=compile_state,
        contract='Stationary foam study. No gameplay cleaning, no ground alignment/trail contract. Literal stock module fields; zero inline HLSL; lifetime controlled by ParticleState.',
        authored_inline_hlsl=0,simulation='CPU world-space sprite',nominal_population=16,modules=modules),ensure_ascii=False,indent=2),encoding='utf8')
    print('BEGINNER_FOAM_AUTHORED',TARGET)
if __name__=='__main__':main()
