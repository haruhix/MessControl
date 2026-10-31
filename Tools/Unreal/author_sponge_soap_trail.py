"""Author one editable world-space Sponge soap trail through Epic Niagara MCP.

Default is an offline plan. --apply --editor-pid PID requires the root's idle,
owned Editor handoff. Existing Sponge/Driver systems and foam resources are
read-only templates; this recipe creates only NS_Sponge_Trail.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

PROJECT = Path('E:/DEVGAME/MessControl')
OUT = Path('E:/CodexWork/MessControl/Validation/SpongeRevision_20261030/SoapTrail')
TEMP = Path('E:/CodexTemp/MessControlSpongeRevision_20261030')
ROOT = '/Game/Gameplay/ActiveSkills/VFX'
SOURCE = ROOT + '/NS_Sponge_Loop'
TARGET = ROOT + '/NS_Sponge_Trail'
MAT = ROOT + '/Materials/M_ActiveDentalFoamAtlas'
TEX = ROOT + '/Textures/T_DentalFoamClumps'
SYSTEM_TOOLS = 'NiagaraToolsets.NiagaraToolset_System'
ASSET_TOOLS = 'editor_toolset.toolsets.asset.AssetTools'
EDITOR_TOOLS = 'EditorToolset.EditorAppToolset'
FLOAT = '/Script/Niagara.NiagaraFloat'
VECTOR = '/Script/CoreUObject.Vector3f'
COLOR = '/Script/CoreUObject.LinearColor'
BOOL = '/Script/Niagara.NiagaraBool'
EXPR = '/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression'
DEFAULTS = dict(Width=84., Lifetime=2.4, Opacity=.48, SpawnSpacing=18.,
                SurfaceNormal=[0., 0., 1.], Velocity=[0., 0., 0.],
                PreviewMotion=True, PreviewSpeed=220.)


def ref(path):
    return dict(refPath=path)


def obj(path):
    return ref(path + '.' + path.rsplit('/', 1)[-1])


def value(kind, payload):
    return dict(struct=ref(kind), value=payload)


def disk(path):
    return PROJECT / 'Content' / Path(path.removeprefix('/Game/')).with_suffix('.uasset')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(name, data):
    (OUT / name).write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf8')


def plan():
    OUT.mkdir(parents=True, exist_ok=True)
    data = dict(
        target=TARGET, template=SOURCE,
        contract='Ground-supported soap history; cosmetic only, no cleaning authority.',
        new_resources=[TARGET], reused_resources=[MAT, TEX, ROOT + '/NET_ActiveSkills'],
        defaults=DEFAULTS,
        inherited_user_parameters=dict(Color='LinearColor mint-white', Direction='Vector3f world heading', Intensity='float 0..1'),
        architecture='One stateful CPU world-space sprite emitter, stock SpawnPerUnit, no particle forces/collision.',
        sampling='Velocity Vector links to User.Velocity; Spawn Spacing links to User.SpawnSpacing. Normal and heading are captured at particle birth.',
        completion='System/Emitter Inactive Response Complete. Root creates unparented auto-destroy component, sets parameters before Activate, updates supported world transform and velocity, then Deactivate (not DeactivateImmediate).',
        bounds='Dynamic CPU emitter/system bounds include the retained history; existing NET_ActiveSkills retained.',
        budget=dict(default_live_cards=math.ceil(620 / 18 * 2.4) + 2,
                    bounded_max_live_cards=math.ceil(1200 / 16 * 3) + 2,
                    max_local_players=4, atlas_samples_per_pixel=1,
                    qualifier='Arithmetic estimate, not measured GPU/overdraw timing.'),
        sources=[
            dict(url='https://dev.epicgames.com/documentation/en-us/unreal-engine/emitter-update-group-reference-for-niagara-effects-in-unreal-engine',
                 applied='Spawn Per Unit distance sampling, world-space particles, Complete inactive response.'),
            dict(url='https://realtimevfx.com/t/real-time-foam-soap-suds-shader/9919/2',
                 applied='Reuse a low-contrast soap silhouette and avoid refraction/fluids. This reply is an approach proposal, not a completed downloadable pipeline.')],
        readiness='Reflected SpongeAction system reference -> Niagara renderer material -> existing atlas texture. No new material/MID; root owns manifest and first-use proof.',
        validation='Not applied. Gameplay camera, supported slopes, retained 2.4s tail, no airborne stamp, interruption and realistic-player overdraw still require root review.',
    )
    write('Plan.json', data)
    return data


def editor_guard(pid, resume=False):
    script = OUT / 'EditorGuard.py'
    script.write_text(
        'import os,json,unreal as u\n'
        'from pathlib import Path\n'
        f'assert os.getpid()=={pid}\n'
        "assert 'mcactiveskillreview' in u.SystemLibrary.get_command_line().lower()\n"
        'assert not u.EditorLevelLibrary.get_pie_worlds(True), "Preserve manual Play"\n'
        'assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), "Preserve unsaved maps"\n'
        f'allowed={{{TARGET!r}}} if {resume!r} else set()\n'
        'assert {p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}.issubset(allowed), "Preserve unrelated unsaved content"\n'
        f'Path({str(OUT / "EditorGuard.json")!r}).write_text(json.dumps(dict(pid=os.getpid(),no_pie=True,no_dirty=True)),encoding="utf8")\n',
        encoding='utf8')
    env = dict(os.environ, TEMP=str(TEMP), TMP=str(TEMP), PYTHONDONTWRITEBYTECODE='1', PYTHONIOENCODING='utf-8')
    result = subprocess.run([sys.executable, '-B', str(PROJECT / 'Tools/Unreal/remote_python.py'), str(script)],
                            cwd=PROJECT, env=env, capture_output=True, text=True, encoding='utf8', timeout=30)
    (OUT / 'EditorGuard.Transport.txt').write_text(result.stdout + '\n' + result.stderr, encoding='utf8')
    assert result.returncode == 0, 'Root must provide an idle owned Editor; inspect guard result'


def reopen_owned_editor(pid):
    """Refresh only our view model; otherwise a failed VM status can be cached."""
    script=OUT/'ReopenOwnedTrail.py'
    script.write_text('import os,unreal as u\n'
        f'assert os.getpid()=={pid}\n'
        'assert not u.EditorLevelLibrary.get_pie_worlds(True)\n'
        f'a=u.load_asset({TARGET!r});assert a\n'
        's=u.get_editor_subsystem(u.AssetEditorSubsystem)\n'
        's.close_all_editors_for_asset(a)\n'
        'assert s.open_editor_for_assets([a])\n',encoding='utf8')
    env=dict(os.environ,TEMP=str(TEMP),TMP=str(TEMP),PYTHONDONTWRITEBYTECODE='1',PYTHONIOENCODING='utf-8')
    result=subprocess.run([sys.executable,'-B',str(PROJECT/'Tools/Unreal/remote_python.py'),str(script)],
        cwd=PROJECT,env=env,capture_output=True,text=True,encoding='utf8',timeout=30)
    (OUT/'ReopenCompile.Transport.txt').write_text(result.stdout+'\n'+result.stderr,encoding='utf8')
    assert result.returncode==0,'Owned target editor refresh failed'


def apply(pid, resume=False):
    assert not (OUT / 'Applied.json').exists(), 'Preserve the prior authored result'
    if resume:
        created=json.loads((OUT/'Created.json').read_text(encoding='utf8'))
        assert created==dict(target=TARGET,editor_pid=pid,source=SOURCE),'Resume only this process\'s owned new target'
    else:
        assert not disk(TARGET).exists(), 'Refuse to overwrite an existing trail package'
    editor_guard(pid,resume)
    sys.path.insert(0, str(PROJECT / 'Tools/Unreal'))
    from native_mcp_client import NativeMCP
    client = NativeMCP()
    trace_path=OUT/'Trace.json'
    trace = json.loads(trace_path.read_text(encoding='utf8')) if resume else []
    if resume:
        number=1
        while (OUT/f'Trace.BeforeResume{number:02d}.json').exists():number+=1
        write(f'Trace.BeforeResume{number:02d}.json',trace)

    def call(tool, toolset=SYSTEM_TOOLS, **arguments):
        response = client.tool('call_tool', dict(toolset_name=toolset, tool_name=tool, arguments=arguments))
        if response.get('isError'):
            trace.append(dict(tool=tool,toolset=toolset,arguments=arguments,error=response))
            write('Trace.json',trace)
        assert not response.get('isError'), response
        data = json.loads(next(c['text'] for c in response['content'] if c['type'] == 'text'))
        assert not data.get('error'), data
        result = data.get('returnValue')
        trace.append(dict(tool=tool, toolset=toolset, arguments=arguments, result=result))
        write('Trace.json', trace)
        return result

    def stack(script='', module='', names=None, renderer=-1, emitter='SimpleSpriteBurst'):
        return dict(system=obj(TARGET), emitterName=emitter, scriptName=script,
                    moduleName=module, rendererIndex=renderer, inputNameStack=names or [])

    def get(script, module, name, emitter='SimpleSpriteBurst'):
        return call('GetStackInputData', stackInputRef=stack(script, module, [name], emitter=emitter))

    def put(script, module, name, data, emitter='SimpleSpriteBurst'):
        return call('SetStackInputData', stackInputRef=stack(script, module, [name], emitter=emitter), inputData=data)

    def expression(script, module, name, code):
        return put(script, module, name, value(EXPR, dict(hlslExpression=code)))

    assert call('IsPIERunning', EDITOR_TOOLS) is False
    if resume:
        protected=json.loads((OUT/'ProtectedBefore.json').read_text(encoding='utf8'))
        assert all(digest(Path(p))==h for p,h in protected.items()),'Protected resources changed before resume'
    else:
        protected = {str(p): digest(p) for p in (PROJECT / 'Content/Gameplay/ActiveSkills').rglob('*.uasset')}
        protected[str(PROJECT / 'Content/Maps/L_Mouth.umap')] = digest(PROJECT / 'Content/Maps/L_Mouth.umap')
        write('ProtectedBefore.json', protected)
        assert not call('exists', ASSET_TOOLS, path=TARGET)
        call('CreateNiagaraSystem', assetName='NS_Sponge_Trail', assetPath=ROOT, templateSystem=obj(SOURCE))
        write('Created.json', dict(target=TARGET, editor_pid=pid, source=SOURCE))
    summary=call('GetSystemSummary',system=obj(TARGET))
    if any(e['emitterName']=='UpwardMeshBurst' for e in summary['emitters']):
        call('RemoveEmitter', emitterToRemove=stack(emitter='UpwardMeshBurst'))
    existing_users={v['name'] for v in summary['userVariables']}
    added = []
    for name, default in DEFAULTS.items():
        if 'User.'+name in existing_users:continue
        kind = VECTOR if isinstance(default, list) else BOOL if isinstance(default, bool) else FLOAT
        payload = dict(zip(('x', 'y', 'z'), default)) if isinstance(default, list) else dict(value=-1 if default else 0) if isinstance(default, bool) else dict(value=default)
        added.append(dict(name='User.' + name, type=dict(classStructOrEnum=ref(kind)),
                          defaultValue=value(kind, payload), description={
                              'Width':'Foam footprint width in centimetres; visual only.',
                              'Lifetime':'Retained soap history seconds (.35..3).',
                              'Opacity':'Soft foam opacity (0..1); no emissive gain.',
                              'SpawnSpacing':'Distance between ground stamps in centimetres (16..80).',
                              'SurfaceNormal':'Current supported surface normal; captured at birth.',
                              'Velocity':'Actual supported movement in cm/s, supplied by gameplay; no stationary spawn.',
                              'PreviewMotion':'Asset-preview motion. Gameplay sets FALSE before activation; preview traces a moving strip instead of a pile.',
                              'PreviewSpeed':'Peak asset-preview speed in cm/s; independent of gameplay motion.'}[name]))
    if added:call('AddUserVariables', system=obj(TARGET), variablesToAdd=added)

    emitter = json.loads(call('GetEmitterData', emitterRef=stack())['propertyValues'])
    emitter.update(bLocalSpace=False, SimTarget='CPUSim', CalculateBoundsMode='Dynamic', PreAllocationCount=256)
    call('SetEmitterData', emitter=stack(), emitterData=dict(propertyValues=json.dumps(emitter)))
    system = json.loads(call('GetSystemData', system=obj(TARGET))['propertyValues'])
    system.update(bFixedBounds=False, WarmupTime=0, WarmupTickCount=0)
    call('SetSystemData', system=obj(TARGET), systemData=dict(propertyValues=json.dumps(system)))

    top = call('GetEmitterTopology', emitterRef=stack())
    for script in ('EmitterUpdateScript', 'ParticleSpawnScript', 'ParticleUpdateScript'):
        for module in top[script[0].lower()+script[1:]]['modules']:
            if module['moduleName'] in ('SpawnRate', 'SpawnBurst_Instantaneous', 'ShapeLocation', 'AddVelocity', 'Drag', 'GravityForce') or module['moduleName'].startswith('SolveForcesAndVelocity'):
                call('RemoveModule', moduleToRemove=stack(script, module['moduleName']))
    per_unit=[m for m in top['emitterUpdateScript']['modules'] if m['moduleName'].startswith('SpawnPerUnit')]
    assert len(per_unit)<=1,per_unit
    spawned = per_unit[0] if per_unit else call('AddModule', moduleLocationRef=stack('EmitterUpdateScript'),
                   moduleAsset=ref('/Niagara/Modules/Emitter/SpawnPerUnit.SpawnPerUnit'))
    spawn_module = spawned['moduleName']
    assert {'Velocity Vector', 'Spawn Spacing'}.issubset({i['name'] for i in spawned['inputs']}), spawned
    preview_forward='(length(User.Direction)>.01?normalize(User.Direction):float3(1,0,0))'
    preview_velocity='('+preview_forward+'*clamp(User.PreviewSpeed,0.0,500.0)*cos(System.Age*.9))'
    expression('EmitterUpdateScript', spawn_module, 'Velocity Vector', '(User.PreviewMotion?'+preview_velocity+':User.Velocity)')
    expression('EmitterUpdateScript', spawn_module, 'Spawn Spacing', 'clamp(User.SpawnSpacing,16.0,80.0)')
    put('EmitterUpdateScript', spawn_module, 'Module.Use Movement Tolerance', value('/Script/Niagara.NiagaraBool', dict(value=-1)))
    put('EmitterUpdateScript', spawn_module, 'Movement Tolerance', value(FLOAT, dict(value=1.0)))
    put('EmitterUpdateScript', spawn_module, 'Module.Use Max Movement Threshold', value('/Script/Niagara.NiagaraBool', dict(value=-1)))
    put('EmitterUpdateScript', spawn_module, 'Max Movement Threshold', value(FLOAT, dict(value=2200.0)))
    put('EmitterUpdateScript', spawn_module, 'Use Spawn Probability', value('/Script/Niagara.NiagaraBool', dict(value=-1)))
    expression('EmitterUpdateScript', spawn_module, 'Spawn Probability', 'saturate(User.Intensity)')

    normal='(length(User.SurfaceNormal)>.01?normalize(User.SurfaceNormal):float3(0,0,1))'
    tangent='(User.Direction-'+normal+'*dot(User.Direction,'+normal+'))'
    forward='(length('+tangent+')>.01?normalize('+tangent+'):float3(1,0,0))'
    side='cross('+normal+','+forward+')'
    jitter='frac(sin((float(Particles.UniqueID)+17.0)*78.233)*23421.631)'
    variation='frac(sin((float(Particles.UniqueID)+41.0)*39.425)*12345.678)'
    age='saturate(Particles.NormalizedAge)'
    size='float2(clamp(User.Width,30.0,180.0),clamp(User.SpawnSpacing,16.0,80.0)*2.8)'
    expression('ParticleSpawnScript','InitializeParticle','Lifetime','clamp(User.Lifetime,.35,3.0)')
    expression('ParticleSpawnScript','InitializeParticle','Color','float4(User.Color.rgb,saturate(User.Opacity)*saturate(User.Intensity))')
    preview_offset='(User.PreviewMotion?'+forward+'*(clamp(User.PreviewSpeed,0.0,500.0)/.9)*sin(System.Age*.9):float3(0,0,0))'
    expression('ParticleSpawnScript','InitializeParticle','Position Offset',side+'*(('+jitter+'-.5)*clamp(User.Width,30.0,180.0)*.08)+'+normal+'*1.5+'+preview_offset)
    # The inherited initializer still computes Random Uniform size before the
    # later explicit rectangular assignment. Replace both legacy Radius inputs
    # too; hiding that overwritten calculation would leave dangling bindings.
    for name in ('Uniform Sprite Size Min','Uniform Sprite Size Max'):
        expression('ParticleSpawnScript','InitializeParticle',name,'clamp(User.Width,30.0,180.0)')
    put('ParticleSpawnScript','InitializeParticle','Position Offset Coordinate Space',
        value('/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum',
              dict(enum=ref('/Script/Niagara.ENiagaraCoordinateSpace'),
                   enumName='ENiagaraCoordinateSpace::World',displayName='World')))

    def assignment(script, attribute):
        topology = call('GetEmitterTopology', emitterRef=stack())
        found=[(m['moduleName'], i['name']) for m in topology[script[0].lower()+script[1:]]['modules']
               if m['enabled'] and m['bIsSetParametersModule'] for i in m['inputs'] if i['name']==attribute]
        assert len(found)==1, (script,attribute,found)
        return found[0]

    for attr, code in [('Particles.SpriteFacing', normal), ('Particles.SpriteAlignment', forward),
                       ('Particles.SpriteSize', size+'*(.96+.08*'+variation+')'),
                       ('Particles.SubImageIndex','floor('+variation+'*4.0)'),
                       ('Particles.SpriteRotation','('+jitter+'-.5)*16.0')]:
        module, name = assignment('ParticleSpawnScript', attr)
        expression('ParticleSpawnScript', module, name, code)
    module, name = assignment('ParticleUpdateScript', 'Particles.SpriteSize')
    expression('ParticleUpdateScript',module,name,size+'*(1.0+.05*'+age+')')
    # CPU VectorVM does not provide the HLSL smoothstep intrinsic. Expand the
    # same Hermite envelope explicitly using supported arithmetic operations.
    fade_in='saturate('+age+'/.035)'
    fade_out='saturate(('+age+'-.20)/.80)'
    expression('ParticleUpdateScript','ScaleColor','Scale Alpha',
               '('+fade_in+'*'+fade_in+'*(3.0-2.0*'+fade_in+'))*(1.0-('+fade_out+'*'+fade_out+'*(3.0-2.0*'+fade_out+')))')
    renderer = json.loads(call('GetRendererData', rendererRef=stack(renderer=0))['propertyValues'])
    renderer.update(FacingMode='CustomFacingVector', Alignment='CustomAlignment', bCastShadows=False)
    assert renderer['Material']['refPath']==MAT+'.M_ActiveDentalFoamAtlas'
    call('SetRendererData',renderer=stack(renderer=0),rendererData=dict(propertyValues=json.dumps(renderer)))

    # System-managed emitters inherit completion from SystemState. Their own
    # Inactive Response is hidden and must not be written through that inactive
    # branch, even though the asset schema lists its serialized default.
    path='/Niagara/Enums/ENiagaraSystemInactiveMode.ENiagaraSystemInactiveMode'
    put('SystemUpdateScript','SystemState','Inactive Response',
        value('/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum',dict(enum=ref(path),
              enumName='ENiagaraSystemInactiveMode::NewEnumerator0',displayName='Complete')),emitter='None')
    # The source's SystemState Loop Duration is linked to User.Duration too.
    # Keep the infinite-loop lifecycle independent from the particle envelope.
    put('SystemUpdateScript','SystemState','Loop Duration',value(FLOAT,dict(value=60.0)),emitter='None')
    system_values=call('GetModuleInputValues',moduleRef=stack('SystemUpdateScript','SystemState',emitter='None'))
    assert 'User.Duration' not in json.dumps(system_values) and 'User.Radius' not in json.dumps(system_values),system_values
    lifecycle=get('EmitterUpdateScript','EmitterState','Life Cycle Mode')
    assert lifecycle['value']['displayName']=='System',lifecycle
    write('LifecycleReadback.json',dict(emitter_lifecycle=lifecycle,
          system_inactive_response=get('SystemUpdateScript','SystemState','Inactive Response',emitter='None'),
          hidden_emitter_inactive_response_not_written=True))
    retained=call('GetEmitterTopology',emitterRef=stack())
    binding_audit=[]
    for script in ('EmitterUpdateScript','ParticleSpawnScript','ParticleUpdateScript'):
        for module in retained[script[0].lower()+script[1:]]['modules']:
            if not module['enabled']:continue
            values=call('GetModuleInputValues',moduleRef=stack(script,module['moduleName']))
            serialized=json.dumps(values)
            assert 'User.Radius' not in serialized and 'User.Duration' not in serialized, (script,module['moduleName'],values)
            binding_audit.append(dict(script=script,module=module['moduleName'],legacy_user_bindings=False))
    write('LegacyBindingAudit.json',binding_audit)
    legacy=[dict(name='User.'+n,type=dict(classStructOrEnum=ref(FLOAT)))
            for n in ('Radius','Duration') if 'User.'+n in existing_users]
    if legacy:call('RemoveUserVariables',system=obj(TARGET),variablesToRemove=legacy)

    call('OpenEditorForAsset', EDITOR_TOOLS, assetPath=TARGET)
    reopen_owned_editor(pid)
    ready=False
    for _ in range(90):
        state=call('GetSystemCompileState',system=obj(TARGET))
        ready=not(state['bIsCompiling'] or state['bIsStale']) and all(s['lastCompileStatus']=='UpToDate' for s in state['scripts'])
        if ready or (state['bHasErrors'] and not state['bIsCompiling']):break
        time.sleep(.3)
    assert ready and not state['bHasErrors'] and not state['bHasWarnings'],state
    assert call('save_assets', ASSET_TOOLS, asset_paths=[TARGET])
    actual=call('GetSystemSummary',system=obj(TARGET))
    names={v['name'] for v in actual['userVariables']}
    assert names=={'User.'+n for n in ('Color','Direction','Intensity',*DEFAULTS)},names
    assert len(actual['emitters'])==1 and actual['emitters'][0]['simTarget']=='CPUSim'
    assert all(digest(Path(p))==h for p,h in protected.items()),'A template or unrelated resource was changed'
    write('Applied.json',dict(target=TARGET,editor_pid=pid,saved=True,compile=state,summary=actual,
        package_sha256=digest(disk(TARGET)),protected_unchanged=True,new_materials_or_textures=[],
        representation='World-space distance-spawn stationary foam sprites with graceful lifetime tail',
        validation='Saved/compiled only. Runtime integration, supported surfaces, tail/cleanup, camera/overdraw and artistic review remain pending.'))
    print('SPONGE_SOAP_TRAIL_SAVED_COMPILED '+TARGET,flush=True)


def apply_preview(pid):
    """Targeted usability revision of this task-owned saved system only."""
    assert (OUT/'Applied.json').exists(),'Create/validate the original trail first'
    assert not (OUT/'AppliedPreview.json').exists(),'Keep the previous preview revision'
    editor_guard(pid,True)
    original=json.loads((OUT/'Applied.json').read_text(encoding='utf8'))
    assert original['editor_pid']==pid and original['target']==TARGET
    assert digest(disk(TARGET))==original['package_sha256'],'Owned target changed after the recorded freeze'
    (OUT/'BeforePreview_NS_Sponge_Trail.uasset').write_bytes(disk(TARGET).read_bytes())
    protected={str(p):digest(p) for p in (PROJECT/'Content/Gameplay/ActiveSkills').rglob('*.uasset') if p!=disk(TARGET)}
    sys.path.insert(0,str(PROJECT/'Tools/Unreal'))
    from native_mcp_client import NativeMCP
    client=NativeMCP();trace=[]
    def call(tool,toolset=SYSTEM_TOOLS,**arguments):
        result=client.tool('call_tool',dict(toolset_name=toolset,tool_name=tool,arguments=arguments))
        assert not result.get('isError'),result
        data=json.loads(next(c['text'] for c in result['content'] if c['type']=='text'));assert not data.get('error'),data
        answer=data.get('returnValue');trace.append(dict(tool=tool,arguments=arguments,result=answer));write('PreviewTrace.json',trace);return answer
    def stack(script='',module='',input_name=None):
        return dict(system=obj(TARGET),emitterName='SimpleSpriteBurst',scriptName=script,moduleName=module,
                    rendererIndex=-1,inputNameStack=[input_name] if input_name else [])
    def expression(script,module,input_name,code):
        return call('SetStackInputData',stackInputRef=stack(script,module,input_name),inputData=value(EXPR,dict(hlslExpression=code)))
    variables=[dict(name='User.PreviewMotion',type=dict(classStructOrEnum=ref(BOOL)),defaultValue=value(BOOL,dict(value=-1)),
               description='Asset-preview motion. Gameplay sets FALSE before activation; preview traces a moving strip instead of a pile.'),
               dict(name='User.PreviewSpeed',type=dict(classStructOrEnum=ref(FLOAT)),defaultValue=value(FLOAT,dict(value=220.)),
               description='Peak asset-preview speed in cm/s; independent of gameplay motion.')]
    call('AddUserVariables',system=obj(TARGET),variablesToAdd=variables)
    normal='(length(User.SurfaceNormal)>.01?normalize(User.SurfaceNormal):float3(0,0,1))'
    tangent='(User.Direction-'+normal+'*dot(User.Direction,'+normal+'))'
    forward='(length('+tangent+')>.01?normalize('+tangent+'):float3(1,0,0))'
    side='cross('+normal+','+forward+')'
    jitter='frac(sin((float(Particles.UniqueID)+17.0)*78.233)*23421.631)'
    velocity='(User.PreviewMotion?'+forward+'*clamp(User.PreviewSpeed,0.0,500.0)*cos(System.Age*.9):User.Velocity)'
    offset=side+'*(('+jitter+'-.5)*clamp(User.Width,30.0,180.0)*.08)+'+normal+'*1.5+(User.PreviewMotion?'+forward+'*(clamp(User.PreviewSpeed,0.0,500.0)/.9)*sin(System.Age*.9):float3(0,0,0))'
    expression('EmitterUpdateScript','SpawnPerUnit','Velocity Vector',velocity)
    expression('ParticleSpawnScript','InitializeParticle','Position Offset',offset)
    reopen_owned_editor(pid)
    for _ in range(60):
        state=call('GetSystemCompileState',system=obj(TARGET))
        ready=not(state['bIsCompiling'] or state['bIsStale']) and all(s['lastCompileStatus']=='UpToDate' for s in state['scripts'])
        if ready or (state['bHasErrors'] and not state['bIsCompiling']):break
        time.sleep(.3)
    assert ready and not state['bHasErrors'] and not state['bHasWarnings'],state
    assert call('save_assets',ASSET_TOOLS,asset_paths=[TARGET])
    summary=call('GetSystemSummary',system=obj(TARGET))
    users={v['name']:v for v in summary['userVariables']}
    assert len(users)==11 and users['User.PreviewMotion']['type']['classStructOrEnum']['refPath']==BOOL
    assert users['User.PreviewMotion']['defaultValue']['value']['value']==-1
    assert users['User.PreviewSpeed']['defaultValue']['value']['value']==220
    assert all(digest(Path(p))==h for p,h in protected.items())
    write('AppliedPreview.json',dict(target=TARGET,editor_pid=pid,saved=True,compile=state,summary=summary,
        package_sha256=digest(disk(TARGET)),protected_unchanged=True,original_applied_preserved=True,
        virtual_motion='Preview velocity=Direction*Speed*cos(System.Age*.9), position=Direction*(Speed/.9)*sin(System.Age*.9). Born particles remain world-stationary.',
        runtime_contract='Gameplay sets User.PreviewMotion FALSE before Activate. Then actual velocity0 produces no new stationary foam.',
        validation='Clean compile/save/readback. Native editor-preview screenshot still required; artistic/gameplay acceptance not claimed.'))
    print('SPONGE_SOAP_TRAIL_PREVIEW_SAVED_COMPILED',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply',action='store_true')
    parser.add_argument('--editor-pid',type=int)
    parser.add_argument('--resume-owned',action='store_true',help='Continue only this process\'s recorded new target after a failed authoring attempt')
    parser.add_argument('--preview-motion',action='store_true',help='Add explicit moving asset-preview mode to this task-owned saved trail')
    args=parser.parse_args()
    plan()
    if args.apply:
        assert args.editor_pid and args.editor_pid>0,'Explicit root-owned Editor PID handoff required'
        TEMP.mkdir(parents=True,exist_ok=True)
        if args.preview_motion:apply_preview(args.editor_pid)
        else:apply(args.editor_pid,args.resume_owned)
    else:
        print('OFFLINE_PLAN '+str(OUT/'Plan.json'))
