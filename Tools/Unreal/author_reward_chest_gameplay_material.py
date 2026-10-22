"""Export the imported chest albedo and create a dedicated mesh-UV PBR candidate.

Imports an owned source packet (two FBX, three textures) and saves only these
new packages plus M_RewardChest_Gameplay. Original master, MIC, meshes, textures
and Blueprint are preserved. A failed first attempt may repair only this tool's
own unsaved material; unrelated dirty content or any PIE aborts before mutation.
"""
import json
import shutil
from pathlib import Path
import unreal as u

OUT=Path(globals().get('MC_CHEST_GAMEPLAY_MATERIAL_OUTPUT',
    'E:/CodexWork/HubChestWardrobe_20261022/ChestMaterialCandidate')).resolve()
assert OUT.drive.upper()=='E:'
assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Do not author while Play is running'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map edits'
allowed_dirty={'/Game/Gameplay/VFX/RewardChest/M_RewardChest_Gameplay'}
assert all(p.get_path_name() in allowed_dirty for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()), 'Preserve unrelated unsaved content edits'
OUT.mkdir(parents=True,exist_ok=True)
E,L=u.MaterialEditingLibrary,u.EditorAssetLibrary
folder='/Game/Gameplay/VFX/RewardChest'
name='M_RewardChest_Gameplay'
asset_path=folder+'/'+name
prefix='/Game/Art/Texture/Buster/Chest/SM_Chest_low_DefaultMaterial_'
original_textures={key:u.load_asset(prefix+suffix) for key,suffix in
    [('Albedo','BaseColor'),('Normal','Normal'),('ORM','OcclusionRoughnessMetallic')]}
assert all(original_textures.values()), 'Original texture evidence is required'
export=u.AssetExportTask()
export.set_editor_properties(dict(object=original_textures['Albedo'],
    filename=str(OUT/'Imported_Chest_BaseColor.png'),automated=True,prompt=False,
    replace_identical=True,exporter=u.TextureExporterPNG()))
assert u.Exporter.run_asset_export_task(export)
assert (OUT/'Imported_Chest_BaseColor.png').exists()
root=Path(u.Paths.project_dir()).resolve()
packet=root/'ArtSource/ChestGameplay_20261022'
assert (packet/'Manifest.json').exists(), 'Run Tools/Art/RewardChest/build_gameplay_texture_packet.py first'
owned_folder='/Game/Gameplay/Roguelike/ChestAuthored'
tools=u.AssetToolsHelpers.get_asset_tools()
textures={}
for key,suffix in [('Albedo','BaseColor'),('Normal','Normal'),('ORM','ORM')]:
    texname='T_ChestGameplay_'+suffix
    task=u.AssetImportTask()
    task.set_editor_properties(dict(filename=str(packet/(texname+'.png')),
        destination_path=owned_folder,destination_name=texname,
        automated=True,replace_existing=True,save=True))
    tools.import_asset_tasks([task])
    texture=u.load_asset(owned_folder+'/'+texname)
    assert texture, 'Owned texture import failed: '+texname
    texture.set_editor_properties(dict(srgb=key=='Albedo',
        compression_settings=u.TextureCompressionSettings.TC_NORMALMAP if key=='Normal'
        else u.TextureCompressionSettings.TC_MASKS if key=='ORM' else u.TextureCompressionSettings.TC_DEFAULT))
    assert L.save_loaded_asset(texture,only_if_is_dirty=False)
    textures[key]=texture
meshes={}
for part in ['Bot','Top']:
    meshname='SM_Chest_'+part
    task=u.AssetImportTask()
    options=u.FbxImportUI()
    options.set_editor_properties(dict(import_mesh=True,import_as_skeletal=False,
        import_materials=False,import_textures=False,import_animations=False))
    options.static_mesh_import_data.set_editor_properties(dict(combine_meshes=True,
        auto_generate_collision=False,generate_lightmap_u_vs=False,
        import_uniform_scale=1.))
    task.set_editor_properties(dict(filename=str(root/'ArtSource/ChestLockpick/Props'/(meshname+'.fbx')),
        destination_path=owned_folder,destination_name=meshname,
        automated=True,replace_existing=True,save=True,options=options))
    tools.import_asset_tasks([task])
    mesh=u.load_asset(owned_folder+'/'+meshname)
    assert isinstance(mesh,u.StaticMesh), 'Owned source mesh import failed: '+meshname
    meshes[part]=mesh.get_path_name()
