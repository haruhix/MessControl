"""Build the floor-coffee material. Does not touch flood, teeth, ulcers or the map.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
MI overrides are preserved when rebuilding the parent material.
"""
from pathlib import Path
import unreal as u

lib=u.EditorAssetLibrary
edit=u.MaterialEditingLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
folder='/Game/Gameplay/Liquid'
path=folder+'/M_CoffeePuddle'
mat=lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset('M_CoffeePuddle',folder,u.Material,u.MaterialFactoryNew())
edit.delete_all_material_expressions(mat)
mat.set_editor_property('blend_mode',u.BlendMode.BLEND_MASKED)
mat.set_editor_property('opacity_mask_clip_value',.35)
mat.set_editor_property('two_sided',False)
mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_DEFAULT_LIT)

def node(kind,x,y): return edit.create_material_expression(mat,kind,x,y)
def scalar(name,value,y):
    n=node(u.MaterialExpressionScalarParameter,-1200,y)
    n.set_editor_property('parameter_name',name); n.set_editor_property('default_value',value)
    n.set_editor_property('group','Liquid'); return n
def vector(name,value,y):
    n=node(u.MaterialExpressionVectorParameter,-1200,y)
    n.set_editor_property('parameter_name',name); n.set_editor_property('default_value',u.LinearColor(*value))
    n.set_editor_property('group','Liquid'); return n
def custom(name,code,inputs,x,y,output=u.CustomMaterialOutputType.CMOT_FLOAT1):
    n=node(u.MaterialExpressionCustom,x,y)
    n.set_editor_property('description',name); n.set_editor_property('code',code); n.set_editor_property('output_type',output)
    entries=[]
    for key in inputs:
        i=u.CustomInput(); i.set_editor_property('input_name',key); entries.append(i)
    n.set_editor_property('inputs',entries)
    for key,(src,pin) in inputs.items(): assert edit.connect_material_expressions(src,pin,n,key),key
    return n
def prop(src,property,output=''): assert edit.connect_material_property(src,output,property)

