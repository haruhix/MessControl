"""Create the editable, bounded Food.AutoMouth Niagara system via Epic native MCP.

No build; idle live Editor required. Donor Rocket assets are read-only. The new
system owns ordinary Niagara modules/assignments, so artists can edit every layer.
"""
import json,sys,time,hashlib
from pathlib import Path
from native_mcp_client import NativeMCP

OUT=Path('E:/CodexWork/MessControl/Validation/FoodAutoMouth_20261108')
ROOT='/Game/Gameplay/Food/VFX'; TARGET=ROOT+'/NS_Food_AutoMouth'
SOURCE='/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Impact'
PROJECT=Path('E:/DEVGAME/MessControl')
TS='NiagaraToolsets.NiagaraToolset_System'; AS='editor_toolset.toolsets.asset.AssetTools'
FLOAT='/Script/Niagara.NiagaraFloat'; INT='/Script/Niagara.NiagaraInt32'
VEC='/Script/CoreUObject.Vector3f'; COLOR='/Script/CoreUObject.LinearColor'
POSITION='/Script/Niagara.NiagaraPosition'; QUAT='/Script/CoreUObject.Quat4f'
EXPR='/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression'
c=NativeMCP();trace=[];OUT.mkdir(parents=True,exist_ok=True)
def ref(p):return dict(refPath=p)
def obj(p):return ref(p+'.'+p.rsplit('/',1)[-1])
def value(t,v=None):return dict(struct=ref(t),**({'value':v} if v is not None else {}))
def write(n,d): (OUT/n).write_text(json.dumps(d,indent=2,ensure_ascii=False),encoding='utf8')
def call(name,toolset=TS,**args):
 r=c.tool('call_tool',dict(toolset_name=toolset,tool_name=name,arguments=args))
 if r.get('isError'):write('FailedCall.json',dict(tool=name,args=args,result=r))
 assert not r.get('isError'),r
 d=json.loads(next(x['text'] for x in r['content'] if x['type']=='text'))
 assert not d.get('error'),d
 ret=d.get('returnValue');trace.append(dict(tool=name,args=args,result=ret));write('AuthorTrace.json',trace);return ret
def stack(em,script='',module='',name=None,index=-1):
 return dict(system=obj(TARGET),emitterName=em,scriptName=script,moduleName=module,rendererIndex=index,inputNameStack=[name] if name else [])
def expr(em,script,module,name,code):
 return call('SetStackInputData',stackInputRef=stack(em,script,module,name),inputData=value(EXPR,dict(hlslExpression=code)))
def number(em,script,module,name,n,typ=FLOAT):
 return call('SetStackInputData',stackInputRef=stack(em,script,module,name),inputData=value(typ,dict(value=n)))
def assignment(em,script,attrs):
 payload={VEC:dict(x=0,y=0,z=0),POSITION:dict(x=0,y=0,z=0),COLOR:dict(r=1,g=1,b=1,a=1),QUAT:dict(x=0,y=0,z=0,w=1),'/Script/CoreUObject.Vector2f':dict(x=0,y=0)}
 entries=[dict(variable=dict(name=n,type=dict(classStructOrEnum=ref(t))),defaultValue=value(t,payload[t])) for n,t,code in attrs]
 m=call('AddSetParametersModule',moduleLocationRef=stack(em,script),parameters=entries)['moduleName']
 for name,typ,code in attrs:expr(em,script,m,name,code)
 return m

assert call('IsPIERunning','EditorToolset.EditorAppToolset') is False
source_disk=PROJECT/('Content/'+SOURCE.removeprefix('/Game/')+'.uasset')
source_hash=hashlib.sha256(source_disk.read_bytes()).hexdigest()
exists=call('exists',AS,path=TARGET)
if exists:assert (OUT/'NS_Food_AutoMouth.created.json').exists(),'Preserve existing artwork'
else:
 call('CreateNiagaraSystem',assetName='NS_Food_AutoMouth',assetPath=ROOT,templateSystem=obj(SOURCE))
 write('NS_Food_AutoMouth.created.json',dict(target=TARGET,source=SOURCE))

summary=call('GetSystemSummary',system=obj(TARGET))
if any(e['emitterName']=='PhysicalSmoke' for e in summary['emitters']):
 call('RemoveEmitter',emitterToRemove=stack('PhysicalSmoke'))