material=L.load_asset(asset_path)
if material:
    file=Path(u.Paths.project_content_dir())/'Gameplay/VFX/RewardChest'/str(name+'.uasset')
    backup=OUT/'BeforePackages'/file.name;backup.parent.mkdir(exist_ok=True)
    if file.exists() and not backup.exists(): shutil.copy2(file,backup)
    E.delete_all_material_expressions(material)
else:
    material=u.AssetToolsHelpers.get_asset_tools().create_asset(name,folder,u.Material,u.MaterialFactoryNew())
material.set_editor_properties(dict(blend_mode=u.BlendMode.BLEND_OPAQUE,
    shading_model=u.MaterialShadingModel.MSM_DEFAULT_LIT,two_sided=False))

def node(cls,x,y,**props):
    result=E.create_material_expression(material,cls,x,y)
    if props: result.set_editor_properties(props)
    return result

uv=node(u.MaterialExpressionTextureCoordinate,-1000,0,coordinate_index=0)
samples={}
for i,(key,sampler) in enumerate([('Albedo',u.MaterialSamplerType.SAMPLERTYPE_COLOR),
        ('Normal',u.MaterialSamplerType.SAMPLERTYPE_NORMAL),('ORM',u.MaterialSamplerType.SAMPLERTYPE_MASKS)]):
    samples[key]=node(u.MaterialExpressionTextureSampleParameter2D,-750,i*250,
        texture=textures[key],parameter_name='Chest'+key,sampler_type=sampler)
    assert E.connect_material_expressions(uv,'',samples[key],'UVs')
tint=node(u.MaterialExpressionVectorParameter,-750,-200,parameter_name='ChestTint',default_value=u.LinearColor(1,1,1,1))
color=node(u.MaterialExpressionMultiply,-350,0)
assert E.connect_material_expressions(samples['Albedo'],'RGB',color,'A')
assert E.connect_material_expressions(tint,'RGB',color,'B')
assert E.connect_material_property(color,'',u.MaterialProperty.MP_BASE_COLOR)
assert E.connect_material_property(samples['Normal'],'RGB',u.MaterialProperty.MP_NORMAL)
assert E.connect_material_property(samples['ORM'],'R',u.MaterialProperty.MP_AMBIENT_OCCLUSION)
rough=node(u.MaterialExpressionClamp,-350,410,min_default=.28,max_default=.88)
assert E.connect_material_expressions(samples['ORM'],'G',rough,'')
assert E.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
metal_strength=node(u.MaterialExpressionScalarParameter,-750,780,
    parameter_name='ChestMetallicStrength',default_value=.75)
metal=node(u.MaterialExpressionMultiply,-350,650)
assert E.connect_material_expressions(samples['ORM'],'B',metal,'A')
assert E.connect_material_expressions(metal_strength,'',metal,'B')
assert E.connect_material_property(metal,'',u.MaterialProperty.MP_METALLIC)
spec=node(u.MaterialExpressionScalarParameter,-350,850,parameter_name='ChestSpecular',default_value=.35)
assert E.connect_material_property(spec,'',u.MaterialProperty.MP_SPECULAR)
E.recompile_material(material)
assert L.save_loaded_asset(material,only_if_is_dirty=False)
report=dict(asset=asset_path,
    scope='Owned source FBX + cyan atlas UV0 PBR candidate; no original package or actor defaults changed',
    meshes=meshes,
    imported_albedo_png=str(OUT/'Imported_Chest_BaseColor.png'),
    textures={key:dict(path=texture.get_path_name(),srgb=texture.get_editor_property('srgb'),
        compression=str(texture.get_editor_property('compression_settings'))) for key,texture in textures.items()},
    channels=dict(Albedo='UV0 RGB -> BaseColor',Normal='UV0 normal RGB -> Normal',
        ORM='R AO; G Roughness clamped .28..88; B Metallic × .75'))
(OUT/'Authoring.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_CHEST_GAMEPLAY_MATERIAL_CANDIDATE '+str(OUT/'Authoring.json'))
