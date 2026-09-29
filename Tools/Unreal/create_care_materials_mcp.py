"""Build local tooth cleaning and blended tissue overlays with Epic's native MCP.

Textures are authored in Substance Painter. Artist enamel and tissue assets are preserved.
Run from the project root with Play-In-Editor stopped.
"""
import json,sys
from pathlib import Path
from native_mcp_client import NativeMCP

c=NativeMCP()
def call(ts,name,args):
    r=c.tool('call_tool',dict(toolset_name=ts,tool_name=name,arguments=args))
    if r.get('isError'): raise RuntimeError(r)
    v=json.loads(r['content'][0]['text'])
    if v.get('error'): raise RuntimeError(v)
    return v.get('returnValue')

if call('EditorToolset.EditorAppToolset','IsPIERunning',{}): raise RuntimeError('Stop PIE before authoring materials')
for path in Path('ArtSource/CareMaterials/Textures').glob('*.png'):
    name='T_'+path.stem.removeprefix('CareSwatches_MC_')
    path_in_game='/Game/Gameplay/Care/Textures/'+name
    if call('editor_toolset.toolsets.asset.AssetTools','exists',{'path':path_in_game}): texture={'refPath':path_in_game+'.'+name}
    else: texture=call('editor_toolset.toolsets.texture.TextureTools','import_file',dict(folder_path='/Game/Gameplay/Care/Textures',asset_name=name,source_file=str(path.resolve())))
    if isinstance(texture,list): texture=texture[0]
    values={'sRGB':path.stem.endswith('BaseColor')}
    if path.stem.endswith('Normal'): values['compressionSettings']='TC_Normalmap'
    elif path.stem.endswith('OcclusionRoughnessMetallic'): values['compressionSettings']='TC_Masks'
    if not call('editor_toolset.toolsets.object.ObjectTools','set_properties',dict(instance=texture,values=json.dumps(values))): raise RuntimeError('Texture properties: '+str(texture))
    call('editor_toolset.toolsets.asset.AssetTools','save_assets',dict(asset_paths=[texture['refPath']]))

plans=[]
nodes=[]; edges=[]; outputs=[]
def node(key,kind,**props):
    nodes.append(dict(key=key,kind=kind,props=props)); return key
def scalar(key,name,value): return node(key,'ScalarParameter',parameterName=name,defaultValue=value,group='Care')
def vector(key,name,value): return node(key,'VectorParameter',parameterName=name,defaultValue=dict(zip('rgba',value)),group='Care')
def tex(key,name,asset,linear=False): return node(key,'TextureObjectParameter',parameterName=name,texture={'refPath':asset},samplerType='SAMPLERTYPE_LinearColor' if linear else 'SAMPLERTYPE_Color')
def custom(key,code,inputs,typ='Float1'):
    node(key,'Custom',description='MC Care: '+key,code=code,outputType='CMOT_'+typ,inputs=[dict(inputName=k) for k in inputs])
    for pin,src in inputs.items(): edges.append([src[0],src[1],key,pin])
    return key
def out(key,prop): outputs.append([key,'MP_'+prop])