uv=node(u.MaterialExpressionTextureCoordinate,-1600,0)
mask=node(u.MaterialExpressionTextureObjectParameter,-1600,200)
mask.set_editor_property('parameter_name','WipeMask')
mask.set_editor_property('texture',lib.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
mask.set_editor_property('sampler_type',u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
clock=node(u.MaterialExpressionTime,-1600,500)
common={'UV':(uv,''),'Wipe':(mask,''),'Seed':(scalar('Seed',1,0),''),
        'Finish':(scalar('Finish',0,120),''),'Age':(scalar('BrushAge',100,240),''),
        'Brush':(vector('Brush',(.5,.5,1,0),360),'RGBA'),'Time':(clock,''),
        'Depth':(scalar('Depth',2.6,500),''),'Rim':(scalar('Rim',.8,620),''),'WorldSize':(scalar('WorldSize',184,740),'')}
shader=r'''
struct CoffeeSurface
{
    float join(float a,float b)
    {
        float h=max(.065-abs(a-b),0)/.065;
        return max(a,b)+h*h*.01625;
    }
    float shape(float2 uv,float seed)
    {
        float angle=seed*2.39996;
        float2 p=(uv-.5)*2;
        p=float2(p.x*cos(angle)-p.y*sin(angle),p.x*sin(angle)+p.y*cos(angle));
        p.y*=1.06+.20*sin(seed*1.731);
        float d=.48-length(p);
        d=join(d,.30+.045*sin(seed*2.17)-length(p-float2(.32,.08)));
        d=join(d,.27+.045*cos(seed*3.11)-length(p-float2(-.35,-.09)));
        d=join(d,.25-length(p-float2(-.12,.34)));
        d=join(d,.24-length(p-float2(.20,-.30)));
        d+=.018*sin(p.x*24+seed)*sin(p.y*19-seed);
        d=max(d,.055-length(p-float2(.74,.32)));
        d=max(d,.075-length(p-float2(-.65,-.46)));
        d=max(d,.035-length(p-float2(-.48,.59)));
        d=max(d,.047-length(p-float2(.20,-.75)));
        return d;
    }
    float4 surface(float2 uv,Texture2D wipe,SamplerState samp,float seed,float finish,float age,float4 brush,float time,float depth,float rim)
    {
        float d=shape(uv,seed);
        float2 delta=uv-brush.xy;
        float pulse=exp(-age*5);
        // Only the wet edge ahead of the bristles is displaced. Old cleared tracks stay clear.
        float wake=exp(-dot(delta,delta)*65)*pulse;
        float2 flow=brush.zw*wake*.013;
        float coverage=Texture2DSampleLevel(wipe,samp,uv-flow,0).r;
        float interior=smoothstep(0,.075,d);
        float liquid=smoothstep(.10,.55,coverage);
        float edge=exp(-abs(d-.025)*55);
        float movingRim=(1-smoothstep(.35,.95,coverage))*liquid*wake;
        float height=(depth*interior*liquid+rim*movingRim);
        float ripple=sin(length(delta)*100-age*20)*exp(-length(delta)*7)*pulse*.20;
        height=max(0,height+ripple*interior*liquid);
        height+=(sin(uv.x*25+time*.8)*sin(uv.y*23-time*.7))*.045*interior*liquid;
        // At the last gameplay contact the thin residue gathers and recedes over 0.45 seconds.
        float finalEdge=d-finish*.85;
        float alpha=smoothstep(-.003,.008,finalEdge)*smoothstep(.08,.23,coverage-finish*.8);
        return float4(height*(1-finish),alpha,saturate(edge+movingRim),interior*liquid);
    }
};
CoffeeSurface S;
'''
call='S.surface(UV,Wipe,WipeSampler,Seed,Finish,Age,Brush,Time,Depth,Rim)'
field=custom('Liquid height, coverage, meniscus, thickness',shader+'return '+call+';',common,-450,0,u.CustomMaterialOutputType.CMOT_FLOAT4)
opacity=custom('Persistent local cleaning','return F.g;',{'F':(field,'')},0,300)
prop(opacity,u.MaterialProperty.MP_OPACITY_MASK)
normal_code=shader+r'''
float e=.0025;
float hx=S.surface(UV+float2(e,0),Wipe,WipeSampler,Seed,Finish,Age,Brush,Time,Depth,Rim).x-S.surface(UV-float2(e,0),Wipe,WipeSampler,Seed,Finish,Age,Brush,Time,Depth,Rim).x;
float hy=S.surface(UV+float2(0,e),Wipe,WipeSampler,Seed,Finish,Age,Brush,Time,Depth,Rim).x-S.surface(UV-float2(0,e),Wipe,WipeSampler,Seed,Finish,Age,Brush,Time,Depth,Rim).x;
return normalize(float3(-hx/(e*2*WorldSize),-hy/(e*2*WorldSize),1));
'''
normal=custom('Rounded liquid edge and brush wake normals',normal_code,common,-450,600,u.CustomMaterialOutputType.CMOT_FLOAT3)
prop(normal,u.MaterialProperty.MP_NORMAL)
vn=node(u.MaterialExpressionVertexNormalWS,-450,1000)
height=custom('Thin liquid film','return N*F.x;',{'F':(field,''),'N':(vn,'')},0,1000,u.CustomMaterialOutputType.CMOT_FLOAT3)
prop(height,u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
tint=vector('LiquidColor',(.24,.075,.011,1),900)
edge_tint=vector('EdgeColor',(.40,.15,.03,1),1040)
color=custom('Context colour and wet meniscus','return lerp(Edge,Body,saturate(F.w*.85+.15))*(1+F.z*.12)*(1+.06*sin(Seed*4.137));',
             {'F':(field,''),'Body':(tint,''),'Edge':(edge_tint,''),'Seed':common['Seed']},0,0,u.CustomMaterialOutputType.CMOT_FLOAT3)
prop(color,u.MaterialProperty.MP_BASE_COLOR)
prop(scalar('Roughness',.10,1180),u.MaterialProperty.MP_ROUGHNESS)
prop(scalar('Specular',.85,1300),u.MaterialProperty.MP_SPECULAR)
# Two broad virtual softboxes keep the wet film readable inside the enclosed mouth.
# Their reflection follows the camera and the same normals used by the physical shading.
sheen_code=r'''
float3 reflected=reflect(-normalize(View),normalize(Normal));
float key=pow(saturate(dot(reflected,normalize(float3(.65,.25,.72)))),110);
float fill=pow(saturate(dot(reflected,normalize(float3(-.35,-.55,.76)))),150);
return float3(1,.96,.89)*(key*2.5+fill*1.2)*Strength;
'''
sheen=custom('Stylized wet reflection',sheen_code,{'View':(node(u.MaterialExpressionCameraVectorWS,-450,1400),''),
             'Normal':(node(u.MaterialExpressionPixelNormalWS,-450,1600),''),'Strength':(scalar('WetSheen',.45,1420),'')},
             0,1400,u.CustomMaterialOutputType.CMOT_FLOAT3)
prop(sheen,u.MaterialProperty.MP_EMISSIVE_COLOR)
errors=edit.recompile_material(mat)
assert not errors,errors
assert lib.save_loaded_asset(mat,only_if_is_dirty=False)
mi_path=folder+'/MI_CoffeePuddle'
mi=lib.load_asset(mi_path) if lib.does_asset_exist(mi_path) else assets.create_asset('MI_CoffeePuddle',folder,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
edit.set_material_instance_parent(mi,mat); edit.update_material_instance(mi)
assert lib.save_loaded_asset(mi,only_if_is_dirty=False)
for name,body,edge,rough,depth in [('MI_CurryPuddle',(.44,.17,.009,1),(.62,.31,.025,1),.19,1.8),('MI_FoodSaucePuddle',(.36,.028,.012,1),(.52,.07,.022,1),.2,2.2)]:
    if lib.does_asset_exist(folder+'/'+name): continue
    preset=assets.create_asset(name,folder,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(preset,mat)
    for parameter,value in [('LiquidColor',body),('EdgeColor',edge)]: edit.set_material_instance_vector_parameter_value(preset,parameter,u.LinearColor(*value))
    for parameter,value in [('Roughness',rough),('Depth',depth)]: edit.set_material_instance_scalar_parameter_value(preset,parameter,value)
    edit.update_material_instance(preset); assert lib.save_loaded_asset(preset,only_if_is_dirty=False)
u.log('MC_COFFEE_PUDDLE_ASSETS_PASS')
