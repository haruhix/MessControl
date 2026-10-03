"""Connect the saved ambient Blueprint and placed actors through Unreal MCP."""
import json
from native_mcp_client import NativeMCP

client=NativeMCP()
B='editor_toolset.toolsets.blueprint.BlueprintTools'
O='editor_toolset.toolsets.object.ObjectTools'
N='NiagaraToolsets.NiagaraToolset_Component'
E='EditorToolset.EditorAppToolset'
S='editor_toolset.toolsets.scene.SceneTools'
A='editor_toolset.toolsets.asset.AssetTools'
ROOT='/Game/Gameplay/VFX/Ambient'
bp={'refPath':ROOT+'/BP_AmbientParticles.BP_AmbientParticles'}
system={'refPath':ROOT+'/NS_AmbientInteractive.NS_AmbientInteractive'}

def call(ts,tool_name,**args):
    result=client.tool('call_tool',dict(toolset_name=ts,tool_name=tool_name,arguments=args))
    if result.get('isError'):raise RuntimeError(result)
    value=json.loads(result['content'][0]['text'])
    if value.get('error'):raise RuntimeError(value)
    return value.get('returnValue')

def set_system(component):
    call(O,'list_properties',instance=component)
    current=json.loads(call(O,'get_properties',instance=component,properties=['Asset']))['Asset']
    if current!=system:
        call(N,'SetSystem',niagaraComponent=component,system=system,bResetExistingOverrideParameters=False)

assert not call(E,'IsPIERunning'),'Stop Play before reconnecting the saved Blueprint.'
parent={'refPath':'/Script/MessControl.MCAmbientParticles'}
if call(B,'get_parent',blueprint=bp)!=parent:
    call(B,'set_parent',blueprint=bp,parent_class=parent)
compiled=call(B,'compile_blueprint',blueprint=bp,warnings_as_errors=True)
print('MC_AMBIENT_BLUEPRINT_COMPILE',json.dumps(compiled))
defaults=call(B,'get_default_object',blueprint=bp)
call(O,'list_properties',instance=defaults)
properties=json.loads(call(O,'get_properties',instance=defaults,properties=['NiagaraComponent']))
set_system(properties['NiagaraComponent'])
call(B,'compile_blueprint',blueprint=bp,warnings_as_errors=True)

actors=call(S,'find_actors',name='',tag='',collision_channels=[],
    actor_type={'refPath':ROOT+'/BP_AmbientParticles.BP_AmbientParticles_C'})
for actor in actors:
    call(O,'list_properties',instance=actor)
    props=json.loads(call(O,'get_properties',instance=actor,properties=['NiagaraComponent']))
    set_system(props['NiagaraComponent'])
call(A,'save_assets',asset_paths=[ROOT+'/BP_AmbientParticles','/Game/Maps/L_Mouth'])
call(E,'SelectActors',actors=actors)
print('MC_INTERACTIVE_AMBIENT_CONNECTED',json.dumps(actors))
