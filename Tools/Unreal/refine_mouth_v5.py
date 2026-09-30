"""Rebuild mucosa shading with a dedicated scatter profile and restrained wet film."""
import json
from pathlib import Path
import unreal as u
lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary; assets=u.AssetToolsHelpers.get_asset_tools()
assert not u.EditorLevelLibrary.get_pie_worlds(False)
root=Path(u.Paths.project_dir()).resolve()
def save(a): assert lib.save_loaded_asset(a,only_if_is_dirty=False),a.get_path_name()
def node(m,cls,**kw):
    n=edit.create_material_expression(m,cls)
    for k,v in kw.items(): n.set_editor_property(k,v)
    return n
def scalar(m,name,value): return node(m,u.MaterialExpressionScalarParameter,parameter_name=name,default_value=value)
def custom(m,name,code,inputs,dim=3):
    n=node(m,u.MaterialExpressionCustom,description=name,code=code,output_type={1:u.CustomMaterialOutputType.CMOT_FLOAT1,3:u.CustomMaterialOutputType.CMOT_FLOAT3}[dim])
    entries=[]
    for key in inputs:
        entry=u.CustomInput();entry.set_editor_property("input_name",key);entries.append(entry)
    n.set_editor_property("inputs",entries)
    for key,(src,pin) in inputs.items(): assert edit.connect_material_expressions(src,pin,n,key)
    return n
def out(n,prop,pin=''): assert edit.connect_material_property(n,pin,prop)
profile_path='/Game/Gameplay/MouthV3/SP_Mucosa'
profile=lib.load_asset(profile_path) if lib.does_asset_exist(profile_path) else assets.create_asset('SP_Mucosa','/Game/Gameplay/MouthV3',u.SubsurfaceProfile,u.SubsurfaceProfileFactory())
s=profile.get_editor_property('settings')
for key,value in dict(enable_burley=True,enable_mean_free_path=True,mean_free_path_distance=1.4,world_unit_scale=1.,
    surface_albedo=u.LinearColor(.46,.065,.075,1),mean_free_path_color=u.LinearColor(1,.24,.18,1),
    tint=u.LinearColor(1,.8,.75,1),boundary_color_bleed=u.LinearColor(1,.55,.45,1),
    transmission_tint_color=u.LinearColor(1,.65,.5,1),ior=1.38,roughness0=.6,roughness1=1.2,lobe_mix=.4).items(): s.set_editor_property(key,value)
