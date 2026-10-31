"""Four editable starter patterns. New owned assets only; source Rocket VFX stay intact.

Uses the live Niagara MCP stack API. CPU sprites, stock modules, no new inline HLSL.
These are construction seeds, not artistic acceptance of finished game effects.
"""
import json, os, sys
from pathlib import Path

PROJECT=Path('E:/DEVGAME/MessControl')
OUT=Path('E:/CodexWork/MessControl/Validation/ComposerActionWorkspace_20261031/Templates')
OUT.mkdir(parents=True,exist_ok=True)
os.environ['TEMP']=os.environ['TMP']='E:/CodexTemp/MessControlComposerUX_20261031'
sys.path.insert(0,str(PROJECT/'Tools/Unreal'))
from native_mcp_client import NativeMCP
C=NativeMCP(); TS='NiagaraToolsets.NiagaraToolset_System'
ROOT='/Game/Tests/VFXPractice/Effects'
SOURCE='/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Impact'
trace=[]
def obj(p): return {'refPath':p+'.'+p.rsplit('/',1)[-1]}
def call(name,**args):
    r=C.tool('call_tool',dict(toolset_name=TS,tool_name=name,arguments=args))
    assert not r.get('isError'),r
    d=json.loads(next(v['text'] for v in r['content'] if v['type']=='text'))
    assert not d.get('error'),d
    result=d.get('returnValue'); trace.append(dict(tool=name,args=args,result=result))
    (OUT/'AuthorTrace.json').write_text(json.dumps(trace,ensure_ascii=False,indent=2),encoding='utf8')
    return result
