"""Build the editable food-wedge/crumb effect with the existing Epic MCP.

Preserves the prior saved draft and canonical system. Run outside the Editor
with Python -B. Geometry/material recipe must run first in the idle Editor.
"""
import hashlib,json,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from native_mcp_client import NativeMCP

OUT=Path('E:/CodexWork/MessControl/Validation/FoodJuicyVFX_20261109/Niagara')
ROOT='/Game/Gameplay/Food/VFX/Juicy'
TARGET=ROOT+'/NS_FoodJuicy_Pulp'
SOURCE='/Game/Gameplay/Food/VFX/Drafts/NS_Food_Juicy_Draft_20261109'
TS='NiagaraToolsets.NiagaraToolset_System'
AS='editor_toolset.toolsets.asset.AssetTools'
FLOAT='/Script/Niagara.NiagaraFloat'
VEC='/Script/CoreUObject.Vector3f'
COLOR='/Script/CoreUObject.LinearColor'
POSITION='/Script/Niagara.NiagaraPosition'
QUAT='/Script/CoreUObject.Quat4f'
EXPR='/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression'

def ref(p):return dict(refPath=p)
def obj(p):return ref(p+'.'+p.rsplit('/',1)[-1])
def value(t,v):return dict(struct=ref(t),value=v)
def stack(em,script='',module='',name=None,index=-1):
    return dict(system=obj(TARGET),emitterName=em,scriptName=script,moduleName=module,
        rendererIndex=index,inputNameStack=[name] if name else [])