node('pos','PreSkinnedPosition'); node('p','VertexInterpolator'); edges.append(['pos','','p',''])
node('normal','PreSkinnedNormal'); node('n','VertexInterpolator'); edges.append(['normal','','n',''])
node('world','WorldPosition'); node('vertexWorldNormal','VertexNormalWS')
node('worldNormal','VertexInterpolator'); edges.append(['vertexWorldNormal','','worldNormal',''])
tex('wipe','GrimeWipe','/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture',True)
tex('pigment','GrimePigment','/Game/Gameplay/Care/Textures/T_Grime_BaseColor.T_Grime_BaseColor')
scalar('amount','GrimeAmount',0); scalar('age','BrushAge',4)
scalar('grimeSeed','GrimeSeed',1); scalar('thickness','GrimeThickness',3.5)
vector('minimum','GrimeMin',(-50,-50,-50,0)); vector('size','GrimeSize',(100,100,100,0)); vector('scale','GrimeScale',(1,1,1,0))
vector('brush','BrushLocal',(0,0,0,0)); vector('tint','GrimeColor',(.16,.034,.004,1))
scalar('damage','Damage',0); scalar('flash','HitFlash',0)
custom('pigmentColor','''
float3 w=pow(abs(N),4); w/=max(dot(w,1),.001);
return Texture2DSample(Tex,TexSampler,P.yz*.009).rgb*w.x+
       Texture2DSample(Tex,TexSampler,P.xz*.009).rgb*w.y+
       Texture2DSample(Tex,TexSampler,P.xy*.009).rgb*w.z;
''',{'P':('p',''),'N':('n',''),'Tex':('pigment','')},'Float3')
noise='''
struct Noise {
 float hash(float3 p) { return frac(sin(dot(p,float3(127.1,311.7,74.7)))*43758.5453); }
 float value(float3 p) {
  float3 i=floor(p), f=frac(p); f=f*f*(3-2*f);
  return lerp(lerp(lerp(hash(i),hash(i+float3(1,0,0)),f.x),lerp(hash(i+float3(0,1,0)),hash(i+float3(1,1,0)),f.x),f.y),
              lerp(lerp(hash(i+float3(0,0,1)),hash(i+float3(1,0,1)),f.x),lerp(hash(i+float3(0,1,1)),hash(i+1),f.x),f.y),f.z);
 }
}; Noise S;
'''
custom('localFilm',noise+'''
float3 uv=saturate((P-Minimum)/max(Size,.01))*15;
float z0=floor(uv.z),z1=min(z0+1,15);
float2 a=(uv.xy+float2(fmod(z0,4),floor(z0/4))*16+.5)/64;
float2 b=(uv.xy+float2(fmod(z1,4),floor(z1/4))*16+.5)/64;
float wipe=lerp(Texture2DSampleLevel(Wipe,WipeSampler,a,0).r,Texture2DSampleLevel(Wipe,WipeSampler,b,0).r,frac(uv.z));
float3 q=P+Seed*float3(13.7,21.3,8.1);
float grain=S.value(q*float3(.055,.052,.036)+4);
float streak=S.value(q*float3(.12,.11,.022)+19);
float flecks=S.value(q*.20);
float h=uv.z/15;
float band=smoothstep(.10,.25,h)*(1-smoothstep(.63,.84,h));
float field=grain*.67+streak*.23+flecks*.10-(1-band)*.30;
float threshold=lerp(.63,.51,Amount);
float patch=smoothstep(threshold,threshold+.045,field);
float dirt=0; // Visible coating is the continuous projected relief mesh.
float distance=length((P-Brush)*Scale);
float foam=(1-smoothstep(23,45,distance))*(1-smoothstep(.25,1.4,Age));
float bubbles=smoothstep(.23,.49,S.value(P*.65));
foam*=lerp(.72,1,bubbles);
float wet=(1-wipe)*patch;
float crack=1-smoothstep(.025,.075,abs(sin(P.z*.044+P.y*.029+sin(P.y*.055)*.8)));
crack*=smoothstep(.15,.65,Damage)*smoothstep(-20,50,P.z);
return float4(dirt,foam,wet,crack);
''',{'P':('p',''),'Minimum':('minimum',''),'Size':('size',''),'Scale':('scale',''),'Wipe':('wipe',''),
     'Amount':('amount',''),'Age':('age',''),'Brush':('brush',''),'Damage':('damage',''),'Seed':('grimeSeed','')},'Float4')