def value(kind,data): return dict(struct={'refPath':kind},value=data)
FLOAT='/Script/Niagara.NiagaraFloat'; COLOR='/Script/CoreUObject.LinearColor'
VECTOR='/Script/CoreUObject.Vector3f'; LINK='/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked'
SPECS={
 'Flash':dict(emitter='SimpleSpriteBurst',life=.16,size=92.,tint=(1.,.84,.38,1.),count=3,speed=65.,radius=5.),
 'Smoke':dict(emitter='PhysicalSmoke',life=1.25,size=110.,tint=(.58,.66,.72,.58),count=6,speed=26.,radius=14.),
 'Trail':dict(emitter='SimpleSpriteBurst',life=.38,size=18.,tint=(.28,1.,.83,.75),rate=45.,speed=0.,radius=3.),
 'Glow':dict(emitter='SimpleSpriteBurst',life=.85,size=105.,tint=(.40,1.,.86,.42),rate=3.,speed=0.,radius=2.),
}
def main():
 for name,s in SPECS.items():
    target=ROOT+'/NS_Template_'+name
    target_file=PROJECT/('Content/'+target[len('/Game/'):]+'.uasset')
    marker=OUT/(name+'.created.json')
    if not marker.exists():
        assert not target_file.exists(),f'Preserve existing learner asset: {target}'
        call('CreateNiagaraSystem',assetName=target.rsplit('/',1)[-1],assetPath=ROOT,templateSystem=obj(SOURCE))
        marker.write_text(json.dumps(dict(target=target,source=SOURCE)),encoding='utf8')
    else: assert json.loads(marker.read_text())==dict(target=target,source=SOURCE)
    emitter=s['emitter']
    def ref(script='',module='',names=None,index=-1,em=emitter):
        return dict(system=obj(target),emitterName=em,scriptName=script,moduleName=module,rendererIndex=index,inputNameStack=names or [])
    def put(script,module,field,data):
        return call('SetStackInputData',stackInputRef=ref(script,module,[field]),inputData=data)
    def number(script,module,field,n):return put(script,module,field,value(FLOAT,{'value':n}))
    def link(script,module,field,user,kind):
        return put(script,module,field,value(LINK,dict(linkedVariable=dict(name='User.'+user,type=dict(classStructOrEnum={'refPath':kind})))))
    summary=call('GetSystemSummary',system=obj(target))
    for e in summary['emitters']:
        if e['emitterName']!=emitter:call('RemoveEmitter',emitterToRemove=ref(em=e['emitterName']))
    top=call('GetEmitterTopology',emitterRef=ref())
    for script,key in [('ParticleSpawnScript','particleSpawnScript'),('ParticleUpdateScript','particleUpdateScript')]:
        for m in top[key]['modules']:
            if m['bIsSetParametersModule']:
                call('RemoveModule',moduleToRemove=ref(script,m['moduleName']))
    # Private typed inputs are independently namespaced per layer by the Composer.
    users=[]
    for field,kind,data in [('Tint',COLOR,dict(zip('rgba',s['tint']))),('Size',FLOAT,{'value':s['size']}),('Lifetime',FLOAT,{'value':s['life']})]:
        users.append(dict(name='User.'+field,type=dict(classStructOrEnum={'refPath':kind}),defaultValue=value(kind,data),description='Editable starter '+field))
    if 'rate' in s:users.append(dict(name='User.SpawnRate',type=dict(classStructOrEnum={'refPath':FLOAT}),defaultValue=value(FLOAT,{'value':s['rate']}),description='Particles per second'))
    call('AddUserVariables',system=obj(target),variablesToAdd=users)
    init='InitializeParticle'; spawn='ParticleSpawnScript'
    fields={x['name'] for m in top['particleSpawnScript']['modules'] if m['moduleName']==init for x in m['inputs'] if x['bIsVisible']}
    for field in ['Lifetime'] if 'Lifetime' in fields else ['Lifetime Min','Lifetime Max']:link(spawn,init,field,'Lifetime',FLOAT)
    link(spawn,init,'Color','Tint',COLOR)
    for f in ('Uniform Sprite Size Min','Uniform Sprite Size Max'):link(spawn,init,f,'Size',FLOAT)
    put(spawn,init,'Position Offset',value(VECTOR,dict(x=0.,y=0.,z=0.)))
    velocity=next((m for m in top['particleSpawnScript']['modules'] if m['moduleName'].startswith('AddVelocity')),None)
    if velocity:number(spawn,velocity['moduleName'],'Velocity Speed',s['speed'])
    shape=next((m for m in top['particleSpawnScript']['modules'] if m['moduleName'].startswith('ShapeLocation')),None)
    if shape:number(spawn,shape['moduleName'],'Sphere Radius',s['radius'])
    burst=next((m for m in top['emitterUpdateScript']['modules'] if m['moduleName'].startswith('SpawnBurst_Instantaneous')),None)
    if 'rate' in s:
        if burst:call('RemoveModule',moduleToRemove=ref('EmitterUpdateScript',burst['moduleName']))
        current=call('GetEmitterTopology',emitterRef=ref())
        rate=next((m for m in current['emitterUpdateScript']['modules'] if m['moduleName'].startswith('SpawnRate')),None)
        if not rate:rate=call('AddModule',moduleLocationRef=ref('EmitterUpdateScript'),moduleAsset={'refPath':'/Niagara/Modules/Emitter/SpawnRate.SpawnRate'})
        link('EmitterUpdateScript',rate['moduleName'],'SpawnRate','SpawnRate',FLOAT)
    elif burst:
        put('EmitterUpdateScript',burst['moduleName'],'Spawn Count',value('/Script/Niagara.NiagaraInt32',{'value':s['count']}))
    if name=='Smoke':
        # The starter no longer has the donor's Initial Random Int assignment.
        # Use the complete owned material atlas, not a dangling random-frame input
        # or geometry cut out from the donor's unrelated ThinSmoke texture.
        for field,n in [('Start Frame',0),('End Frame',63)]:
            put('ParticleUpdateScript','SubUVAnimation',field,value('/Script/Niagara.NiagaraInt32',{'value':n}))
        call('SetRendererData',renderer=ref(index=0),rendererData={'propertyValues':json.dumps({'CutoutTexture':{'refPath':''},'BoundingMode':'BVC_FourVertices'})})
    current=call('GetSystemSummary',system=obj(target))
    assert len(current['emitters'])==1 and current['emitters'][0]['emitterName']==emitter
    (OUT/(name+'.summary.json')).write_text(json.dumps(current,ensure_ascii=False,indent=2),encoding='utf8')
    print('AUTHORED',target,flush=True)
if __name__=='__main__':main()