profile.set_editor_property('settings',s);save(profile)
mpc=lib.load_asset('/Game/Gameplay/Cold/MPC_MouthClimate')
report=[]
for tissue in ('Gum','Palate','Exit'):
    m=lib.load_asset('/Game/Gameplay/MouthV3/M_'+tissue+'V3')
    edit.delete_all_material_expressions(m)
    m.set_editor_property("shading_model",u.MaterialShadingModel.MSM_SUBSURFACE_PROFILE);m.set_editor_property("subsurface_profile",profile)
    m.set_editor_property("two_sided",True);m.set_editor_property("tangent_space_normal",True)
    p=node(m,u.MaterialExpressionWorldPosition);wn=node(m,u.MaterialExpressionVertexNormalWS)
    samples=[]
    for suffix,name,sampler in [('BaseColor','TissueColor',u.MaterialSamplerType.SAMPLERTYPE_COLOR),('OcclusionRoughnessMetallic','TissueARM',u.MaterialSamplerType.SAMPLERTYPE_MASKS),('Normal','TissueNormal',u.MaterialSamplerType.SAMPLERTYPE_NORMAL)]:
        samples.append(node(m,u.MaterialExpressionTextureSampleParameter2D,texture=lib.load_asset('/Game/Gameplay/MouthV3/Textures/T_'+tissue+'_'+suffix),parameter_name=name,sampler_type=sampler))
    color,arm,norm=samples
    tint_values={'Gum':(1.219703,2.399435,1.866061),'Palate':(1.115446,2.066995,1.732087),'Exit':(1.015043,1.857617,1.581965)}[tissue]
    rough_median={'Gum':.270588,'Palate':.278431,'Exit':.235294}[tissue]
    tint=node(m,u.MaterialExpressionVectorParameter,parameter_name='TissueTint',default_value=u.LinearColor(*tint_values,1))
    tissue_color={'Gum':(.46,.065,.077),'Palate':(.31,.03,.043),'Exit':(.24,.018,.026)}[tissue]
    base_code=r'''
float warm=sin(P.x*.007+sin(P.y*.010))*sin(P.z*.011+P.y*.005);
float3 tissue=lerp(TISSUE_COLOR,Base*Tint,.45);
return saturate(tissue*lerp(float3(.97,.94,.94),float3(1.035,1.04,1.02),.5+.5*warm));
'''.replace('TISSUE_COLOR','float3('+','.join(str(v) for v in tissue_color)+')')
    base=custom(m,'Mucosa: warm perfusion',base_code,{'Base':(color,'RGB'),'Tint':(tint,'RGB'),'P':(p,'')})
    rough=custom(m,'Mucosa: continuous wet film',r'''
float film=.5+.5*sin(P.x*.017+sin(P.y*.023)*1.3+P.z*.015);
float center=.14+.18*ROUGH_MEDIAN+.012;
return clamp(center+(R-ROUGH_MEDIAN)+.024*(film-.5),.16,.24);
'''.replace('ROUGH_MEDIAN',str(rough_median)),{'R':(arm,'G'),'P':(p,'')},1)
    fine=custom(m,'Mucosa: fine soft pores','return normalize(float3(N.xy*Strength,N.z));',{'N':(norm,'RGB'),'Strength':(scalar(m,'NormalStrength',1.2),'')})
    cold=node(m,u.MaterialExpressionCollectionParameter,collection=mpc,parameter_name='ColdAmount')
    frost=custom(m,'Mouth climate frost crystal coverage',r'''
float grains=frac(sin(dot(floor(P*.9),float3(12.9898,78.233,45.164)))*43758.5453);
float wisps=.5+.5*sin(P.x*.021+sin(P.y*.018)*2+P.z*.015);
return saturate(Cold)*saturate(.38+.5*saturate(N.z)+.28*wisps+.12*grains);
''',{'Cold':(cold,''),'P':(p,''),'N':(wn,'')},1)
    final=custom(m,'Mouth climate frost color','return lerp(Base,float3(.65,.82,.93),Frost*.84);',{'Base':(base,''),'Frost':(frost,'')})
    rr=custom(m,'Mouth climate rough crystalline finish','return lerp(Rough,.58,Frost);',{'Rough':(rough,''),'Frost':(frost,'')},1)
    out(final,u.MaterialProperty.MP_BASE_COLOR);out(rr,u.MaterialProperty.MP_ROUGHNESS);out(fine,u.MaterialProperty.MP_NORMAL)
    out(scalar(m,'Specular',.44),u.MaterialProperty.MP_SPECULAR);out(scalar(m,'ScatterDensity',.7),u.MaterialProperty.MP_OPACITY)
    out(arm,u.MaterialProperty.MP_AMBIENT_OCCLUSION,'R')
    if tissue=='Exit':
        edit.set_base_material_usage(m,u.MaterialUsage.MATUSAGE_SKELETAL_MESH);edit.set_base_material_usage(m,u.MaterialUsage.MATUSAGE_MORPH_TARGETS)
    edit.layout_material_expressions(m);assert not edit.recompile_material(m);save(m);report.append(m.get_path_name())
# Retain the authored uvula/throat UV maps and all deformation/frost inputs.
for path in ['/Game/Art/Materials/M_LivingTissue','/Game/Art/Materials/M_ThroatSculpt']:
    m=lib.load_asset(path);m.set_editor_property("shading_model",u.MaterialShadingModel.MSM_SUBSURFACE_PROFILE);m.set_editor_property("subsurface_profile",profile)
    for n in edit.get_material_expressions(m):
        if isinstance(n,u.MaterialExpressionScalarParameter):
            name=str(n.get_editor_property("parameter_name"))
            if name=='MicroCreaseStrength':n.set_editor_property("default_value",.025)
            if name=='Specular':n.set_editor_property("default_value",.44)
        if isinstance(n,u.MaterialExpressionCustom):
            if n.get_editor_property('description')=='Wet film':n.set_editor_property('code','return clamp(.14+ORM.g*.18+Bias*.15,.17,.28);')
            if n.get_editor_property('description')=='Subtle vascular mottling':n.set_editor_property('code','float n=sin(P.x*.011+sin(P.y*.019))*sin(P.z*.016+P.y*.008); return Base.rgb*Tint.rgb*(.98+.04*n);')
    assert not edit.recompile_material(m);save(m);report.append(path)
