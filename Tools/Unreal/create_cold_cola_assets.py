"""Author ice, climate material controls and the real first-day cold drink event."""
import unreal as u
from pathlib import Path
import json
lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary; assets=u.AssetToolsHelpers.get_asset_tools()
folder='/Game/Gameplay/Cold'; lib.make_directory(folder)
assert not u.EditorLevelLibrary.get_pie_worlds(False)
def save(a): assert lib.save_loaded_asset(a,only_if_is_dirty=False)
def node(mat,cls,**values):
    n=edit.create_material_expression(mat,cls)
    for k,v in values.items(): n.set_editor_property(k,v)
    return n
def scalar(mat,name,value): return node(mat,u.MaterialExpressionScalarParameter,parameter_name=name,default_value=value)
def custom(mat,name,code,inputs,dim=3):
    n=node(mat,u.MaterialExpressionCustom,description=name,code=code,output_type={1:u.CustomMaterialOutputType.CMOT_FLOAT1,3:u.CustomMaterialOutputType.CMOT_FLOAT3}[dim])
    entries=[]
    for name,(src,pin) in inputs.items():
        i=u.CustomInput(); i.set_editor_property('input_name',name); entries.append(i)
    n.set_editor_property('inputs',entries)
    for name,(src,pin) in inputs.items(): assert edit.connect_material_expressions(src,pin,n,name)
    return n
def out(src,prop,pin=''): assert edit.connect_material_property(src,pin,prop)
mpc=lib.load_asset(folder+'/MPC_MouthClimate')
if not mpc:
    mpc=assets.create_asset('MPC_MouthClimate',folder,u.MaterialParameterCollection,u.MaterialParameterCollectionFactoryNew())
    s=u.CollectionScalarParameter(); s.set_editor_property('parameter_name','ColdAmount'); s.set_editor_property('default_value',0)
    mpc.set_editor_property('scalar_parameters',[s]); save(mpc)

ice=lib.load_asset(folder+'/M_StylizedIce')
if not ice: ice=assets.create_asset('M_StylizedIce',folder,u.Material,u.MaterialFactoryNew())
edit.delete_all_material_expressions(ice)
ice.set_editor_property('shading_model',u.MaterialShadingModel.MSM_SUBSURFACE)
ice.set_editor_property('tangent_space_normal',False)
p=node(ice,u.MaterialExpressionWorldPosition); n=node(ice,u.MaterialExpressionVertexNormalWS); v=node(ice,u.MaterialExpressionCameraVectorWS)
damage=scalar(ice,'DamageAmount',0)
cells=custom(ice,'Layered ice cracks and trapped bubbles',r'''
float3 q=P/58.; float3 cell=floor(q),f=frac(q); float a=10,b=10;
[unroll] for(int x=-1;x<=1;x++) [unroll] for(int y=-1;y<=1;y++) [unroll] for(int z=-1;z<=1;z++) {
    float3 o=float3(x,y,z),g=cell+o;
    float3 h=frac(sin(float3(dot(g,float3(127.1,311.7,74.7)),dot(g,float3(269.5,183.3,246.1)),dot(g,float3(113.5,271.9,124.6))))*43758.5453);
    float d=length(o+h-f); if(d<a) {b=a;a=d;} else b=min(b,d);
}
float vein=1-smoothstep(.008,.045+Damage*.06,b-a);
float secondary=pow(saturate(.5+.5*sin(P.x*.11+sin(P.z*.061)*3+P.y*.075)),30)*.28;
float bubble=pow(saturate(1-a*3.4),6);
return float3(saturate(vein*.75+secondary),bubble,a);
''',{'P':(p,''),'Damage':(damage,'')})
color=custom(ice,'Blue ice body with white internal fractures',r'''
float rim=pow(1-saturate(abs(dot(normalize(N),normalize(V)))),3);
float depth=saturate(Cells.z);
float3 body=lerp(float3(.018,.18,.34),float3(.10,.48,.68),depth*.8+rim*.45);
return lerp(body,float3(.72,.92,1),saturate(Cells.x*.58+Cells.y+rim*.60));
''',{'N':(n,''),'V':(v,''),'Cells':(cells,'')})
out(color,u.MaterialProperty.MP_BASE_COLOR)
rough=custom(ice,'Polished ice and rough frosted fractures','return lerp(.09,.42,saturate(Cells.x*.7+Cells.y));',{'Cells':(cells,'')},1); out(rough,u.MaterialProperty.MP_ROUGHNESS)
normal=custom(ice,'Subtle world space frozen ripples','return normalize(N+float3(cos(P.y*.16+P.z*.08),sin(P.z*.17+P.x*.05),cos(P.x*.13+P.y*.07))*.038);',{'N':(n,''),'P':(p,'')}); out(normal,u.MaterialProperty.MP_NORMAL)
out(scalar(ice,'IceSpecular',.78),u.MaterialProperty.MP_SPECULAR)
scatter=node(ice,u.MaterialExpressionVectorParameter,parameter_name='IceTransmission',default_value=u.LinearColor(.12,.54,.74,1)); out(scatter,u.MaterialProperty.MP_SUBSURFACE_COLOR)
out(scalar(ice,'IceDensity',.42),u.MaterialProperty.MP_OPACITY)
edit.set_base_material_usage(ice,u.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES)
edit.layout_material_expressions(ice); edit.recompile_material(ice); save(ice)

