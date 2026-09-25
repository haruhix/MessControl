"""Create the coffee grid, surface material/instance and water DA. No level or artist mesh edits.
Existing MI and DA tuning is retained; rebuilding this generated master requires MC_REBUILD_COFFEE_WATER=1.
"""
import os
from pathlib import Path
import unreal as u
root=Path(u.Paths.project_dir()).resolve()
lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary; assets=u.AssetToolsHelpers.get_asset_tools()
mesh_path='/Game/Art/Meshes/SM_CoffeeSurface'
if not lib.does_asset_exist(mesh_path):
    u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
    task=u.AssetImportTask(); task.filename=str(root/'ArtSource/CoffeeWater/SM_CoffeeSurface.fbx')
    task.destination_path='/Game/Art/Meshes'; task.destination_name='SM_CoffeeSurface'; task.automated=True; task.save=True
    opt=u.FbxImportUI(); opt.import_mesh=True; opt.import_as_skeletal=False; opt.import_materials=False; opt.import_textures=False
    opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH; opt.static_mesh_import_data.combine_meshes=True
    opt.static_mesh_import_data.auto_generate_collision=False; opt.static_mesh_import_data.generate_lightmap_u_vs=False
    task.options=opt; task.factory=u.FbxFactory(); assets.import_asset_tasks([task])
    assert lib.does_asset_exist(mesh_path)

# A flat mesh has zero vertical bounds; retain all waves during visibility culling.
surface=lib.load_asset(mesh_path)
for side in ('positive_bounds_extension','negative_bounds_extension'):
    bounds=surface.get_editor_property(side)
    surface.set_editor_property(side,u.Vector(bounds.x,bounds.y,max(bounds.z,16)))
assert lib.save_loaded_asset(surface,only_if_is_dirty=True)

