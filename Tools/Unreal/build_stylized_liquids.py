"""Reusable stylized optical surface: coffee and clear water presets.

Preserves the runtime contract and server-shared macro wave height. No UI input.
"""
from pathlib import Path
import json
import unreal as u

lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
root=Path(u.Paths.project_dir()).resolve()
folder='/Game/Gameplay/Liquid/Stylized'
lib.make_directory(folder)
assert not u.EditorLevelLibrary.get_pie_worlds(False)
for name,normal in [('T_LiquidRipples_N',True),('T_LiquidDetail_M',False)]:
    task=u.AssetImportTask(); task.filename=str(root/'ArtSource/Fluids'/(name+'.png')); task.destination_path=folder
    task.destination_name=name; task.automated=True; task.replace_existing=True; task.save=True
    assets.import_asset_tasks([task])
    tex=lib.load_asset(folder+'/'+name); tex.set_editor_property('srgb',False)
    tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP if normal else u.TextureCompressionSettings.TC_MASKS)
    tex.set_editor_property('address_x',u.TextureAddress.TA_WRAP); tex.set_editor_property('address_y',u.TextureAddress.TA_WRAP)
    lib.save_loaded_asset(tex,only_if_is_dirty=False)

path=folder+'/M_StylizedLiquidSurface'
# Rebuilding starts from the original height contract; replace only this owned asset.
if lib.does_asset_exist(path):
    mat=lib.load_asset(path)
