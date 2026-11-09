"""Author an owned opaque Niagara food-chip material in an idle live Editor.

UV0.x < .25 is the peel face. Remaining faces are pulp, with stable per-face
grain. This is a stylized food material, not the original product's cut mesh.
Run with remote_python.py; outputs and temporary media stay on E:.
"""
import json
from pathlib import Path
import unreal as u

OUT=Path('E:/CodexWork/MessControl/Validation/FoodJuicyVFX_20261109')
ROOT='/Game/Gameplay/Food/VFX/Juicy'
PATH=ROOT+'/M_FoodJuicy_Pulp'
OWNER='FoodJuicyVFX_20261109'
EDIT=u.MaterialEditingLibrary
LIB=u.EditorAssetLibrary

PULP=r'''
float peel = 1.0-step(0.25, UV.x);
float2 uv = float2(lerp((UV.x-.34)/.64,UV.x/.24,peel),UV.y);
float2 grid=uv*float2(GrainScale,GrainScale*.58);
float2 cell=floor(grid), f=frac(grid);
float hash=frac(sin(dot(cell,float2(127.1,311.7)))*43758.5453);
float wave=sin(uv.y*9.0+sin(uv.x*7.0)*.4);
float fibers=pow(saturate(.5+.5*sin(uv.x*GrainScale*6.2831853+wave*.55)),12.0);
float footprint=max(length(ddx(uv)),length(ddy(uv)));
float detail=1.0-saturate(footprint*GrainScale*.65);
float fleck=(1.0-smoothstep(.06,.20,length((f-.5)*float2(1.0,1.8))))*step(.83,hash)*detail;
float3 tint=saturate(Tint);
float3 pulp=lerp(sqrt(max(tint,.006)),float3(.95,.89,.72),PulpWhiten);
pulp*=.95+.10*hash;
pulp=lerp(pulp,float3(.99,.95,.82),fibers*FiberStrength*detail);
pulp*=1.0-fleck*.18;
float3 skin=tint*SkinDarken*(.94+.10*hash);
float3 color=lerp(pulp,skin,peel);
float rough=clamp(WetRoughness+.09*hash+.06*peel,.28,.62);
return float4(saturate(color),rough);
'''

def node(mat,kind,x,y,**props):
    result=EDIT.create_material_expression(mat,kind,x,y)
    if props:result.set_editor_properties(props)
    return result

def connect(a,output,b,pin):
    assert EDIT.connect_material_expressions(a,output,b,pin),(output,pin)

def output(a,pin,prop):
    assert EDIT.connect_material_property(a,pin,prop),prop

def run():
    assert not u.EditorLevelLibrary.get_pie_worlds(True),'Preserve active PIE'
    assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Preserve unsaved maps'
    dirty=u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    assert all(p.get_path_name()==PATH for p in dirty),'Preserve unrelated edits'
    OUT.mkdir(parents=True,exist_ok=True)
    if LIB.does_asset_exist(PATH):
        mat=u.load_asset(PATH)
        assert LIB.get_metadata_tag(mat,'MC.AuthoringOwner')==OWNER,'Preserve existing artwork'
        EDIT.delete_all_material_expressions(mat)
    else:
        mat=u.AssetToolsHelpers.get_asset_tools().create_asset('M_FoodJuicy_Pulp',ROOT,u.Material,u.MaterialFactoryNew())
    assert mat
    LIB.set_metadata_tag(mat,'MC.AuthoringOwner',OWNER)
    LIB.set_metadata_tag(mat,'MC.SurfaceContract','UV0.x<.25 peel; UV0.x>=.34 pulp. ParticleColor tints food.')
    mat.set_editor_properties(dict(two_sided=False,blend_mode=u.BlendMode.BLEND_OPAQUE,
        shading_model=u.MaterialShadingModel.MSM_DEFAULT_LIT))
    EDIT.set_material_usage(mat,u.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES)
    uv=node(mat,u.MaterialExpressionTextureCoordinate,-1000,0)
    tint=node(mat,u.MaterialExpressionParticleColor,-1000,180)
    custom=node(mat,u.MaterialExpressionCustom,-350,0,code=PULP,
        output_type=u.CustomMaterialOutputType.CMOT_FLOAT4,
        description='Stable food pulp RGB and wet roughness A; no world-space sliding')
    names=['UV','Tint','GrainScale','PulpWhiten','FiberStrength','SkinDarken','WetRoughness']
    inputs=[]
    for name in names:
        item=u.CustomInput();item.set_editor_property('input_name',name);inputs.append(item)
    custom.set_editor_property('inputs',inputs)
    connect(uv,'',custom,'UV');connect(tint,'RGB',custom,'Tint')
    defaults=dict(GrainScale=18.,PulpWhiten=.46,FiberStrength=.18,SkinDarken=.58,WetRoughness=.38)
    for i,(name,number) in enumerate(defaults.items()):
        scalar=node(mat,u.MaterialExpressionScalarParameter,-950,350+i*120,
            parameter_name=name,default_value=number,group='Food surface')
        connect(scalar,'',custom,name)
    rgb=node(mat,u.MaterialExpressionComponentMask,-80,0,r=True,g=True,b=True,a=False)
    rough=node(mat,u.MaterialExpressionComponentMask,-80,180,r=False,g=False,b=False,a=True)
    connect(custom,'',rgb,'');connect(custom,'',rough,'')
    output(rgb,'',u.MaterialProperty.MP_BASE_COLOR)
    output(rough,'',u.MaterialProperty.MP_ROUGHNESS)
    spec=node(mat,u.MaterialExpressionConstant,-80,300,r=.38)
    output(spec,'',u.MaterialProperty.MP_SPECULAR)
    # A bounded low fill preserves food identity in the dark mouth; it is not
    # an additive flash, bloom layer or dynamic light.
    fill=node(mat,u.MaterialExpressionMultiply,180,-100,const_b=.045)
    connect(rgb,'',fill,'A');output(fill,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.recompile_material(mat)
    assert LIB.save_loaded_asset(mat,only_if_is_dirty=False)
    report=dict(path=PATH,saved=True,owner=OWNER,parameters=defaults,opaque=True,
        uv_contract='U<.25 peel, U>=.34 pulp; grain stays on each rotating face',
        shader='Original procedural fibers/flecks; ParticleColor. No textures, transparency or world-position sampling.',
        original_product_materials=False,requires_visual_review=True)
    (OUT/'Material.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print('FOOD_JUICY_MATERIAL_SAVED '+PATH)

run()