custom('cleaningColor','''
float3 pigment=Tint*lerp(.62,1.60,saturate(Pigment.r*2.5));
pigment*=lerp(1.3,.83,smoothstep(.35,1,F.x));
float3 color=lerp(Enamel*float3(1,.98,.94),pigment,F.x);
color=lerp(color,float3(.075,.024,.012),F.w*.85);
color=lerp(color,float3(.91,.98,.95),F.y*.95);
return lerp(color,float3(1,.3,.08),saturate(Flash)*.55);
''',{'Enamel':('artistColor','RGB'),'F':('localFilm',''),'Pigment':('pigmentColor',''),'Tint':('tint',''),'Flash':('flash','')},'Float3'); out('cleaningColor','BaseColor')
custom('cleaningRoughness','return lerp(lerp(lerp(clamp(Base,.16,.24),.12,F.x),.12,F.z*.6),.42,F.y);',{'Base':('artistRough',''),'F':('localFilm','')}); out('cleaningRoughness','Roughness')
scalar('filmSpecular','Specular',.65); out('filmSpecular','Specular')
custom('filmHeight',noise+'''
float bead=smoothstep(.39,.67,S.value(P*.18+Seed));
float broad=smoothstep(.30,.68,S.value(P*.060+Seed*2.7));
return pow(saturate(F.x),.62)*Thickness*(.25+.30*broad+1.10*bead)+F.y*.35;
''',{'P':('p',''),'F':('localFilm',''),'Seed':('grimeSeed',''),'Thickness':('thickness','')})
custom('filmNormal','''
float3 n=normalize(N);
float3 dx=ddx(World),dy=ddy(World);
float3 rx=cross(dy,n),ry=cross(n,dx);
float det=dot(dx,rx);
float3 slope=(ddx(Height)*rx+ddy(Height)*ry)*sign(det)/max(abs(det),1e-5);
return normalize(n-slope);
''',{'World':('world',''),'N':('worldNormal',''),'Height':('filmHeight','')},'Float3'); out('filmNormal','Normal')
plans.append(dict(name='M_ArenaToothCare',source='/Game/Art/Materials/M_Enamel',nodes=nodes,edges=edges,outputs=outputs,properties={'bTangentSpaceNormal':False}))
# The enamel stays clean outside the two connected coatings. Their geometry
# carries contour coverage and meniscus depth; the replicated wipe volume
# removes the same local contact on every client.
nodes=[]; edges=[]; outputs=[]
node('pos','PreSkinnedPosition'); node('p','VertexInterpolator'); edges.append(['pos','','p',''])
node('vc','VertexColor')
node('normal','PreSkinnedNormal'); node('n','VertexInterpolator'); edges.append(['normal','','n',''])
tex('wipe','GrimeWipe','/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture',True)
tex('pigment','GrimePigment','/Game/Gameplay/Care/Textures/T_Grime_BaseColor.T_Grime_BaseColor')
scalar('amount','GrimeAmount',0); scalar('age','BrushAge',4)
vector('minimum','GrimeMin',(-50,-50,-50,0)); vector('size','GrimeSize',(100,100,100,0)); vector('scale','GrimeScale',(1,1,1,0))
vector('brush','BrushLocal',(0,0,0,0)); vector('tint','GrimeColor',(.16,.034,.004,1))
custom('coatingMask','''float3 uv=saturate((P-Minimum)/max(Size,.01))*15;
float z0=floor(uv.z),z1=min(z0+1,15);
float2 a=(uv.xy+float2(fmod(z0,4),floor(z0/4))*16+.5)/64;
float2 b=(uv.xy+float2(fmod(z1,4),floor(z1/4))*16+.5)/64;
float wipe=lerp(Texture2DSampleLevel(Wipe,WipeSampler,a,0).r,Texture2DSampleLevel(Wipe,WipeSampler,b,0).r,frac(uv.z));
float foam=(1-smoothstep(23,45,length((P-Brush)*Scale)))*(1-smoothstep(.25,1.4,Age));
return float4(V.r*wipe*saturate(Amount*2),foam,V.b,V.g);
''',{'P':('p',''),'V':('vc',''),'Minimum':('minimum',''),'Size':('size',''),'Scale':('scale',''),'Wipe':('wipe',''),'Amount':('amount',''),'Age':('age',''),'Brush':('brush','')},'Float4')
custom('coatingColor','''float3 w=pow(abs(N),4); w/=max(dot(w,1),.001);
float pigment=Texture2DSample(Tex,TexSampler,P.yz*.009).r*w.x+Texture2DSample(Tex,TexSampler,P.xz*.009).r*w.y+Texture2DSample(Tex,TexSampler,P.xy*.009).r*w.z;
float3 color=Tint*lerp(1.55,.72,F.z)*lerp(.88,1.15,F.w)*lerp(.9,1.15,saturate(pigment*3));
return lerp(color,float3(.92,.97,.94),F.y*.96);
''',{'P':('p',''),'N':('n',''),'Tex':('pigment',''),'Tint':('tint',''),'F':('coatingMask','')},'Float3'); out('coatingColor','BaseColor')
custom('coatingRoughness','return lerp(.09+F.w*.025,.35,F.y);',{'F':('coatingMask','')}); out('coatingRoughness','Roughness')
custom('coatingClip','return F.x;',{'F':('coatingMask','')}); out('coatingClip','OpacityMask')
scalar('specular','Specular',.65); out('specular','Specular')
node('reliefWorld','WorldPosition'); node('reliefNormal','VertexNormalWS'); node('reliefN','VertexInterpolator'); edges.append(['reliefNormal','','reliefN',''])
custom('meniscusNormal','''float3 n=normalize(N);
float height=F.z*.8;
float3 dx=ddx(World),dy=ddy(World),rx=cross(dy,n),ry=cross(n,dx);
float det=dot(dx,rx);
return normalize(n-(ddx(height)*rx+ddy(height)*ry)*sign(det)/max(abs(det),1e-5));
''',{'World':('reliefWorld',''),'N':('reliefN',''),'F':('coatingMask','')},'Float3'); out('meniscusNormal','Normal')
plans.append(dict(name='M_ArenaToothRelief',nodes=nodes,edges=edges,outputs=outputs,properties={'blendMode':'BLEND_Masked','opacityMaskClipValue':.25,'bTangentSpaceNormal':False,'shadingModel':'MSM_DefaultLit'}))