for path in ['/Game/Art/Materials/MI_MouthPalate','/Game/Art/Materials/MI_ThroatSculpt']:
    if not lib.does_asset_exist(path):continue
    mi=lib.load_asset(path)
    for name,value in [('MicroCreaseStrength',.025),('Specular',.44),('MicroNormalStrength',.8),('RoughnessBias',0)]:edit.set_material_instance_scalar_parameter_value(mi,name,value)
    edit.update_material_instance(mi);save(mi)
# The default pressure preset selects its own material at runtime. Calibrate
# that material and the alternate presets, keeping pressure deformation intact.
settings=lib.load_asset('/Game/Data/DA_Tongue')
active=settings.default_pressure_preset.surface_material
u.log('MC_MUCOSA_ACTIVE_TONGUE '+active.get_path_name())
paths={'/Game/Gameplay/Arena/MI_TonguePain',active.get_path_name().split('.')[0]}
paths.update(lib.list_assets('/Game/Gameplay/Arena/Pressure',recursive=False,include_folder=False))
for path in paths:
    mi=lib.load_asset(path)
    if not isinstance(mi,u.MaterialInstanceConstant):continue
    for name,value in [('Normal Strength',.24),('Roughness Strength',1.4),('Spec',.40),('Pressure Wetness',.25),('Pressure Roughness',.18),('Pressure Tint Amount',.35)]:
        edit.set_material_instance_scalar_parameter_value(mi,name,value)
    edit.set_material_instance_vector_parameter_value(mi,'Pressure Tint',u.LinearColor(.96,.84,.88,1))
    edit.update_material_instance(mi);save(mi);report.append(mi.get_path_name())
m=lib.load_asset('/Game/Gameplay/Arena/M_TonguePain')
# The active pressure instances inherit this parent. Their old profile used
# a large scattering radius and a pale albedo unrelated to the mucosa family.
m.set_editor_property('subsurface_profile',profile)
for name in ['MI_TonguePain','Pressure/MI_Pressure_Soft','Pressure/MI_Pressure_Firm','Pressure/MI_Pressure_Deep']:
    instance=lib.load_asset('/Game/Gameplay/Arena/'+name)
    instance.set_editor_property('subsurface_profile',profile)
    instance.set_editor_property('override_subsurface_profile',False)
    save(instance)