# Preserve authored gum/enamel texture graphs and add a world-wide frost blend.
paths=['/Game/Gameplay/MouthV3/M_GumV3','/Game/Gameplay/MouthV3/M_PalateV3','/Game/Gameplay/MouthV3/M_ExitV3','/Game/Gameplay/Arena/V2/M_ArenaToothCareV2']
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
for a in actors:
    if isinstance(a,u.MCTongue) or isinstance(a,u.MCThroat):
        for c in a.get_components_by_class(u.MeshComponent):
            for i in range(c.get_num_materials()):
                m=c.get_material(i)
                while isinstance(m,u.MaterialInstance): m=m.get_editor_property('parent')
                if isinstance(m,u.Material): paths.append(m.get_path_name().split('.')[0])
modified=[]
for path in set(paths):
    mat=lib.load_asset(path)
    if not mat: continue
    if any(isinstance(x,u.MaterialExpressionCustom) and x.get_editor_property('description')=='Mouth climate frost color' for x in edit.get_material_expressions(mat)): continue
    original=edit.get_material_property_input_node(mat,u.MaterialProperty.MP_BASE_COLOR); pin=edit.get_material_property_input_node_output_name(mat,u.MaterialProperty.MP_BASE_COLOR)
    if not original: continue
    cold=node(mat,u.MaterialExpressionCollectionParameter,collection=mpc,parameter_name='ColdAmount')
    wp=node(mat,u.MaterialExpressionWorldPosition); wn=node(mat,u.MaterialExpressionVertexNormalWS)
    mask=custom(mat,'Mouth climate frost crystal coverage',r'''
float grains=frac(sin(dot(floor(P*.9),float3(12.9898,78.233,45.164)))*43758.5453);
float wisps=.5+.5*sin(P.x*.021+sin(P.y*.018)*2+P.z*.015);
float coat=saturate(.38+.50*saturate(N.z)+.28*wisps+.12*grains);
return saturate(Cold)*coat;
''',{'P':(wp,''),'N':(wn,''),'Cold':(cold,'')},1)
    mix=custom(mat,'Mouth climate frost color','return lerp(Base,float3(.65,.82,.93),Frost*.84);',{'Base':(original,pin),'Frost':(mask,'')}); out(mix,u.MaterialProperty.MP_BASE_COLOR)
    rp=edit.get_material_property_input_node(mat,u.MaterialProperty.MP_ROUGHNESS); rpin=edit.get_material_property_input_node_output_name(mat,u.MaterialProperty.MP_ROUGHNESS)
    if not rp: rp=scalar(mat,'UnfrozenRoughness',.3); rpin=''
    rm=custom(mat,'Mouth climate rough crystalline finish','return lerp(Rough,.58,Frost);',{'Rough':(rp,rpin),'Frost':(mask,'')},1); out(rm,u.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(mat); save(mat); modified.append(path)

drink=lib.load_asset('/Game/Data/DA_ColdColaLiquid') or lib.duplicate_asset('/Game/Data/DA_CoffeeWater','/Game/Data/DA_ColdColaLiquid')
settings=drink.get_editor_property('settings'); settings.set_editor_property('fill_seconds',4); settings.set_editor_property('drain_seconds',3); drink.set_editor_property('settings',settings)
for prop,source,name in [('surface_material','/Game/Gameplay/Liquid/Stylized/MI_StylizedCoffee','MI_ColdColaSurface'),('pour_material','/Game/Art/Materials/MI_CoffeePour','MI_ColdColaPour'),('drop_material','/Game/Art/Materials/MI_CoffeeDrops','MI_ColdColaDrops')]:
    src=lib.load_asset(source)
    if not src:
        src=drink.get_editor_property(prop).load_synchronous() if hasattr(drink.get_editor_property(prop),'load_synchronous') else lib.load_asset(str(drink.get_editor_property(prop)))
    target=lib.load_asset(folder+'/'+name) or lib.duplicate_asset(src.get_path_name().split('.')[0],folder+'/'+name)
    for param,value in [('DeepColor',(.012,.007,.009)),('ShallowColor',(.25,.065,.012)),('FoamColor',(.50,.22,.08)),('CoffeeColor',(.032,.008,.005)),('LiquidColor',(.03,.008,.004))]:
        edit.set_material_instance_vector_parameter_value(target,param,u.LinearColor(*value,1))
    edit.set_material_instance_scalar_parameter_value(target,'CausticStrength',.06); edit.update_material_instance(target); save(target)
    drink.set_editor_property(prop,target)
save(drink)
profile=lib.load_asset('/Game/Data/DA_ColdCola')
if not profile:
    factory=u.DataAssetFactory(); factory.set_editor_property('data_asset_class',u.MCColdColaProfile)
    profile=assets.create_asset('DA_ColdCola','/Game/Data',u.MCColdColaProfile,factory)
profile.set_editor_property('drink',drink); profile.set_editor_property('cold_seconds',60); save(profile)
plan=lib.load_asset('/Game/Data/DA_Day01'); plan.set_editor_property('cold_cola_profile',profile)
steps=list(plan.get_editor_property('steps'))
if not any(s.step==u.MCDayStep.COLD_COLA for s in steps):
    step=u.MCDayStepSettings(); step.step=u.MCDayStep.COLD_COLA; step.seconds=70; step.title='ХОЛОДНАЯ КОЛА'; step.instruction='Скользко! Слот 2 + ЛКМ — разбей лёд. E у зуба — лазать, Space — отпрыгнуть.'
    index=next(i+1 for i,s in enumerate(steps) if s.step==u.MCDayStep.COFFEE_CLEANUP); steps.insert(index,step); plan.set_editor_property('steps',steps)
save(plan)
for a in actors:
    if 'MCCameraBounds' in [str(t) for t in a.tags]:
        # Front teeth extend to X=-1410. Eye limit is just beyond the front enamel.
        a.set_actor_location(u.Vector(-480,-25,350),False,False)
        a.get_component_by_class(u.BoxComponent).set_box_extent(u.Vector(1130,640,300),False)
assert u.MCVFXAssetBuilder.create_ice_shatter()
u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
Path(u.Paths.project_dir(),'Artifacts/ColdCola').mkdir(parents=True,exist_ok=True)
Path(u.Paths.project_dir(),'Artifacts/ColdCola/Assets.json').write_text(json.dumps(dict(frostMaterials=modified,ice=ice.get_path_name(),profile=profile.get_path_name(),steps=[str(s.step) for s in plan.steps]),indent=2),encoding='utf-8')
u.log('MC_COLD_COLA_ASSETS_READY')