nodes=[]; edges=[]; outputs=[]
node('uv','TextureCoordinate'); node('time','Time')
tex('tissue','TissueVariation','/Game/Gameplay/Care/Textures/T_Ulcer_BaseColor.T_Ulcer_BaseColor')
scalar('healing','Healing',0); scalar('disturbed','Disturbed',0); scalar('seed','Seed',1)
custom('ulcerField','''
float2 p=(UV-.5)*2;
float a=atan2(p.y,p.x);
float irregular=1+.065*sin(a*3+Seed)+.035*sin(a*7-Seed*.71);
float pulse=1+Disturbed*.025*sin(Time*10);
float r=length(p*float2(1,1.05))/(irregular*(1-Healing*.20)*pulse);
float edge=1-smoothstep(.69,.97,r);
float center=1-smoothstep(.38,.60,r);
float rim=exp(-pow((r-.62)/.10,2));
float alpha=edge*(1-smoothstep(.25,1,Healing));
return float4(center,rim,alpha,r);
''',{'UV':('uv',''),'Healing':('healing',''),'Disturbed':('disturbed',''),'Seed':('seed',''),'Time':('time','')},'Float4')
custom('ulcerColor','''
float3 tissueSample=Texture2DSample(Tissue,TissueSampler,UV*1.7+Seed*.013).rgb;
float3 rim=lerp(float3(.64,.065,.09),float3(.78,.105,.12),Disturbed)*(.82+tissueSample.r*.32);
float3 center=float3(.90,.64,.36)*(.88+tissueSample.r*.16);
return lerp(rim,center,F.x);
''',{'UV':('uv',''),'Tissue':('tissue',''),'F':('ulcerField',''),'Seed':('seed',''),'Disturbed':('disturbed','')},'Float3'); out('ulcerColor','BaseColor')
custom('ulcerOpacity','return F.z*lerp(.62,.96,saturate(F.x+F.y));',{'F':('ulcerField','')}); out('ulcerOpacity','Opacity')
custom('ulcerRoughness','return lerp(.30,.22,F.x);',{'F':('ulcerField','')}); out('ulcerRoughness','Roughness')
scalar('specular','Specular',.55); out('specular','Specular')
plans.append(dict(name='M_UlcerBlend',nodes=nodes,edges=edges,outputs=outputs,properties={'materialDomain':'MD_DeferredDecal','blendMode':'BLEND_Translucent','shadingModel':'MSM_DefaultLit'}))