# Remove only this task-owned system's donor assignments and forces. The flight
# is analytic, so no solver, collision, velocity integration or attractor overshoot.
for em in ('UpwardMeshBurst','SimpleSpriteBurst'):
 top=call('GetEmitterTopology',emitterRef=stack(em))
 for script,key in [('ParticleSpawnScript','particleSpawnScript'),('ParticleUpdateScript','particleUpdateScript')]:
  for m in top[key]['modules']:
   if m['moduleName'] not in ('InitializeParticle','ParticleState'):
    call('RemoveModule',moduleToRemove=stack(em,script,m['moduleName']))
 data=json.loads(call('GetEmitterData',emitterRef=stack(em))['propertyValues'])
 data.update(bLocalSpace=False,SimTarget='CPUSim',CalculateBoundsMode='Dynamic',PreAllocationCount=64,bDeterminism=True,RandomSeed=73)
 call('SetEmitterData',emitter=stack(em),emitterData=dict(propertyValues=json.dumps(data)))

existing=call('GetSystemSummary',system=obj(TARGET))['userVariables']
if existing:call('RemoveUserVariables',system=obj(TARGET),variablesToRemove=[dict(name=v['name'],type=v['type']) for v in existing])
defaults=[('Color',COLOR,dict(r=.7,g=.035,b=.05,a=1),'Accepted food tint; authoritative ingredient data.'),
 ('TargetOffset',VEC,dict(x=450,y=0,z=160),'World-oriented MouthWorld-ComponentWorld offset, centimetres; fixed accepted endpoint.'),
 ('Duration',FLOAT,dict(value=.9),'Flight duration seconds (.35..1.8); cosmetic only.'),
 ('Radius',FLOAT,dict(value=32),'Source food bounds radius centimetres, clamped 6..100.'),
 ('Intensity',FLOAT,dict(value=1),'Bounded population gain 0..1; default16 crumbs and10 motes.'),
 ('Seed',INT,dict(value=17),'Accepted cosmetic seed; no gameplay RNG in the effect.')]
call('AddUserVariables',system=obj(TARGET),variablesToAdd=[dict(name='User.'+n,type=dict(classStructOrEnum=ref(t)),defaultValue=value(t,v),description=d) for n,t,v,d in defaults])

# Bound the float seed phase BEFORE adding UniqueID. Integer remainder/bitwise
# operators are unsupported by the CPU VectorVM's ES2 translation. Losing low
# seed bits is cosmetic; keeping every small particle ID distinct is essential.
seed='(frac(float(User.Seed)*.000244140625)*4096.0)'
h1='frac(sin((float(Particles.UniqueID)+'+seed+'*.173+17.0)*78.233)*23421.631)'
h2='frac(sin((float(Particles.UniqueID)+'+seed+'*.071+41.0)*39.425)*12345.678)'
h3='frac(sin((float(Particles.UniqueID)+'+seed+'*.113+83.0)*19.173)*19873.123)'
theta='('+h1+'*6.2831853)'; radius='clamp(User.Radius,6.0,100.0)'
birth='(float3(cos('+theta+'),sin('+theta+'),('+h3+'-.35)*.8)*'+radius+'*(.12+.55*'+h2+'))'
age='saturate(Particles.NormalizedAge)';p='saturate(('+age+'-.13)/.87)';ease='('+p+'*'+p+'*(3.0-2.0*'+p+'))'
scatter='(float3(cos('+theta+'),sin('+theta+'),.45+.6*'+h3+')*'+radius+'*1.4*(1.0-exp(-'+age+'*17.0)))'
arc='(float3(0,0,clamp(length(User.TargetOffset)*.18,55.0,220.0))*sin('+ease+'*3.14159265)*(.8+.35*'+h2+'))'
swirl='(float3(cos('+theta+'+'+age+'*7.0),sin('+theta+'+'+age+'*7.0),0)*'+radius+'*.55*sin('+ease+'*3.14159265)*(1.0-'+ease+'))'
flight='(Engine.Owner.Position+User.TargetOffset*'+ease+'+('+birth+'+'+scatter+')*(1.0-'+ease+')+'+arc+'+'+swirl+')'
scale='(clamp(User.Radius/32.0,.35,1.45)*(.48+.72*'+h2+'))'
shrink='(1.0-.98*pow(saturate(('+age+'-.78)/.22),2.0))'
axis='normalize(float3('+h1+'-.43,'+h2+'-.57,'+h3+'-.31))'
spin='(('+h2+'*2.0+'+age+'*(.55+'+h3+'))*6.2831853)'
quat='float4('+axis+'*sin('+spin+'*.5),cos('+spin+'*.5))'
for em,count,mesh in [('UpwardMeshBurst',16,True),('SimpleSpriteBurst',10,False)]:
 top=call('GetEmitterTopology',emitterRef=stack(em))
 burst=next(m['moduleName'] for m in top['emitterUpdateScript']['modules'] if m['moduleName'].startswith('SpawnBurst'))
 expr(em,'EmitterUpdateScript',burst,'Spawn Count','int(floor('+str(count)+'.0*saturate(User.Intensity)+.5))')
 init=next(m for m in top['particleSpawnScript']['modules'] if m['moduleName']=='InitializeParticle')
 fields={i['name'] for i in init['inputs'] if i['bIsVisible']}
 for field in ('Lifetime Min','Lifetime Max') if 'Lifetime Min' in fields else ('Lifetime',):
  expr(em,'ParticleSpawnScript','InitializeParticle',field,'clamp(User.Duration,.35,1.8)*(.87+.21*'+h3+')')
 expr(em,'ParticleSpawnScript','InitializeParticle','Color','float4(User.Color.rgb,1)')
 expr(em,'ParticleSpawnScript','InitializeParticle','Position Offset','float3(0,0,0)')
 for name in ('Mesh Uniform Scale Min','Mesh Uniform Scale Max') if mesh else ('Uniform Sprite Size Min','Uniform Sprite Size Max'):
  if name in fields:expr(em,'ParticleSpawnScript','InitializeParticle',name,'1.0')
 color='float4(User.Color.rgb*(.72+.38*'+h2+'),'+('1.0' if mesh else '(.65*saturate((1.0-'+age+')/.18))')+')'
 attrs=[('Particles.Position',POSITION,'Engine.Owner.Position+'+birth),('Particles.Color',COLOR,'float4(User.Color.rgb*(.72+.38*'+h2+'),1)')]
 attrs+=[('Particles.MeshScale',VEC,'float3(1,.72,.55)*'+scale),('Particles.MeshOrientation',QUAT,quat)] if mesh else [('Particles.SpriteSize','/Script/CoreUObject.Vector2f','float2(7,7)*'+scale)]
 assignment(em,'ParticleSpawnScript',attrs)
 attrs=[('Particles.Position',POSITION,flight),('Particles.Color',COLOR,color)]
 attrs+=[('Particles.MeshScale',VEC,'float3(1,.72,.55)*'+scale+'*'+shrink),('Particles.MeshOrientation',QUAT,quat)] if mesh else [('Particles.SpriteSize','/Script/CoreUObject.Vector2f','float2(7,7)*'+scale+'*'+shrink)]
 assignment(em,'ParticleUpdateScript',attrs)
 renderer=json.loads(call('GetRendererData',rendererRef=stack(em,index=0))['propertyValues'])
 if mesh:
  renderer['Meshes'][0]['mesh']=obj(ROOT+'/SM_FoodCrumb');renderer['bCastShadows']=False
  renderer['OverrideMaterials'][0]['explicitMat']=obj(ROOT+'/M_FoodCrumb')
 else:renderer.update(Material=obj(ROOT+'/M_FoodMote'),bCastShadows=False)
 call('SetRendererData',renderer=stack(em,index=0),rendererData=dict(propertyValues=json.dumps(renderer)))

