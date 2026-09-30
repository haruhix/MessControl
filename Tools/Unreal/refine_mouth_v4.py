"""Painter tissue textures, saliva sheen and world-space detail on the real arena."""
import unreal as u
from pathlib import Path
import json
lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary; assets=u.AssetToolsHelpers.get_asset_tools()
root=Path(u.Paths.project_dir()).resolve()
assert not u.EditorLevelLibrary.get_pie_worlds(False)
def save(a): assert lib.save_loaded_asset(a,only_if_is_dirty=False)
def node(m,cls,**kw):
    n=edit.create_material_expression(m,cls)
    for k,v in kw.items(): n.set_editor_property(k,v)
    return n
def scalar(m,name,v): return node(m,u.MaterialExpressionScalarParameter,parameter_name=name,default_value=v)
def custom(m,name,code,inputs,dim=3):
    n=node(m,u.MaterialExpressionCustom,description=name,code=code,output_type={1:u.CustomMaterialOutputType.CMOT_FLOAT1,3:u.CustomMaterialOutputType.CMOT_FLOAT3}[dim])
    entries=[]
    for key in inputs:
        entry=u.CustomInput(); entry.set_editor_property('input_name',key); entries.append(entry)
    n.set_editor_property('inputs',entries)
    for key,(src,pin) in inputs.items(): assert edit.connect_material_expressions(src,pin,n,key)
    return n
def out(n,prop,pin=''): assert edit.connect_material_property(n,pin,prop)
def world_normal(m,n,pin=''):
    t=node(m,u.MaterialExpressionTransform,transform_source_type=u.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_TANGENT,transform_type=u.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    assert edit.connect_material_expressions(n,pin,t,'')
    return t
def detail(m,p,n,strength):
    return custom(m,'V4 mucosa fine creases',r'''
float3 q=P*.075;
float a=q.x+1.8*sin(q.y*.81)+.8*sin(q.z*.57);
float b=q.y+1.6*sin(q.z*.92)+.6*sin(q.x*.73);
float c=q.z+1.4*sin(q.x*.84)+.8*sin(q.y*.67);
float3 g=float3(cos(a),cos(b),cos(c));
float3 pores=sin(P.yzx*.57+sin(P.xyz*.29)*2.1);
float3 grain=g+pores*.23;
float3 nn=normalize(N); grain-=nn*dot(grain,nn);
return normalize(nn-grain*Strength);
''',{'P':(p,''),'N':(n,''),'Strength':(scalar(m,'MicroCreaseStrength',strength),'')})
mpc=lib.load_asset('/Game/Gameplay/Cold/MPC_MouthClimate')
report=[]
for tissue,strength in [('Gum',.16),('Palate',.12),('Exit',.16)]:
    maps={}
    for suffix in ['BaseColor','OcclusionRoughnessMetallic','Normal']:
        files=list((root/'ArtSource/MouthV4/Textures'/tissue).glob('*_'+suffix+'.png')); assert len(files)==1
        task=u.AssetImportTask(); task.filename=str(files[0]); task.destination_path='/Game/Gameplay/MouthV3/Textures'; task.destination_name='T_'+tissue+'_'+suffix
        task.automated=True; task.replace_existing=True; task.save=True; assets.import_asset_tasks([task])
        tex=lib.load_asset(task.destination_path+'/'+task.destination_name)
        tex.srgb=suffix=='BaseColor'
        if suffix=='Normal': tex.compression_settings=u.TextureCompressionSettings.TC_NORMALMAP
        elif suffix!='BaseColor': tex.compression_settings=u.TextureCompressionSettings.TC_MASKS
        save(tex); maps[suffix]=tex
    m=lib.load_asset('/Game/Gameplay/MouthV3/M_'+tissue+'V3')
    edit.delete_all_material_expressions(m)
    m.set_editor_property('two_sided',True); m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_SUBSURFACE); m.set_editor_property('tangent_space_normal',False)
    p=node(m,u.MaterialExpressionWorldPosition); v=node(m,u.MaterialExpressionCameraVectorWS); wn=node(m,u.MaterialExpressionVertexNormalWS)
    textures=[]
    for suffix,param,sampler in [('BaseColor','TissueColor',u.MaterialSamplerType.SAMPLERTYPE_COLOR),('OcclusionRoughnessMetallic','TissueARM',u.MaterialSamplerType.SAMPLERTYPE_MASKS),('Normal','TissueNormal',u.MaterialSamplerType.SAMPLERTYPE_NORMAL)]:
        textures.append(node(m,u.MaterialExpressionTextureSampleParameter2D,texture=maps[suffix],parameter_name=param,sampler_type=sampler))
    color,arm,norm=textures
    color_mix=custom(m,'V4 perfused tissue color',r'''
float warm=sin(P.x*.008+sin(P.y*.013)*1.2)*sin(P.z*.014+P.y*.007);
float patches=sin(P.y*.037+sin(P.z*.021)*2)*sin(P.x*.019+P.z*.027);
float3 tint=lerp(float3(.84,.72,.79),float3(1.07,1.14,1.10),.5+.5*warm);
return Base*tint*Tint*(1+patches*.055);
''',{'P':(p,''),'Base':(color,'RGB'),'Tint':(node(m,u.MaterialExpressionVectorParameter,parameter_name='TissueTint',default_value=u.LinearColor(.86,1.55,1.30,1) if tissue=='Gum' else u.LinearColor(.88,1.30,1.16,1)),'RGB')})
    wet=custom(m,'V4 broken saliva sheen',r'''
float film=.5+.5*sin(P.x*.024+sin(P.y*.041)*1.9+P.z*.018);
float folds=.5+.5*sin(P.y*.066+P.z*.029+sin(P.x*.032));
return clamp(Rough*.62+.035+film*.075+folds*.025,.145,.34);
''',{'P':(p,''),'Rough':(arm,'G')},1)
    cold=node(m,u.MaterialExpressionCollectionParameter,collection=mpc,parameter_name='ColdAmount')
    frost=custom(m,'Mouth climate frost crystal coverage',r'''
float grains=frac(sin(dot(floor(P*.9),float3(12.9898,78.233,45.164)))*43758.5453);
float wisps=.5+.5*sin(P.x*.021+sin(P.y*.018)*2+P.z*.015);
return saturate(Cold)*saturate(.38+.5*saturate(N.z)+.28*wisps+.12*grains);
''',{'P':(p,''),'N':(wn,''),'Cold':(cold,'')},1)
    final=custom(m,'Mouth climate frost color','return lerp(Base,float3(.65,.82,.93),Frost*.84);',{'Base':(color_mix,''),'Frost':(frost,'')})
    frozen_rough=custom(m,'Mouth climate rough crystalline finish','return lerp(Rough,.58,Frost);',{'Rough':(wet,''),'Frost':(frost,'')},1)
    out(final,u.MaterialProperty.MP_BASE_COLOR); out(frozen_rough,u.MaterialProperty.MP_ROUGHNESS)
    out(detail(m,p,world_normal(m,norm,'RGB'),strength),u.MaterialProperty.MP_NORMAL)
    out(scalar(m,'Specular',.52),u.MaterialProperty.MP_SPECULAR)
    scatter=custom(m,'V4 warm translucent flesh','return Base*float3(.74,.23,.15);',{'Base':(color_mix,'')})
    out(scatter,u.MaterialProperty.MP_SUBSURFACE_COLOR); out(scalar(m,'ScatterDensity',.64),u.MaterialProperty.MP_OPACITY)
    out(arm,u.MaterialProperty.MP_AMBIENT_OCCLUSION,'R')
    if tissue=='Exit':
        edit.set_base_material_usage(m,u.MaterialUsage.MATUSAGE_SKELETAL_MESH); edit.set_base_material_usage(m,u.MaterialUsage.MATUSAGE_MORPH_TARGETS)
    edit.layout_material_expressions(m); assert not edit.recompile_material(m); save(m); report.append(m.get_path_name())