if len(sys.argv)>1: plans=[p for p in plans if p['name'] in sys.argv[1:]]

script='''import json
M="editor_toolset.toolsets.material.MaterialTools."
O="editor_toolset.toolsets.object.ObjectTools."
A="editor_toolset.toolsets.asset.AssetTools."
def call(name,args):
 r=execute_tool(name,json.dumps(args))
 if "error" in r: raise RuntimeError(str(r))
 return r["returnValue"]
def props(obj,values):
 if "inputs" in values:
  current=json.loads(call(O+"get_properties",{"instance":obj,"properties":["inputs"]}))["inputs"]
  if len(current)<len(values["inputs"]): call(O+"set_properties",{"instance":obj,"values":json.dumps({"inputs":current+values["inputs"][len(current):]})})
 if not call(O+"set_properties",{"instance":obj,"values":json.dumps(values)}): raise RuntimeError("Properties: "+str(obj))
def run():
 result=[]
 for plan in json.loads(PLANS):
  path="/Game/Gameplay/Care/"+plan["name"]
  mat={"refPath":path+"."+plan["name"]}
  if not call(A+"exists",{"path":path}):
   if "source" in plan: call(A+"duplicate",{"path":plan["source"],"new_path":path})
   else: call(M+"create_material",{"folder_path":"/Game/Gameplay/Care","asset_name":plan["name"]})
  made={}
  preserved=[]
  if "source" in plan:
   for key,prop in [("artistColor","MP_BaseColor"),("artistRough","MP_Roughness")]:
    inp=call(M+"get_property_input",{"material":{"refPath":plan["source"]+"."+plan["source"].split("/")[-1]},"material_property":prop})
    ref=mat["refPath"]+":"+inp["expression"]["refPath"].split(":")[-1]
    made[key]={"refPath":ref}; preserved.append(ref)
  for e in call(M+"get_expressions",{"material_or_function":mat}):
   if e["refPath"] not in preserved: call(M+"delete_expression",{"material_or_function":mat,"expression":e})
  if plan["properties"]: props(mat,plan["properties"])
  for i,n in enumerate(plan["nodes"]):
   obj=call(M+"add_expression",{"material_or_function":mat,"expression_class":{"refPath":"/Script/Engine.MaterialExpression"+n["kind"]},"x":(i%5)*430,"y":400+(i//5)*240})
   if n["props"]: props(obj,n["props"])
   made[n["key"]]=obj
  for a,ap,b,bp in plan["edges"]: call(M+"connect_expressions",{"from_expression":made[a],"from_output_name":ap,"to_expression":made[b],"to_input_name":bp})
  for a,p in plan["outputs"]: call(M+"connect_to_output",{"expression":made[a],"output_name":"","material_property":p})
  call(M+"recompile",{"material_or_function":mat})
  if not call(A+"save_assets",{"asset_paths":[path]}): raise RuntimeError("Could not save "+path)
  result.append({"material":mat,"expressions":len(made)})
 return {"materials":result}
'''.replace('PLANS',repr(json.dumps(plans)))
call('editor_toolset.toolsets.programmatic.ProgrammaticToolset','get_execution_environment',{})
r=[]
for plan in plans:
    part=script.replace(repr(json.dumps(plans)),repr(json.dumps([plan])))
    r.append(call('editor_toolset.toolsets.programmatic.ProgrammaticToolset','execute_tool_script',{'script':part}))
Path('Saved/CareMaterialBuild.json').write_text(json.dumps(r,indent=2))
print(json.dumps(r,indent=2))