system=json.loads(call('GetSystemData',system=obj(TARGET))['propertyValues'])
assert call('exists',AS,path=ROOT+'/NET_Food_AutoMouth'),'Run resource recipe first to prepare the one-shot Effect Type'
system.update(bFixedBounds=False,WarmupTime=0,WarmupTickCount=0,MaxPoolSize=16,PoolPrimeSize=2,
 EffectType=obj(ROOT+'/NET_Food_AutoMouth'))
call('SetSystemData',system=obj(TARGET),systemData=dict(propertyValues=json.dumps(system)))
expr('None','SystemUpdateScript','SystemState','Loop Duration','clamp(User.Duration,.35,1.8)*1.12+.1')
call('OpenEditorForAsset','EditorToolset.EditorAppToolset',assetPath=TARGET)
ready=False
for _ in range(45):
 state=call('GetSystemCompileState',system=obj(TARGET));ready=not(state['bIsCompiling'] or state['bIsStale']) and all(s['lastCompileStatus']=='UpToDate' for s in state['scripts'])
 if ready or (state['bHasErrors'] and not state['bIsCompiling']):break
 time.sleep(.25)
write('Compile.json',state);assert ready and not state['bHasErrors'],state
assert call('save_assets',AS,asset_paths=[TARGET])
assert hashlib.sha256(source_disk.read_bytes()).hexdigest()==source_hash,'Donor modified'
summary=call('GetSystemSummary',system=obj(TARGET))
write('Niagara.json',dict(target=TARGET,saved=True,compile=state,summary=summary,donor_unchanged=True,
 architecture='Two CPU world-space analytic emitters;16 opaque12-triangle crumbs +10 soft7cm motes maximum; exact endpoint, no collision/solver/child particle actors.',
 bounds='Dynamic CPU bounds track complete source-to-endpoint path, no fixed arena bound.',
 pooling=dict(max=16,prime=2),validation='Compile/save/readback only; real scenario and artistic approval require root runtime validation.'))
print('FOOD_AUTOMOUTH_NIAGARA_SAVED '+TARGET,flush=True)
