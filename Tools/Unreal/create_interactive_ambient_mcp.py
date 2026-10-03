"""Author the interactive ambient emitter through Epic's native Niagara MCP.

The original lightweight asset is preserved. Run with the editor's MCP server on.
"""
import json
from pathlib import Path
from native_mcp_client import NativeMCP

ROOT = '/Game/Gameplay/VFX/Ambient'
PATH = ROOT + '/NS_AmbientInteractive'
SYSTEM = {'refPath': PATH + '.NS_AmbientInteractive'}
S = 'NiagaraToolsets.NiagaraToolset_System'
A = 'editor_toolset.toolsets.asset.AssetTools'
client = NativeMCP()

def call(ts, name, **args):
    result = client.tool('call_tool', dict(toolset_name=ts, tool_name=name, arguments=args))
    if result.get('isError'):
        raise RuntimeError(result)
    value = json.loads(result['content'][0]['text'])
    if value.get('error'):
        raise RuntimeError(value)
    return value.get('returnValue')

def ref(path):
    return {'refPath': path}

def stack(script='', module='', inputs=None, renderer=-1):
    return dict(system=SYSTEM, emitterName='InteractiveMotes', scriptName=script,
                moduleName=module, rendererIndex=renderer, inputNameStack=inputs or [])

def value(struct, data):
    return dict(struct=ref(struct), value=data)

def hlsl(script, module, input_name, expression):
    return call(S, 'SetStackInputData', stackInputRef=stack(script,module,[input_name]),
        inputData=value('/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression',
                        dict(hlslExpression=expression)))

def link(script, module, input_name, name, type_path):
    return call(S, 'SetStackInputData', stackInputRef=stack(script,module,[input_name]),
        inputData=value('/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked',
            dict(linkedVariable=dict(name=name,type=dict(classStructOrEnum=ref(type_path))))))

if call('EditorToolset.EditorAppToolset','IsPIERunning'):
    call('EditorToolset.EditorAppToolset','StopPIE')
if not call(A,'exists',path=PATH):
    call(S,'CreateNiagaraSystem',assetName='NS_AmbientInteractive',assetPath=ROOT,
        templateSystem=ref(ROOT+'/NS_AmbientParticles.NS_AmbientParticles'))
summary = call(S,'GetSystemSummary',system=SYSTEM)
if not any(e['emitterName']=='InteractiveMotes' for e in summary['emitters']):
    for emitter in summary['emitters']:
        call(S,'RemoveEmitter',emitterToRemove=dict(stack(),emitterName=emitter['emitterName']))
    call(S,'AddEmitter',system=SYSTEM,emitterName='InteractiveMotes',
        templateEmitter=ref('/Niagara/DefaultAssets/Templates/Emitters/HangingParticulates.HangingParticulates'))

variables=[]
def variable(name, type_path, default, description):
    variables.append(dict(name='User.'+name,description=description,
        type=dict(classStructOrEnum=ref(type_path)),defaultValue=value(type_path,default)))

VEC='/Script/CoreUObject.Vector3f'
FLOAT='/Script/Niagara.NiagaraFloat'
variable('AirVelocity',VEC,dict(x=4,y=-2,z=5),'Runtime air velocity in emitter space.')
variable('ReactionRadius',FLOAT,dict(value=260),'Radius around moving players in centimetres.')
variable('RunPushSpeed',FLOAT,dict(value=95),'Outward air speed from a running player.')
for i in range(8):
    variable('Player%dPosition'%i,VEC,dict(x=0,y=0,z=0),'Runtime player interaction position.')
    variable('Player%dVelocity'%i,VEC,dict(x=0,y=0,z=0),'Runtime smoothed movement velocity.')
    variable('Player%dStrength'%i,FLOAT,dict(value=0),'Runtime movement strength; zero disables this slot.')
call(S,'AddUserVariables',system=SYSTEM,variablesToAdd=variables)

emitter_data = json.loads(call(S,'GetEmitterData',emitterRef=stack())['propertyValues'])
emitter_data.update(bLocalSpace=True,SimTarget='GPUComputeSim',CalculateBoundsMode='Fixed',
    FixedBounds=dict(min=dict(x=-5000,y=-5000,z=-2500),max=dict(x=5000,y=5000,z=2500),isValid=True))
call(S,'SetEmitterData',emitter=stack(),emitterData=dict(propertyValues=json.dumps(emitter_data)))
link('EmitterUpdateScript','SpawnRate','SpawnRate','User.SpawnRate',FLOAT)
hlsl('ParticleSpawnScript','InitializeParticle','Lifetime Min','20.0')
hlsl('ParticleSpawnScript','InitializeParticle','Lifetime Max','30.0')
link('ParticleSpawnScript','InitializeParticle','Color','User.ParticleColor','/Script/CoreUObject.LinearColor')
hlsl('ParticleSpawnScript','InitializeParticle','Uniform Sprite Size Min','User.ParticleSize.x * 0.75')
hlsl('ParticleSpawnScript','InitializeParticle','Uniform Sprite Size Max','User.ParticleSize.x * 1.25')
link('ParticleSpawnScript','ShapeLocation','Box Size','User.VolumeSize',VEC)
call(S,'SetModuleEnabled',moduleRef=stack('ParticleUpdateScript','ScaleSpriteSize'),bEnabled=False)
hlsl('ParticleUpdateScript','ScaleColor','Scale Alpha',
     'smoothstep(0.0,0.12,Particles.NormalizedAge)*(1.0-smoothstep(0.78,1.0,Particles.NormalizedAge))')

# Wind is evaluated at every particle every frame. The ordinary Niagara solver
# retains particle velocity so characters displace the cloud without restarting it.
terms=['User.AirVelocity']
for i in range(8):
    delta='(Particles.Position-User.Player%dPosition)'%i
    weight='(User.Player%dStrength * pow(saturate(1.0-length(%s)/max(User.ReactionRadius,1.0)),2.0))'%(i,delta)
    push='(%s/max(length(%s),1.0)*User.RunPushSpeed+User.Player%dVelocity*0.20)'%(delta,delta,i)
    terms.append('(%s*%s)'%(weight,push))
hlsl('ParticleUpdateScript','WindForce','Wind Speed','+'.join(terms))
hlsl('ParticleUpdateScript','WindForce','Wind Speed Scale','1.0')
hlsl('ParticleUpdateScript','AerodynamicDrag','Aerodynamic Drag','2.5')
renderer = call(S,'GetRendererData',rendererRef=stack(renderer=0))
renderer_data = json.loads(renderer['propertyValues'])
renderer_data['Material']=ROOT+'/M_AmbientParticle.M_AmbientParticle'
call(S,'SetRendererData',renderer=stack(renderer=0),rendererData=dict(propertyValues=json.dumps(renderer_data)))

state = call(S,'GetSystemCompileState',system=SYSTEM)
Path('Saved/Tests/InteractiveAmbientCompile.json').write_text(json.dumps(state,indent=2),encoding='utf-8')
print('MC_INTERACTIVE_AMBIENT_COMPILE',json.dumps(state))
assert not state['bHasErrors'] and not state['bIsStale'] and not state['bIsCompiling'], state
call(A,'save_assets',asset_paths=[PATH])
print('MC_INTERACTIVE_AMBIENT_MCP_SAVED '+PATH)