path='/Game/Art/Materials/M_CoffeeSurface'
if not lib.does_asset_exist(path) or os.environ.get('MC_REBUILD_COFFEE_WATER')=='1':
    mat=lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset('M_CoffeeSurface','/Game/Art/Materials',u.Material,u.MaterialFactoryNew())
    edit.delete_all_material_expressions(mat)
    mat.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('two_sided',True)
    mat.set_editor_property('tangent_space_normal',False)
    mat.set_editor_property('translucency_lighting_mode',u.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    mat.set_editor_property('screen_space_reflections',True)
    mat.set_editor_property('refraction_method',u.RefractionMode.RM_PIXEL_NORMAL_OFFSET)
    def node(cls,x,y,desc=''):
        n=edit.create_material_expression(mat,cls,x,y)
        if desc: n.set_editor_property('desc',desc)
        return n
    row=0
    def scalar(name,default,group='Appearance'):
        global row
        row+=100; n=node(u.MaterialExpressionScalarParameter,-1200,row)
        n.set_editor_property('parameter_name',name); n.set_editor_property('default_value',default); n.set_editor_property('group',group)
        return n
    def vector(name,color,group='Appearance'):
        global row
        row+=100; n=node(u.MaterialExpressionVectorParameter,-1550,row)
        n.set_editor_property('parameter_name',name); n.set_editor_property('default_value',u.LinearColor(*color)); n.set_editor_property('group',group)
        return n
    def link(a,b,pin='',output=''):
        assert edit.connect_material_expressions(a,output,b,pin),str((a,b,pin,output))
    def prop(n,p,output=''): assert edit.connect_material_property(n,output,p)
    def custom(code,inputs,kind,x,y,description):
        n=node(u.MaterialExpressionCustom,x,y); n.set_editor_property('description',description); n.set_editor_property('output_type',kind)
        entries=[]
        for name in inputs:
            e=u.CustomInput(); e.set_editor_property('input_name',name); entries.append(e)
        n.set_editor_property('inputs',entries); n.set_editor_property('code',code)
        for name,source in inputs.items(): link(source,n,name,'RGBA' if isinstance(source,u.MaterialExpressionVectorParameter) else '')
        return n
    world=node(u.MaterialExpressionWorldPosition,-1900,0)
    world.set_editor_property('world_position_shader_offset',u.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    time=scalar('WaterTime',0,'Runtime'); height=scalar('RippleHeight',6,'Runtime'); length=scalar('RippleLength',260,'Runtime'); speed=scalar('RippleSpeed',1.4,'Runtime')
    shared={'P':world,'Time':time,'Height':height,'Length':length,'Speed':speed}
    phases='''float k=6.2831853/max(Length,1); float t=Time*Speed;
float a=k*(P.x+.28*P.y)-t;
float b=k*(-.45*P.x+.9*P.y)*1.73+t*1.21;
float c=k*(.2*P.x+P.y)*2.7-t*.73;
'''
    wpo=custom(phases+'return float3(0,0,Height*(.6*sin(a)+.28*sin(b)+.12*sin(c)));',shared,u.CustomMaterialOutputType.CMOT_FLOAT3,-400,0,'Shared physical wave height (cm)')
    prop(wpo,u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    normal=custom(phases+'''
float2 slope=Height*k*(.6*cos(a)*float2(1,.28)+.28*cos(b)*1.73*float2(-.45,.9)+.12*cos(c)*2.7*float2(.2,1));
// Fine normals provide small moving highlights without adding physics geometry.
slope+=NormalDetail*float2(sin(P.x*.11+P.y*.04-Time*2),cos(P.y*.12-P.x*.05+Time*1.7));
return normalize(float3(-slope,1));
''',dict(shared,NormalDetail=scalar('NormalDetail',.075)),u.CustomMaterialOutputType.CMOT_FLOAT3,-400,250,'Analytic world normal + fine ripples')
    prop(normal,u.MaterialProperty.MP_NORMAL)
    depth=node(u.MaterialExpressionDepthFade,-800,650); depth.set_editor_property('fade_distance_default',240)
    link(scalar('DepthTintDistance',240),depth,'FadeDistance')
    edge=node(u.MaterialExpressionDepthFade,-800,800); edge.set_editor_property('fade_distance_default',22)
    link(scalar('EdgeFadeDistance',22),edge,'FadeDistance')
    shore=node(u.MaterialExpressionDepthFade,-800,950); shore.set_editor_property('fade_distance_default',48)
    link(scalar('ShoreFoamWidth',48),shore,'FadeDistance')
    fresnel=node(u.MaterialExpressionFresnel,-800,1100); fresnel.set_editor_property('exponent',3)
    wakes={f'Wake{i}':vector(f'Wake{i}',(0,0,0,0),'Runtime') for i in range(4)}
    foam=custom('''
float t=Time*.22;
float field=.5+.24*sin(P.x*.024+t+1.3*sin(P.y*.018-t*.6))
    +.18*cos(P.y*.027-t*.7+sin(P.x*.015))+.08*sin(P.x*.062+P.y*.037+t);
float crema=smoothstep(.70,.86,field)*.55;
float2 q=P.xy*.075+float2(-Time*.23,Time*.07);
float2 cell=floor(q);
float2 jitter=.22*sin(float2(dot(cell,float2(127.1,311.7)),dot(cell,float2(269.5,183.3))));
float r=length(frac(q)-.5-jitter);
float bubbles=smoothstep(.13,.2,r)*(1-smoothstep(.23,.32,r));
float rim=(1-Shore)*(.45+.35*bubbles);
float wake=0;
float4 points[4]={Wake0,Wake1,Wake2,Wake3};
[unroll] for(int i=0;i<4;i++) {
    float d=length(P.xy-points[i].xy);
    float rings=pow(saturate(.5+.5*sin(d*.1-Time*5)),5);
    wake+=rings*exp(-d/78)*smoothstep(16,36,d)*points[i].z*points[i].w;
}
return saturate(FoamAmount*(crema*(.6+.4*bubbles)+rim+WakeStrength*wake));
''',dict(P=world,Time=time,Shore=shore,FoamAmount=scalar('FoamAmount',.7),WakeStrength=scalar('WakeStrength',.7),**wakes),u.CustomMaterialOutputType.CMOT_FLOAT1,-300,800,'Crema patches, shore bubbles and four player wakes')
    color=custom('''
float stain=.96+.04*sin(P.x*.035+P.y*.026-Time*.7);
float3 liquid=lerp(Shallow.rgb,Deep.rgb,saturate(Depth))*stain;
return lerp(liquid,FoamColor.rgb,saturate(Foam));
''',dict(P=world,Time=time,Depth=depth,Foam=foam,Shallow=vector('ShallowColor',(.28,.11,.025,1)),Deep=vector('DeepColor',(.11,.033,.008,1)),FoamColor=vector('FoamColor',(.85,.60,.32,1))),u.CustomMaterialOutputType.CMOT_FLOAT3,200,300,'Coffee depth tint and crema')
    prop(color,u.MaterialProperty.MP_BASE_COLOR)
    opacity=custom('float border=saturate(min(ArenaSize.x-abs(P.x),ArenaSize.y-abs(P.y))/25); return saturate(lerp(ShallowOpacity,DeepOpacity,Depth)+Fresnel*.12+Foam*.18)*Edge*border;',
        dict(P=world,ArenaSize=vector('ArenaSize',(1050,740,0,0),'Runtime'),Depth=depth,Edge=edge,Fresnel=fresnel,Foam=foam,ShallowOpacity=scalar('ShallowOpacity',.68),DeepOpacity=scalar('DeepOpacity',.92)),u.CustomMaterialOutputType.CMOT_FLOAT1,200,600,'Readable shallow water, richer deep coffee')
    prop(opacity,u.MaterialProperty.MP_OPACITY)
    roughness=custom('return lerp(Roughness,.42,Foam);',dict(Roughness=scalar('Roughness',.22),Foam=foam),u.CustomMaterialOutputType.CMOT_FLOAT1,200,800,'Glossy liquid / softer foam highlights')
    prop(roughness,u.MaterialProperty.MP_ROUGHNESS)
    prop(scalar('Specular',.5),u.MaterialProperty.MP_SPECULAR)
    prop(scalar('Refraction',1.015),u.MaterialProperty.MP_REFRACTION)
    # Restrained fill keeps coffee readable in the dark mouth without looking self-lit.
    fill=custom('return Color*Fill;',dict(Color=color,Fill=scalar('AmbientFill',.4)),u.CustomMaterialOutputType.CMOT_FLOAT3,200,1000,'Small art-directed ambient fill')
    prop(fill,u.MaterialProperty.MP_EMISSIVE_COLOR)
    errors=edit.recompile_material(mat)
    if errors: raise RuntimeError(str(errors))
    assert lib.save_loaded_asset(mat,only_if_is_dirty=False)

instance_path='/Game/Art/Materials/MI_CoffeeSurface'
if not lib.does_asset_exist(instance_path):
    instance=assets.create_asset('MI_CoffeeSurface','/Game/Art/Materials',u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(instance,lib.load_asset(path)); edit.update_material_instance(instance)
    assert lib.save_loaded_asset(instance,only_if_is_dirty=False)
profile_path='/Game/Data/DA_CoffeeWater'
if not lib.does_asset_exist(profile_path):
    factory=u.DataAssetFactory(); factory.set_editor_property('data_asset_class',u.MCCoffeeProfile)
    profile=assets.create_asset('DA_CoffeeWater','/Game/Data',u.MCCoffeeProfile,factory)
    profile.set_editor_property('surface_mesh',lib.load_asset(mesh_path)); profile.set_editor_property('surface_material',lib.load_asset(instance_path))
    assert lib.save_loaded_asset(profile,only_if_is_dirty=False)
u.log('MC_COFFEE_WATER_ASSETS_PASS')
u.SystemLibrary.quit_editor()