# The authored uvula and sculpted throat keep their UV maps and deformation inputs.
for path in ['/Game/Art/Materials/M_LivingTissue','/Game/Art/Materials/M_ThroatSculpt']:
    m=lib.load_asset(path)
    nodes=edit.get_material_expressions(m)
    if not any(isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description')=='V4 mucosa fine creases' for n in nodes):
        base=edit.get_material_property_input_node(m,u.MaterialProperty.MP_NORMAL); pin=edit.get_material_property_input_node_output_name(m,u.MaterialProperty.MP_NORMAL)
        if base:
            n=world_normal(m,base,pin) if m.get_editor_property('tangent_space_normal') else base
            p=node(m,u.MaterialExpressionWorldPosition); out(detail(m,p,n,.13),u.MaterialProperty.MP_NORMAL); m.set_editor_property('tangent_space_normal',False)
    edit.recompile_material(m); save(m); report.append(m.get_path_name())

for cmd in ['r.ReflectionMethod 2','r.SSR.Quality 2','r.SSR.MaxRoughness .65']:
    u.SystemLibrary.execute_console_command(None,cmd)
assert u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
path=root/'Artifacts/Approval/MouthV4_Assets.json'; path.parent.mkdir(parents=True,exist_ok=True)
path.write_text(json.dumps(dict(materials=report,textures='Painter 2048px packed Unreal maps',reflection='SSR'),indent=2))
u.log('MC_MOUTH_V4_MATERIALS_READY')