else: mat=lib.duplicate_asset('/Game/Art/Materials/M_CoffeeSurface',path)
assert mat
mat.set_editor_property('translucency_lighting_mode',u.TranslucencyLightingMode.TLM_SURFACE)
def nodes(): return list(edit.get_material_expressions(mat))
def described(name):
    aliases={'Rich coffee body, golden thin edges, pale aerated crema':'Stylized depth gradient, Fresnel body and porous foam'}
    return next(n for n in nodes() if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description') in (name,aliases.get(name)))
def scalar(name,value,group='Optics'):
    n=next((n for n in nodes() if isinstance(n,u.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name'))==name),None)
    if n is None: n=edit.create_material_expression(mat,u.MaterialExpressionScalarParameter)
    n.set_editor_property('parameter_name',name); n.set_editor_property('default_value',value); n.set_editor_property('group',group)
    return n
def vector(name,value,group='Optics'):
    n=next((n for n in nodes() if isinstance(n,u.MaterialExpressionVectorParameter) and str(n.get_editor_property('parameter_name'))==name),None)
    if n is None: n=edit.create_material_expression(mat,u.MaterialExpressionVectorParameter)
    n.set_editor_property('parameter_name',name); n.set_editor_property('default_value',u.LinearColor(*value)); n.set_editor_property('group',group)
    return n
def add_input(custom,name,source,pin=''):
    entries=list(custom.get_editor_property('inputs'))
    if name not in [str(i.get_editor_property('input_name')) for i in entries]:
        inp=u.CustomInput(); inp.set_editor_property('input_name',name); entries.append(inp); custom.set_editor_property('inputs',entries)
    assert edit.connect_material_expressions(source,pin,custom,name)
def custom(name,code,inputs,dim):
    n=next((n for n in nodes() if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description')==name),None)
    if n is None: n=edit.create_material_expression(mat,u.MaterialExpressionCustom)
    n.set_editor_property('description',name); n.set_editor_property('code',code)
    n.set_editor_property('output_type',{1:u.CustomMaterialOutputType.CMOT_FLOAT1,2:u.CustomMaterialOutputType.CMOT_FLOAT2,3:u.CustomMaterialOutputType.CMOT_FLOAT3}[dim])
    for name,(source,pin) in inputs.items(): add_input(n,name,source,pin)
    return n
def output(n,prop): assert edit.connect_material_property(n,'',prop)
parameters={str(n.get_editor_property('parameter_name')):n for n in nodes() if isinstance(n,(u.MaterialExpressionScalarParameter,u.MaterialExpressionVectorParameter))}
position=next(n for n in nodes() if isinstance(n,u.MaterialExpressionWorldPosition))
uv1=custom('World flow UV, broad ripples','''
float2 from=P.xy-Inlet.xy, to=Outlet.xy-P.xy;
float2 flow=normalize(lerp(from,to,DrainAmount)+float2(.001,.001));
return P.xy/TileSize-flow*WaterTime*FlowSpeed/TileSize;
''',{'P':(position,''),'Inlet':(parameters['Inlet'],'RGBA'),'Outlet':(parameters['Outlet'],'RGBA'),'DrainAmount':(parameters['DrainAmount'],''),'WaterTime':(parameters['WaterTime'],''),'TileSize':(scalar('RippleTileSize',310),''),'FlowSpeed':(scalar('OpticalFlowSpeed',34),'')},2)
uv2=custom('Crossing fine ripple UV','return float2(-UV.y,UV.x)*1.73+float2(Time*.009,-Time*.013);',{'UV':(uv1,''),'Time':(parameters['WaterTime'],'')},2)
def sample(name,texture,uv,sampler):
    n=next((n for n in nodes() if isinstance(n,u.MaterialExpressionTextureSampleParameter2D) and str(n.get_editor_property('parameter_name'))==name),None)
    if n is None: n=edit.create_material_expression(mat,u.MaterialExpressionTextureSampleParameter2D)
    n.set_editor_property('parameter_name',name); n.set_editor_property('texture',texture); n.set_editor_property('sampler_type',sampler)
    assert edit.connect_material_expressions(uv,'',n,'UVs')
    return n
texture=lib.load_asset(folder+'/T_LiquidRipples_N')
n1=sample('BroadRippleNormal',texture,uv1,u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
n2=sample('FineRippleNormal',texture,uv2,u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
cells=sample('FoamFlowDetail',lib.load_asset(folder+'/T_LiquidDetail_M'),uv1,u.MaterialSamplerType.SAMPLERTYPE_MASKS)
normal=described('Analytic surface slopes and directional small ripples')
originals=list(edit.get_material_expressions(lib.load_asset('/Game/Art/Materials/M_CoffeeSurface')))
def original_code(name): return next(n.get_editor_property('code') for n in originals if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description')==name)
normal_base=original_code('Analytic surface slopes and directional small ripples').split('float2 flow=')[0]
normal.set_editor_property('code',normal_base+'''
// World-projected, seamless two-scale ripple normals, with independent flow.
slope-=(N1.xy+float2(-N2.y,N2.x)*.50)*NormalDetail;
slope+=from/d*JetAmount*.055*cos(d*.075-WaterTime*9)*exp(-d/650);
return normalize(float3(-slope,1));
''')
add_input(normal,'N1',n1,'RGB'); add_input(normal,'N2',n2,'RGB')
foam=described('Advected microfoam, impact froth, wave crest and player wakes')
base=original_code('Advected microfoam, impact froth, wave crest and player wakes').split('float2 flow=')[0]
foam.set_editor_property('code',base+'''
float cloud=smoothstep(.28,.73,Cells.g);
float porous=smoothstep(.32,.80,Cells.a);
float rim=pow(saturate(1-Shore),.72)*(.50+.50*cloud);
float froth=JetAmount*exp(-pow((d-145)/145,2))*(.35+.65*cloud);
float crest=Filling*exp(-band*band*1.4)*exp(-d/1800)*(.20+.80*cloud);
float drain=DrainAmount*exp(-r/480)*pow(saturate(.5+.5*sin(r*.042+WaterTime*9+3*atan2(to.y,to.x)+Cells.b*4)),5)*.22;
float wake=0; float4 points[4]={Wake0,Wake1,Wake2,Wake3};
[unroll] for(int i=0;i<4;i++) {
    [branch] if(points[i].z>.001) {
        float wd=length(P.xy-points[i].xy);
        wake+=pow(saturate(.5+.5*sin(wd*.085-WaterTime*5+Cells.b)),5)*exp(-wd/95)*smoothstep(12,28,wd)*points[i].z*points[i].w;
    }
}
// Sparse soft patches, without sharply cut Voronoi junctions across the pool.
float islands=pow(saturate((Cells.g-.58)*3),2)*smoothstep(.2,.7,Cells.b)*.05;
return saturate(FoamAmount*(rim+froth*.88+crest*.62+drain+WakeStrength*wake+islands));
''')
add_input(foam,'Cells',cells,'RGBA')

color=described('Rich coffee body, golden thin edges, pale aerated crema')
color.set_editor_property('description','Stylized depth gradient, Fresnel body and porous foam')
color.set_editor_property('code','''
float thickness=pow(saturate(Depth),DepthContrast);
float3 body=lerp(Shallow.rgb,Deep.rgb,thickness);
body*=.96+.08*Cells.b;
return lerp(body,FoamColor.rgb,Foam);
''')
add_input(color,'DepthContrast',scalar('DepthContrast',.72)); add_input(color,'Cells',cells,'RGBA')

# The engine function reconstructs the floor's world position. Caustics remain
# attached to submerged geometry when the camera or water surface moves.
behind=next((n for n in nodes() if isinstance(n,u.MaterialExpressionMaterialFunctionCall) and n.get_editor_property('desc')=='Submerged world position'),None)
if behind is None:
    behind=edit.create_material_expression(mat,u.MaterialExpressionMaterialFunctionCall)
    behind.set_editor_property('desc','Submerged world position')
    assert behind.set_material_function(lib.load_asset('/Engine/Functions/Engine_MaterialFunctions02/UVs/WorldPositionBehindTranslucency'))
caustic_uv=custom('Submerged caustic projection','return Bottom.xy/CausticSize+float2(Time*.009,-Time*.006);',{'Bottom':(behind,''),'CausticSize':(scalar('CausticTileSize',230),''),'Time':(parameters['WaterTime'],'')},2)
caustic_a=sample('CausticPattern',lib.load_asset(folder+'/T_LiquidDetail_M'),caustic_uv,u.MaterialSamplerType.SAMPLERTYPE_MASKS)
caustic_uv2=custom('Second caustic phase','return float2(UV.y,-UV.x)*1.11+float2(Time*.011,Time*.006);',{'UV':(caustic_uv,''),'Time':(parameters['WaterTime'],'')},2)
caustic_b=sample('CausticPatternFine',lib.load_asset(folder+'/T_LiquidDetail_M'),caustic_uv2,u.MaterialSamplerType.SAMPLERTYPE_MASKS)
view=next((n for n in nodes() if isinstance(n,u.MaterialExpressionCameraVectorWS)),None)
if view is None: view=edit.create_material_expression(mat,u.MaterialExpressionCameraVectorWS)
depth_input=next(n for n in nodes() if isinstance(n,u.MaterialExpressionDepthFade) and n.get_editor_property('fade_distance_default')>100)
fresnel=next(n for n in nodes() if isinstance(n,u.MaterialExpressionFresnel))
emission=custom('Moving wet glints and projected underwater caustics','''
float3 reflected=reflect(-normalize(View),normalize(Normal));
float key=pow(saturate(dot(reflected,normalize(KeyDirection.xyz))),HighlightPower);
float fill=pow(saturate(dot(reflected,normalize(float3(-.42,.56,.72)))),110)*.28;
float sparkle=.55+.45*smoothstep(.23,.74,Cells.b);
float glint=(key+fill)*sparkle*HighlightStrength;
float shallow=1-smoothstep(.12,.90,Depth);
float pattern=saturate(CausticA.r*.62+CausticB.r*.55);
// Project onto the floor; prevent vertical teeth from acquiring bright wires.
float3 bottomNormal=normalize(cross(ddx(Bottom),ddy(Bottom))+float3(0,0,.00001));
float floorMask=smoothstep(.20,.72,abs(bottomNormal.z));
pattern=pow(pattern,1.45)*shallow*CausticStrength*(1-Foam)*floorMask;
float3 caustic=CausticTint.rgb*pattern;
return Color*AmbientFill+HighlightColor.rgb*glint*(1-Foam*.80)+ReflectionTint.rgb*Fresnel*ReflectionStrength+caustic;
''',{'View':(view,''),'Normal':(normal,''),'Color':(color,''),'Foam':(foam,''),'Depth':(depth_input,''),'Fresnel':(fresnel,''),'Cells':(cells,'RGBA'),'CausticA':(caustic_a,'RGBA'),'CausticB':(caustic_b,'RGBA'),
    'AmbientFill':(parameters['AmbientFill'],''),'KeyDirection':(vector('HighlightDirection',(.92,-.08,.385,0)), 'RGBA'),'HighlightPower':(scalar('HighlightPower',110),''),'HighlightStrength':(scalar('HighlightStrength',1.8),''),'HighlightColor':(vector('HighlightColor',(.92,.96,1,1)),'RGB'),
    'ReflectionTint':(vector('ReflectionTint',(.015,.065,.09,1)),'RGB'),'ReflectionStrength':(scalar('ReflectionStrength',.25),''),'CausticTint':(vector('CausticTint',(.08,.40,.36,1)),'RGB'),'CausticStrength':(scalar('CausticStrength',0),''),'Bottom':(behind,'')},3)
output(emission,u.MaterialProperty.MP_EMISSIVE_COLOR)
assert not edit.recompile_material(mat)
assert lib.save_loaded_asset(mat,only_if_is_dirty=False)
def preset(name,parent,scalars,vectors):
    path=folder+'/'+name
    mi=lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset(name,folder,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(mi,None); edit.set_material_instance_parent(mi,parent)
    for k,v in scalars.items(): edit.set_material_instance_scalar_parameter_value(mi,k,v)
    for k,v in vectors.items(): edit.set_material_instance_vector_parameter_value(mi,k,u.LinearColor(*v))
    edit.update_material_instance(mi); assert lib.save_loaded_asset(mi,only_if_is_dirty=False)
    return mi
coffee=preset('MI_StylizedCoffee',mat,dict(NormalDetail=.24,DepthTintDistance=110,DepthContrast=.75,ShallowOpacity=.72,DeepOpacity=.97,Roughness=.11,AmbientFill=.40,FoamAmount=.78,ShoreFoamWidth=32,HighlightStrength=1.35,HighlightPower=420,ReflectionStrength=.16,CausticStrength=0),dict(ShallowColor=(.34,.145,.040,1),DeepColor=(.11,.043,.012,1),FoamColor=(.72,.49,.24,1),HighlightColor=(1,.94,.83,1),ReflectionTint=(.10,.07,.036,1)))

# Clear liquid needs coloured transmission through the surface. The dense
# coffee surface keeps its scattering model; both share textures and uniforms.
clear_path=folder+'/M_ClearLiquidSurface'
if lib.does_asset_exist(clear_path):
    clear=lib.load_asset(clear_path)
else: clear=lib.duplicate_asset(path,clear_path)
opaque_master=mat
mat=clear
# Keep both optical masters in sync on subsequent runs, including the shared
# server height expression. Clear-only transmission nodes remain independent.
for source in edit.get_material_expressions(opaque_master):
    if isinstance(source,u.MaterialExpressionCustom):
        name=source.get_editor_property('description')
        dest=next((n for n in nodes() if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description')==name),None)
        if dest: dest.set_editor_property('code',source.get_editor_property('code'))
clear_behind=next(n for n in nodes() if isinstance(n,u.MaterialExpressionMaterialFunctionCall) and n.get_editor_property('desc')=='Submerged world position')
add_input(described('Moving wet glints and projected underwater caustics'),'Bottom',clear_behind)
mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_THIN_TRANSLUCENT)
mat.set_editor_property('translucency_lighting_mode',u.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
clear_depth=next(n for n in nodes() if isinstance(n,u.MaterialExpressionDepthFade) and n.get_editor_property('fade_distance_default')>100)
clear_foam=described('Advected microfoam, impact froth, wave crest and player wakes')
transmit=custom('Depth-dependent coloured transmission','return lerp(Shallow.rgb,Deep.rgb,pow(saturate(Depth),.72))*(1-Foam*.93);',{'Depth':(clear_depth,''),'Foam':(clear_foam,''),'Shallow':(vector('ShallowTransmission',(.82,.96,.99,1)),'RGB'),'Deep':(vector('DeepTransmission',(.42,.76,.92,1)),'RGB')},3)
thin=next((n for n in nodes() if isinstance(n,u.MaterialExpressionThinTranslucentMaterialOutput)),None)
if thin is None: thin=edit.create_material_expression(mat,u.MaterialExpressionThinTranslucentMaterialOutput)
assert edit.connect_material_expressions(transmit,'',thin,'TransmittanceColor')
old_opacity=described('Wet front expands from inlet; softened arena and object intersections')
coverage=custom('Visible liquid footprint and contact fade','return saturate(Edge*Border*Wet);',{
    'Edge':(next(n for n in nodes() if isinstance(n,u.MaterialExpressionDepthFade) and n.get_editor_property('fade_distance_default')==16),''),
    'Border':(scalar('Coverage',1),''),'Wet':(scalar('CoverageWet',1),'')},1)
# Use the original front/border expression for full footprint coverage, but
# remove optical density. Runtime values still hide dry areas as before.
coverage.set_editor_property('code',old_opacity.get_editor_property('code').split('return saturate(')[0]+'return Edge*border*wet;')
for inp in old_opacity.get_editor_property('inputs'):
    name=str(inp.get_editor_property('input_name'))
    # Input structs are not exposed reliably; resolve the uniform by name.
    source=next((n for n in nodes() if isinstance(n,(u.MaterialExpressionScalarParameter,u.MaterialExpressionVectorParameter)) and str(n.get_editor_property('parameter_name'))==name),None)
    if source: add_input(coverage,name,source,'RGBA' if isinstance(source,u.MaterialExpressionVectorParameter) else '')
add_input(coverage,'P',next(n for n in nodes() if isinstance(n,u.MaterialExpressionWorldPosition)))
assert edit.connect_material_expressions(coverage,'',thin,'SurfaceCoverage')
assert not edit.recompile_material(mat); assert lib.save_loaded_asset(mat,only_if_is_dirty=False)
water=preset('MI_ClearWater',mat,dict(NormalDetail=.22,DepthTintDistance=360,DepthContrast=.72,ShallowOpacity=.42,DeepOpacity=.86,Roughness=.10,AmbientFill=1.35,FoamAmount=1.15,ShoreFoamWidth=44,HighlightStrength=2.0,HighlightPower=540,ReflectionStrength=.70,CausticStrength=1.05,CausticTileSize=430,Refraction=1.013),dict(ShallowColor=(.024,.62,.43,1),DeepColor=(.012,.18,.43,1),FoamColor=(.82,.96,1,1),HighlightColor=(.88,.96,1,1),ReflectionTint=(.05,.22,.38,1),CausticTint=(.14,.58,.42,1),ShallowTransmission=(.82,.96,.99,1),DeepTransmission=(.42,.76,.92,1)))
mat=opaque_master
water_pour=preset('MI_ClearWaterPour',lib.load_asset('/Game/Art/Materials/M_CoffeePour'),dict(Opacity=.59,Roughness=.07,AmbientFill=.48,NormalDetail=.26,Refraction=1.015,SilhouetteRipple=3.8),dict(BodyColor=(.012,.16,.22,1),ThinColor=(.03,.47,.50,1),CremaColor=(.84,.97,1,1)))
water_drops=preset('MI_ClearWaterDrops',lib.load_asset('/Game/Art/Materials/M_CoffeeDrops'),dict(Roughness=.06,Specular=.85,AmbientFill=.45),dict(BodyColor=(.015,.14,.22,1),RimColor=(.04,.54,.59,1)))
profile=lib.load_asset('/Game/Data/DA_CoffeeWater')
profile.set_editor_property('surface_material',coffee); assert lib.save_loaded_asset(profile,only_if_is_dirty=False)
water_profile_path='/Game/Data/DA_ClearWater'
water_profile=lib.load_asset(water_profile_path) if lib.does_asset_exist(water_profile_path) else lib.duplicate_asset('/Game/Data/DA_CoffeeWater',water_profile_path)
for name in ['settings','surface_mesh','jet_mesh','crown_mesh','drop_mesh','drain_mesh']:
    water_profile.set_editor_property(name,profile.get_editor_property(name))
water_profile.set_editor_property('surface_material',water)
water_profile.set_editor_property('pour_material',water_pour)
water_profile.set_editor_property('drop_material',water_drops)
assert lib.save_loaded_asset(water_profile,only_if_is_dirty=False)
# Keep the historical surface MI useful when a designer assigns it directly.
old=lib.load_asset('/Game/Art/Materials/MI_CoffeeSurface')
edit.set_material_instance_parent(old,mat)
old.set_editor_property('scalar_parameter_values',coffee.get_editor_property('scalar_parameter_values'))
old.set_editor_property('vector_parameter_values',coffee.get_editor_property('vector_parameter_values'))
edit.update_material_instance(old); assert lib.save_loaded_asset(old,only_if_is_dirty=False)
edit.layout_material_expressions(mat); lib.save_loaded_asset(mat,only_if_is_dirty=False)
report=dict(master=mat.get_path_name(),clear_master=clear.get_path_name(),coffee=coffee.get_path_name(),water=water.get_path_name(),water_pour=water_pour.get_path_name(),water_drops=water_drops.get_path_name(),water_profile=water_profile.get_path_name(),runtime_height_unchanged=True)
Path(u.Paths.project_saved_dir(),'StylizedLiquidAssets.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_STYLIZED_LIQUID_ASSETS_PASS')
