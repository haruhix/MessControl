"""Prepare an isolated gold PBR surface using the verified source-compatible atlas.

No assignment to BP/current model; no change to shared masters or old glow VFX.
Palette is editable parameters; cyan source paint becomes warm gold lacquer,
source brass becomes brighter trim. Painted form shading/UV layout stay intact.
"""
import hashlib
import json
import shutil
from pathlib import Path
import unreal as u

OUT=Path(globals().get('MC_CHEST_GOLD_MATERIAL_OUTPUT',
    'E:/CodexWork/HubChestWardrobe_20261022/ChestGoldMaterial')).resolve()
assert OUT.drive.upper()=='E:'
assert not u.EditorLevelLibrary.get_pie_worlds(False)
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
OUT.mkdir(parents=True,exist_ok=True)
E,L=u.MaterialEditingLibrary,u.EditorAssetLibrary
name='M_RewardChest_Gold'
folder='/Game/Gameplay/VFX/RewardChest'
asset_path=folder+'/'+name
material=L.load_asset(asset_path)
if material:
    file=Path(u.Paths.project_content_dir())/'Gameplay/VFX/RewardChest'/(name+'.uasset')
    backup=OUT/'BeforePackages'/file.name;backup.parent.mkdir(exist_ok=True)
    if file.exists() and not backup.exists():shutil.copy2(file,backup)
    E.delete_all_material_expressions(material)
else:
    material=u.AssetToolsHelpers.get_asset_tools().create_asset(name,folder,u.Material,u.MaterialFactoryNew())
material.set_editor_properties(dict(blend_mode=u.BlendMode.BLEND_OPAQUE,
    shading_model=u.MaterialShadingModel.MSM_DEFAULT_LIT,two_sided=False))

def node(cls,x,y,**props):
    n=E.create_material_expression(material,cls,x,y)
    if props:n.set_editor_properties(props)
    return n

def link(a,out,b,pin):assert E.connect_material_expressions(a,out,b,pin),(out,pin)
def output(a,out,pin):assert E.connect_material_property(a,out,pin)

uv=node(u.MaterialExpressionTextureCoordinate,-1100,0,coordinate_index=0)
samples={}
for i,(key,suffix,sampler) in enumerate([
        ('Albedo','BaseColor',u.MaterialSamplerType.SAMPLERTYPE_COLOR),
        ('Normal','Normal',u.MaterialSamplerType.SAMPLERTYPE_NORMAL),
        ('ORM','ORM',u.MaterialSamplerType.SAMPLERTYPE_MASKS)]):
    texture=u.load_asset('/Game/Gameplay/Roguelike/ChestAuthored/T_ChestGameplay_'+suffix)
    assert texture,'Prepare the owned source-compatible texture packet first'
    sample=node(u.MaterialExpressionTextureSampleParameter2D,-850,i*250,
        texture=texture,parameter_name='Chest'+key,sampler_type=sampler)
    link(uv,'',sample,'UVs');samples[key]=sample
body=node(u.MaterialExpressionVectorParameter,-850,-280,parameter_name='BodyGoldColor',
    default_value=u.LinearColor(.60,.29,.042,1))
trim=node(u.MaterialExpressionVectorParameter,-850,-130,parameter_name='TrimGoldColor',
    default_value=u.LinearColor(.95,.60,.11,1))
palette=node(u.MaterialExpressionCustom,-450,0,description='Source-compatible warm gold palette',
    output_type=u.CustomMaterialOutputType.CMOT_FLOAT3,
    code='''float id=smoothstep(-.015,.14,Albedo.r-Albedo.b);
float lum=max(.012,dot(Albedo,float3(.2126,.7152,.0722)));
float bodyShade=clamp(lum/.32,.25,1.35);
float trimShade=clamp(lum/.53,.22,1.25);
return saturate(lerp(BodyGold.rgb*bodyShade,TrimGold.rgb*trimShade,id));''')
inputs=[]
for label,source in [('Albedo',samples['Albedo']),('BodyGold',body),('TrimGold',trim)]:
    entry=u.CustomInput();entry.set_editor_property('input_name',label);inputs.append(entry)
palette.set_editor_property('inputs',inputs)
link(samples['Albedo'],'RGB',palette,'Albedo');link(body,'RGB',palette,'BodyGold');link(trim,'RGB',palette,'TrimGold')
output(palette,'',u.MaterialProperty.MP_BASE_COLOR)
output(samples['Normal'],'RGB',u.MaterialProperty.MP_NORMAL)
output(samples['ORM'],'R',u.MaterialProperty.MP_AMBIENT_OCCLUSION)
rough=node(u.MaterialExpressionClamp,-450,420,min_default=.30,max_default=.85)
link(samples['ORM'],'G',rough,'');output(rough,'',u.MaterialProperty.MP_ROUGHNESS)
strength=node(u.MaterialExpressionScalarParameter,-850,750,parameter_name='MetallicStrength',default_value=.75)
metal=node(u.MaterialExpressionMultiply,-450,650)
link(samples['ORM'],'B',metal,'A');link(strength,'',metal,'B');output(metal,'',u.MaterialProperty.MP_METALLIC)
spec=node(u.MaterialExpressionScalarParameter,-450,850,parameter_name='SpecularStrength',default_value=.35)
output(spec,'',u.MaterialProperty.MP_SPECULAR)
E.recompile_material(material)
assert L.save_loaded_asset(material,only_if_is_dirty=False)
report=dict(status='candidate_saved',asset=material.get_path_name(),
    scope='Gold palette with source-compatible UV0 maps; not assigned to runtime',
    parameters=dict(BodyGoldColor=[.60,.29,.042],TrimGoldColor=[.95,.60,.11],MetallicStrength=.75,SpecularStrength=.35),
    maps='Exact cyan/gold authored atlas; shader remaps cyan paint to warm gold lacquer, retaining painted shading. Neutral normal and technically generated material-ID ORM, no foreign map sampling.',
    preserved='Existing BP meshes/component defaults/glow and all shared original materials unchanged')
(OUT/'Authoring.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_CHEST_GOLD_CANDIDATE '+str(OUT/'Authoring.json'))