def run():
    OUT.mkdir(parents=True,exist_ok=True)
    c=NativeMCP();trace=[]
    def call(name,toolset=TS,**args):
        result=c.tool('call_tool',dict(toolset_name=toolset,tool_name=name,arguments=args))
        assert not result.get('isError'),result
        data=json.loads(next(x['text'] for x in result['content'] if x['type']=='text'))
        assert not data.get('error'),data
        returned=data.get('returnValue')
        trace.append(dict(tool=name,args=args,result=returned))
        (OUT/'Trace.json').write_text(json.dumps(trace,indent=2),encoding='utf8')
        return returned
    def expr(em,script,module,name,code):
        call('SetStackInputData',stackInputRef=stack(em,script,module,name),
            inputData=value(EXPR,dict(hlslExpression=code)))
    def assignment(em,script,attrs):
        defaults={VEC:dict(x=0,y=0,z=0),POSITION:dict(x=0,y=0,z=0),
            COLOR:dict(r=1,g=1,b=1,a=1),QUAT:dict(x=0,y=0,z=0,w=1)}
        entries=[dict(variable=dict(name=n,type=dict(classStructOrEnum=ref(t))),
            defaultValue=value(t,defaults[t])) for n,t,code in attrs]
        module=call('AddSetParametersModule',moduleLocationRef=stack(em,script),parameters=entries)['moduleName']
        for name,typ,code in attrs:expr(em,script,module,name,code)
        return module

    assert call('IsPIERunning','EditorToolset.EditorAppToolset') is False
    donor=Path('E:/DEVGAME/MessControl/Content/'+SOURCE.removeprefix('/Game/')+'.uasset')
    before=hashlib.sha256(donor.read_bytes()).hexdigest()
    if call('exists',AS,path=TARGET):
        assert (OUT/'Created.json').exists(),'Preserve existing artwork'
    else:
        for resource in ('M_FoodJuicy_Pulp','SM_FoodJuicy_Chunk','SM_FoodJuicy_Crumb'):
            assert call('exists',AS,path=ROOT+'/'+resource),resource
        call('CreateNiagaraSystem',assetName='NS_FoodJuicy_Pulp',assetPath=ROOT,templateSystem=obj(SOURCE))
        (OUT/'Created.json').write_text(json.dumps(dict(target=TARGET,donor=SOURCE)),encoding='utf8')

    descriptions=dict(Intensity='Cosmetic gain 0..1: six primary wedges and eighteen crumbs.',
        Radius='Captured portion radius in centimetres; presentation clamps to 8..90.',
        Duration='Captured flight duration; presentation clamps to .45..1.8 seconds.')
    variables=call('GetSystemSummary',system=obj(TARGET))['userVariables']
    updates=[]
    for variable in variables:
        name=variable['name'].removeprefix('User.')
        if name in descriptions:
            variable['description']=descriptions[name];updates.append(variable)
    call('AddUserVariables',system=obj(TARGET),variablesToAdd=updates)

    phase='(frac(float(User.Seed)*.000244140625)*4096.0)'
    randoms=[f'frac(sin((float(Particles.UniqueID)+{phase}*{a}+{b})*{d})*{e})'
        for a,b,d,e in [('.173','17.0','78.233','23421.631'),('.071','41.0','39.425','12345.678'),('.113','83.0','19.173','19873.123')]]
    h1,h2,h3=('Particles.FoodVariation.x','Particles.FoodVariation.y','Particles.FoodVariation.z')
    radius='clamp(User.Radius,8.0,90.0)'
    theta=f'({h1}*6.2831853)'
    birth=f'(float3(cos({theta}),sin({theta}),({h3}-.45)*.6)*{radius}*(.08+.22*{h2}))'
    delay=f'(.10+.055*{h1})'
    p=f'saturate((Particles.Age-{delay})/max(Particles.Lifetime-{delay},.01))'
    q=f'({p}*{p}*{p}*({p}*({p}*6.0-15.0)+10.0))'
    pop=f'(float3(cos({theta}),sin({theta}),.35+.45*{h3})*{radius}*(.45+.3*{h2})*(1.0-exp(-Particles.Age*28.0)))'
    arc=f'(float3(0,0,clamp(length(User.TargetOffset)*.025,28.0,105.0))*sin({q}*3.14159265)*(.85+.25*{h2}))'
    side=f'(float3(-sin({theta}),cos({theta}),0)*{radius}*.18*sin({q}*3.14159265))'
    flight=f'(Engine.Owner.Position+User.TargetOffset*{q}+({birth}+{pop})*(1.0-{q})+{arc}+{side})'
    age='saturate(Particles.NormalizedAge)'
    shrink=f'(1.0-.995*pow(saturate(({age}-.86)/.14),2.0))'
    axis=f'normalize(float3({h1}-.43,{h2}-.57,{h3}-.31))'
    angle=f'(({h2}+{age}*(.42+.38*{h3}))*6.2831853)'
    quat=f'float4({axis}*sin({angle}*.5),cos({angle}*.5))'
    for em,count,mesh,large in [('UpwardMeshBurst',6,'SM_FoodJuicy_Chunk',True),('SimpleSpriteBurst',18,'SM_FoodJuicy_Crumb',False)]:
        topology=call('GetEmitterTopology',emitterRef=stack(em))
        for script,key in [('ParticleSpawnScript','particleSpawnScript'),('ParticleUpdateScript','particleUpdateScript')]:
            for module in topology[key]['modules']:
                if module['moduleName'] not in ('InitializeParticle','ParticleState'):
                    call('RemoveModule',moduleToRemove=stack(em,script,module['moduleName']))
        data=json.loads(call('GetEmitterData',emitterRef=stack(em))['propertyValues'])
        data.update(bLocalSpace=False,SimTarget='CPUSim',CalculateBoundsMode='Dynamic',
            PreAllocationCount=count,bDeterminism=True,RandomSeed=73)
        call('SetEmitterData',emitter=stack(em),emitterData=dict(propertyValues=json.dumps(data)))
        topology=call('GetEmitterTopology',emitterRef=stack(em))
        burst=next(m['moduleName'] for m in topology['emitterUpdateScript']['modules'] if m['moduleName'].startswith('SpawnBurst'))
        expr(em,'EmitterUpdateScript',burst,'Spawn Count',f'int(floor({count}.0*saturate(User.Intensity)+.5))')
        init=next(m for m in topology['particleSpawnScript']['modules'] if m['moduleName']=='InitializeParticle')
        fields={i['name'] for i in init['inputs'] if i['bIsVisible']}
        # Lifetime variation is sampled once in InitializeParticle; persist the
        # same analytic seed separately for the later mesh/material motion.
        for name in ('Lifetime Min','Lifetime Max') if 'Lifetime Min' in fields else ('Lifetime',):
            expr(em,'ParticleSpawnScript','InitializeParticle',name,f'clamp(User.Duration,.45,1.8)*(.9+.10*{randoms[2]})')
        expr(em,'ParticleSpawnScript','InitializeParticle','Color','float4(User.Color.rgb,1)')
        assignment(em,'ParticleSpawnScript',[('Particles.FoodVariation',VEC,'float3('+','.join(randoms)+')')])
        size=(f'(clamp(User.Radius/45.0,.8,1.8)*(1.15+.9*{h2}))' if large
            else f'(clamp(User.Radius/45.0,.8,1.8)*(.20+.32*{h2}))')
        shape='float3(1,.85,.72)' if large else 'float3(1,.75,.48)'
        color=f'float4(User.Color.rgb*(.94+.14*{h2}),1)'
        assignment(em,'ParticleSpawnScript',[
            ('Particles.Position',POSITION,'Engine.Owner.Position+'+birth),
            ('Particles.Color',COLOR,color),('Particles.MeshScale',VEC,shape+'*'+size),
            ('Particles.MeshOrientation',QUAT,quat)])
        assignment(em,'ParticleUpdateScript',[
            ('Particles.Position',POSITION,flight),('Particles.Color',COLOR,color),
            ('Particles.MeshScale',VEC,shape+'*'+size+'*'+shrink),('Particles.MeshOrientation',QUAT,quat)])
        renderer=json.loads(call('GetRendererData',rendererRef=stack(em,index=0))['propertyValues'])
        renderer['Meshes'][0]['mesh']=obj(ROOT+'/'+mesh)
        renderer['OverrideMaterials'][0]['explicitMat']=obj(ROOT+'/M_FoodJuicy_Pulp')
        renderer['bCastShadows']=False
        call('SetRendererData',renderer=stack(em,index=0),rendererData=dict(propertyValues=json.dumps(renderer)))
        print('FOOD_JUICY_EMITTER_AUTHORED '+em,flush=True)
    data=json.loads(call('GetSystemData',system=obj(TARGET))['propertyValues'])
    data.update(bFixedBounds=False,WarmupTime=0,WarmupTickCount=0,MaxPoolSize=16,PoolPrimeSize=2)
    call('SetSystemData',system=obj(TARGET),systemData=dict(propertyValues=json.dumps(data)))
    expr('None','SystemUpdateScript','SystemState','Loop Duration','clamp(User.Duration,.45,1.8)*1.12+.1')
    call('OpenEditorForAsset','EditorToolset.EditorAppToolset',assetPath=TARGET)
    ready=False
    for _ in range(30):
        state=call('GetSystemCompileState',system=obj(TARGET))
        ready=not(state['bIsCompiling'] or state['bIsStale']) and all(s['lastCompileStatus']=='UpToDate' for s in state['scripts'])
        if ready or (state['bHasErrors'] and not state['bIsCompiling']):break
        time.sleep(.25)
    assert ready and not state['bHasErrors'],state
    assert call('save_assets',AS,asset_paths=[TARGET])
    assert hashlib.sha256(donor.read_bytes()).hexdigest()==before,'Prior draft changed'
    report=dict(target=TARGET,compile=state,summary=call('GetSystemSummary',system=obj(TARGET)),
        saved=True,donor_unchanged=True,particles_per_cue=24,
        behavior='6 faceted pulp/peel wedges +18 crumbs; .10-.155s pop then staggered analytic collection; late shrink',
        simulation='Two CPU mesh emitters, dynamic bounds, no collision/events/solver/physical actors',
        integration='Composer action/profile and scoped prewarm verification pending')
    (OUT/'Result.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print('FOOD_JUICY_NIAGARA_SAVED '+TARGET,flush=True)

if __name__=='__main__':run()