call=next(n for n in edit.get_material_expressions(m) if isinstance(n,u.MaterialExpressionMaterialFunctionCall) and n.get_editor_property('material_function')==lib.load_asset('/Game/Gameplay/Arena/MF_TonguePressureSurface'))
remap=next((n for n in edit.get_material_expressions(m) if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description')=='Mucosa: bounded tongue roughness'),None)
if not remap:
    assert list(edit.get_material_expression_input_names(call))==['BaseColor','Roughness','Normal']
    rough_input=list(edit.get_inputs_for_material_expression(m,call))[1]
    output=edit.get_input_node_output_name_for_material_expression(call,rough_input)
    # Unreal returns the output pin text; Multiply has a single unnamed output.
    output=output if isinstance(output,str) else ''
    remap=custom(m,'Mucosa: bounded tongue roughness','return clamp(.18+.10*saturate(R),.18,.28);',{'R':(rough_input,output)},1)
    assert edit.connect_material_expressions(remap,'',call,'Roughness')
remap.set_editor_property('code','return clamp(.18+.10*saturate(R),.18,.28);')
assert not edit.recompile_material(m);save(m);report.append(m.get_path_name())
# Restrict bloom to bright highlights and use a neutral white balance. Keep the
# colleague's lights, volume, exposure method and other post-process settings.
for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    if isinstance(actor,u.DirectionalLight):
        # The 20 m-wide follow bounds and this mouth fit inside 60 m. The old
        # 400 m CSM range wasted shadow texels and made tissue boundaries jagged.
        light=actor.get_component_by_class(u.DirectionalLightComponent)
        light.set_editor_property('dynamic_shadow_distance_movable_light',6000.)
    if not isinstance(actor,u.PostProcessVolume):continue
    pp=actor.get_editor_property('settings')
    for name,value in [('white_temp',5400.),('white_tint',0.),('auto_exposure_bias',.2),('bloom_intensity',.15),('bloom_gaussian_intensity',1.),('bloom_threshold',1.2),('color_saturation',u.Vector4(1.10,1.10,1.10,1.))]:
        pp.set_editor_property('override_'+name,True);pp.set_editor_property(name,value)
    actor.set_editor_property('settings',pp)
assert u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
# One shared beat drives pepper color, emissive flash and the cosmetic expansion.
m=lib.load_asset('/Game/Gameplay/Hazards/M_SpicyPepper');edit.delete_all_material_expressions(m)
color=node(m,u.MaterialExpressionTextureSampleParameter2D,parameter_name='PepperColor',texture=lib.load_asset('/Game/Stylized_Vegetables/Textures/T_Chili_C'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
urgency=scalar(m,'Urgency',0);flash=scalar(m,'Flash',0)
beat=custom(m,'Pepper: shared pulse','return pow(saturate(Flash),3);',{'Flash':(flash,'')},1)
red=custom(m,'Pepper: red pulse',r'''return lerp(Base,float3(.95,.006,.003),saturate(Urgency*.22+Beat*(.6+.18*Urgency)));''',{'Base':(color,'RGB'),'Urgency':(urgency,''),'Beat':(beat,'')})
glow=custom(m,'Pepper: red glow pulse','return float3(.35,.001,.001)*Beat*(.25+.75*Urgency);',{'Beat':(beat,''),'Urgency':(urgency,'')})
out(red,u.MaterialProperty.MP_BASE_COLOR);out(glow,u.MaterialProperty.MP_EMISSIVE_COLOR);out(scalar(m,'Roughness',.32),u.MaterialProperty.MP_ROUGHNESS)
assert not edit.recompile_material(m);save(m)
for name,nozzle in [('M_SprayCan',False),('M_SprayNozzle',True)]:
    path='/Game/Gameplay/Care/'+name
    can=lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset(name,'/Game/Gameplay/Care',u.Material,u.MaterialFactoryNew())
    edit.delete_all_material_expressions(can)
    uv=node(can,u.MaterialExpressionTextureCoordinate)
    code='return float3(.055,.12,.14);' if nozzle else r'''
float label=smoothstep(.25,.27,UV.y)*(1-smoothstep(.70,.72,UV.y));
float2 p=float2(frac(UV.x*3)-.5,UV.y-.48);
float cross=max((1-step(.035,abs(p.x)))*(1-step(.14,abs(p.y))),(1-step(.11,abs(p.x)))*(1-step(.035,abs(p.y))));
return lerp(float3(.78,.88,.85),lerp(float3(.025,.32,.20),float3(.9,.98,.96),cross),label);
'''
    pigment=custom(can,'Spray can: clinic label',code,{'UV':(uv,'')})
    out(pigment,u.MaterialProperty.MP_BASE_COLOR);out(scalar(can,'Roughness',.26),u.MaterialProperty.MP_ROUGHNESS)
    assert not edit.recompile_material(can);save(can)
assert u.MCVFXAssetBuilder.create_spray_mist()
for path in ['/Game/Gameplay/VFX/NS_BrushFoam','/Game/Gameplay/VFX/NS_IceShatter']:
    assert u.MCVFXAssetBuilder.repair_mesh_renderer_slots(lib.load_asset(path))>=0
(root/'Artifacts/Approval/MouthV5_Assets.json').write_text(json.dumps(dict(materials=report,profile=profile.get_path_name(),normal_strength={'mucosa':1.2,'tongue':.24},specular={'mucosa':.44,'tongue':.40},wet_roughness=[.16,.24],tongue_roughness=[.18,.28],post_process={'white_temp':5400,'exposure_bias':.2,'bloom_intensity':.15,'bloom_threshold':1.2}),indent=2))
u.log('MC_MOUTH_V5_READY')
