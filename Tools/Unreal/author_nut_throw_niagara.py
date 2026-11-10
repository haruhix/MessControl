"""Author the editable nut-shot burst via the running Unreal Editor's MCP."""

import json
import time

from native_mcp_client import NativeMCP


ROOT = '/Game/Gameplay/VFX/NutCombat'
TARGET = ROOT + '/NS_NutThrow_BossImpact'
DONOR = '/Game/Gameplay/Food/VFX/NS_Food_AutoMouth'
TOOLSET = 'NiagaraToolsets.NiagaraToolset_System'
ASSETS = 'editor_toolset.toolsets.asset.AssetTools'
APP = 'EditorToolset.EditorAppToolset'
VECTOR = '/Script/CoreUObject.Vector3f'
EXPRESSION = '/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression'


def ref(path):
    return {'refPath': path}


def asset(path):
    return ref(f'{path}.{path.rsplit("/", 1)[-1]}')


def main():
    client = NativeMCP()

    def call(name, toolset=TOOLSET, **arguments):
        response = client.tool('call_tool', dict(toolset_name=toolset, tool_name=name, arguments=arguments))
        assert not response.get('isError'), (name, response)
        result = json.loads(next(row['text'] for row in response['content'] if row['type'] == 'text'))
        assert not result.get('error'), (name, result)
        return result.get('returnValue')

    def stack(emitter, script='', module='', parameter=None, renderer=-1):
        return dict(system=asset(TARGET), emitterName=emitter, scriptName=script,
                    moduleName=module, inputNameStack=[parameter] if parameter else [], rendererIndex=renderer)

    def expression(emitter, script, module, parameter, code):
        call('SetStackInputData', stackInputRef=stack(emitter, script, module, parameter),
             inputData=dict(struct=ref(EXPRESSION), value=dict(hlslExpression=code)))

    assert call('IsPIERunning', APP) is False
    assert not call('exists', ASSETS, path=TARGET), f'Preserve existing asset {TARGET}'
    call('CreateNiagaraSystem', assetName=TARGET.rsplit('/', 1)[-1], assetPath=ROOT, templateSystem=asset(DONOR))
    system = json.loads(call('GetSystemData', system=asset(TARGET))['propertyValues'])
    system.update(MaxPoolSize=12, PoolPrimeSize=2)
    call('SetSystemData', system=asset(TARGET), systemData=dict(propertyValues=json.dumps(system)))
    summary = call('GetSystemSummary', system=asset(TARGET))
    old = [dict(name=v['name'], type=v['type']) for v in summary['userVariables']
           if v['name'].rsplit('.', 1)[-1] in ('Color', 'TargetOffset', 'Seed')]
    call('RemoveUserVariables', system=asset(TARGET), variablesToRemove=old)
    call('AddUserVariables', system=asset(TARGET), variablesToAdd=[dict(
        name='User.Direction', type=dict(classStructOrEnum=ref(VECTOR)),
        defaultValue=dict(struct=ref(VECTOR), value=dict(x=-1, y=0, z=0)),
        description='Outward normal of the accepted boss contact, in world space.')])

    angle = '(float(Particles.UniqueID)*2.39996323)'
    radial = f'float3(cos({angle}),sin({angle}),0)'
    normal = 'normalize(User.Direction+float3(0.0001,0,0))'
    radius = 'clamp(User.Radius,8.0,90.0)'
    age = 'saturate(Particles.NormalizedAge)'
    size = f'clamp({radius}/24.0,.65,2.3)'
    for emitter, count, mesh in [('UpwardMeshBurst', 5, True), ('SimpleSpriteBurst', 9, False)]:
        topology = call('GetEmitterTopology', emitterRef=stack(emitter))
        burst = next(m['moduleName'] for m in topology['emitterUpdateScript']['modules'] if m['moduleName'].startswith('SpawnBurst'))
        expression(emitter, 'EmitterUpdateScript', burst, 'Spawn Count', f'int(floor({count}.0*saturate(User.Intensity)+.5))')
        for stage, key in [('ParticleSpawnScript', 'particleSpawnScript'), ('ParticleUpdateScript', 'particleUpdateScript')]:
            update = stage == 'ParticleUpdateScript'
            assignment = next(m['moduleName'] for m in topology[key]['modules'] if m['moduleName'] not in ('InitializeParticle', 'ParticleState'))
            params = {i['name'].rsplit('.', 1)[-1]: i['name'] for m in topology[key]['modules']
                      if m['moduleName'] == assignment for i in m['inputs']}
            lift = '.46' if mesh else '.28'
            displacement = f'({normal}*.28+{radial}*.15)*{radius}'
            if update:
                displacement += (f'+({normal}*.85+{radial}*.76+float3(0,0,{lift}))'
                                 f'*{radius}*(1.0-exp(-{age}*4.0))')
                if mesh:
                    displacement += f'-float3(0,0,1.15)*{radius}*{age}*{age}'
            tint = 'float4(.42,.21,.09,1.0)' if mesh else f'float4(.62,.40,.22,.55*(1.0-{age}))'
            expression(emitter, stage, assignment, params['Position'], f'Engine.Owner.Position+{displacement}')
            expression(emitter, stage, assignment, params['Color'], tint)
            if mesh:
                shrink = f'*(1.0-.98*pow(saturate(({age}-.67)/.33),2.0))' if update else ''
                turn = f'({angle}+{age}*5.8)' if update else angle
                axis = f'normalize({radial}+float3(0,0,.65))'
                expression(emitter, stage, assignment, params['MeshScale'], f'float3(1.6,1.35,1.3)*{size}{shrink}')
                expression(emitter, stage, assignment, params['MeshOrientation'], f'float4({axis}*sin({turn}*.5),cos({turn}*.5))')
            else:
                grow = f'*(1.0+{age}*1.6)' if update else ''
                expression(emitter, stage, assignment, params['SpriteSize'], f'float2(11,11)*{size}{grow}')
        initialize = next(m for m in topology['particleSpawnScript']['modules'] if m['moduleName'] == 'InitializeParticle')
        for parameter in initialize['inputs']:
            if parameter['bIsVisible'] and parameter['name'] in ('Lifetime', 'Lifetime Min', 'Lifetime Max'):
                expression(emitter, 'ParticleSpawnScript', 'InitializeParticle', parameter['name'],
                           'clamp(User.Duration,.22,.85)' + ('' if mesh else '*.72'))
        expression(emitter, 'ParticleSpawnScript', 'InitializeParticle', 'Color', 'float4(.55,.31,.16,1.0)')
        if mesh:
            renderer = json.loads(call('GetRendererData', rendererRef=stack(emitter, renderer=0))['propertyValues'])
            renderer['Meshes'][0]['mesh'] = asset('/Game/Gameplay/CoreLoop/SM_Walnut_HalfShell')
            renderer['OverrideMaterials'][0]['explicitMat'] = asset(ROOT + '/TransientEditable/Materials/MI_NutHalfShell_Niagara')
            call('SetRendererData', renderer=stack(emitter, renderer=0), rendererData=dict(propertyValues=json.dumps(renderer)))

    call('OpenEditorForAsset', APP, assetPath=TARGET)
    for _ in range(90):
        state = call('GetSystemCompileState', system=asset(TARGET))
        if not state['bIsCompiling'] and not state['bIsStale']:
            break
        time.sleep(.35)
    assert not (state['bIsCompiling'] or state['bIsStale'] or state['bHasErrors']), state
    assert call('save_assets', ASSETS, asset_paths=[TARGET])
    print(json.dumps(dict(asset=TARGET, compile=state), ensure_ascii=False))


if __name__ == '__main__':
    main()
