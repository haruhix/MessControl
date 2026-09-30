"""Import Painter maps and connect the artist's live mouth and hazard menu. PIE stopped."""
import json
from pathlib import Path
import unreal as u

lib=u.EditorAssetLibrary
edit=u.MaterialEditingLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before integration'
if not any(isinstance(a,u.MCThroat) for a in actors.get_all_level_actors()):
    assert u.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_Mouth')
root=Path(u.Paths.project_dir()).resolve()
folder='/Game/Gameplay/MouthV3'
hazards='/Game/Gameplay/Hazards'
for path in (folder,folder+'/Textures',hazards): lib.make_directory(path)

def save(obj):
    assert lib.save_loaded_asset(obj,only_if_is_dirty=False),obj.get_path_name()
def material(name,destination):
    path=destination+'/'+name
    mat=lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset(name,destination,u.Material,u.MaterialFactoryNew())
    edit.delete_all_material_expressions(mat)
    return mat
def node(mat,kind,**props):
    n=edit.create_material_expression(mat,kind)
    for key,value in props.items(): n.set_editor_property(key,value)
    return n
def scalar(mat,name,value): return node(mat,u.MaterialExpressionScalarParameter,parameter_name=name,default_value=value)
def vector(mat,name,value): return node(mat,u.MaterialExpressionVectorParameter,parameter_name=name,default_value=u.LinearColor(*value,1))
def connect(src,pin,dst,inp): assert edit.connect_material_expressions(src,pin,dst,inp)
def output(src,prop,pin=''): assert edit.connect_material_property(src,pin,prop)
def finish(mat):
    assert not edit.recompile_material(mat)
    save(mat)

maps={}
for tissue in ('Gum','Palate','Exit'):
    textures={}
    for channel in ('BaseColor','OcclusionRoughnessMetallic','Normal'):
        files=list((root/'ArtSource/MouthV2/Textures'/tissue).glob('*_'+channel+'.png'))
        assert len(files)==1,(tissue,channel,files)
        name='T_'+tissue+'_'+channel
        task=u.AssetImportTask(); task.filename=str(files[0]); task.destination_path=folder+'/Textures'; task.destination_name=name
        task.automated=True; task.save=True; task.replace_existing=True
        assets.import_asset_tasks([task])
        texture=lib.load_asset(folder+'/Textures/'+name)
        assert texture
        if channel=='OcclusionRoughnessMetallic':
            texture.set_editor_property('srgb',False); texture.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_MASKS)
        elif channel=='Normal':
            texture.set_editor_property('srgb',False); texture.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP)
        save(texture); textures[channel]=texture
    mat=material('M_'+tissue+'V3',folder)
    mat.set_editor_property('two_sided',True)
    mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_SUBSURFACE)
    for channel,param,sampler,prop,pin in [
        ('BaseColor','TissueColor',u.MaterialSamplerType.SAMPLERTYPE_COLOR,u.MaterialProperty.MP_BASE_COLOR,'RGB'),
        ('OcclusionRoughnessMetallic','TissueARM',u.MaterialSamplerType.SAMPLERTYPE_MASKS,u.MaterialProperty.MP_ROUGHNESS,'G'),
        ('Normal','TissueNormal',u.MaterialSamplerType.SAMPLERTYPE_NORMAL,u.MaterialProperty.MP_NORMAL,'RGB')]:
        n=node(mat,u.MaterialExpressionTextureSampleParameter2D,parameter_name=param,texture=textures[channel],sampler_type=sampler)
        output(n,prop,pin)
    output(scalar(mat,'Specular',.48),u.MaterialProperty.MP_SPECULAR)
    output(vector(mat,'ScatterColor',(.24,.025,.04)),u.MaterialProperty.MP_SUBSURFACE_COLOR)
    output(scalar(mat,'ScatterAmount',.25),u.MaterialProperty.MP_OPACITY)
    if tissue=='Exit':
        edit.set_base_material_usage(mat,u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
        edit.set_base_material_usage(mat,u.MaterialUsage.MATUSAGE_MORPH_TARGETS)
    finish(mat); maps[tissue]=mat

for a in actors.get_all_level_actors():
    if isinstance(a,u.StaticMeshActor) and a.static_mesh_component.static_mesh:
        c=a.static_mesh_component; name=c.static_mesh.get_name()
        if name in ('SM_Gum','SM_Roof_Wall'):
            c.set_material(0,maps['Gum' if name=='SM_Gum' else 'Palate'])

throats=[a for a in actors.get_all_level_actors() if isinstance(a,u.MCThroat)]
assert len(throats)==1
throat=throats[0]
source=next(a for a in actors.get_all_level_actors() if isinstance(a,u.SkeletalMeshActor) and a.skeletal_mesh_component.skeletal_mesh_asset and a.skeletal_mesh_component.skeletal_mesh_asset.get_name()=='SK_Exit')
mouth=throat.get_editor_property('authored_mouth')
mouth.set_skeletal_mesh_asset(source.skeletal_mesh_component.skeletal_mesh_asset)
mouth.set_world_transform(source.skeletal_mesh_component.get_world_transform(),False,True)
mouth.set_material(0,maps['Exit']); mouth.set_visibility(True)
mouth.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
source.set_actor_hidden_in_game(True); source.skeletal_mesh_component.set_visibility(False)
throat.call_method('RebuildAppearance')

# Preserve the original authored uvula and its procedural tissue detail.
uvula=lib.load_asset('/Game/Art/Materials/MI_MouthPalate')
edit.set_material_instance_scalar_parameter_value(uvula,'MicroNormalStrength',.22)
edit.set_material_instance_scalar_parameter_value(uvula,'RoughnessBias',.14)
edit.set_material_instance_scalar_parameter_value(uvula,'Specular',.48)
edit.set_material_instance_vector_parameter_value(uvula,'TissueTint',u.LinearColor(.85,.65,.72,1))
edit.update_material_instance(uvula); save(uvula)

pepper=material('M_SpicyPepper',hazards)
color=node(pepper,u.MaterialExpressionTextureSampleParameter2D,parameter_name='PepperColor',texture=lib.load_asset('/Game/Stylized_Vegetables/Textures/T_Chili_C'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
urgency=scalar(pepper,'Urgency',0); flash=scalar(pepper,'Flash',0)
lerp=node(pepper,u.MaterialExpressionLinearInterpolate)
connect(color,'RGB',lerp,'A'); connect(vector(pepper,'HotRed',(.9,.005,.002)),'',lerp,'B'); connect(urgency,'',lerp,'Alpha')
output(lerp,u.MaterialProperty.MP_BASE_COLOR)
mul=node(pepper,u.MaterialExpressionMultiply); connect(flash,'',mul,'A'); connect(urgency,'',mul,'B')
emission=node(pepper,u.MaterialExpressionMultiply); connect(mul,'',emission,'A'); connect(vector(pepper,'Glow',(.65,.002,.001)),'',emission,'B')
output(emission,u.MaterialProperty.MP_EMISSIVE_COLOR); output(scalar(pepper,'Roughness',.35),u.MaterialProperty.MP_ROUGHNESS)
finish(pepper)

path=hazards+'/MI_VomitPuddle'
puddle=lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset('MI_VomitPuddle',hazards,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
edit.set_material_instance_parent(puddle,lib.load_asset('/Game/Gameplay/Liquid/M_CoffeePuddle'))
for name,value in [('LiquidColor',(.22,.16,.018,1)),('EdgeColor',(.36,.25,.028,1))]: edit.set_material_instance_vector_parameter_value(puddle,name,u.LinearColor(*value))
for name,value in [('Roughness',.26),('Depth',1.2),('WetSheen',.25)]: edit.set_material_instance_scalar_parameter_value(puddle,name,value)
edit.update_material_instance(puddle); save(puddle)

table=lib.load_asset('/Game/Data/DT_BreakfastMenu')
rows=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
row=dict(Name='SpicyPepper',Label='SPICY PEPPER',Kind='Spicy',Resistance='Soft',SelectionWeight=.35,
         WholeMeshes=['/Game/Stylized_Vegetables/Meshes/SM_Chili.SM_Chili'],
         FragmentMeshes=['/Game/Stylized_Vegetables/Meshes/SM_ChiliCutA.SM_ChiliCutA','/Game/Stylized_Vegetables/Meshes/SM_ChiliCutB.SM_ChiliCutB'],
         Scale=dict(X=2,Y=2,Z=2),HalfExtent=dict(X=40,Y=20,Z=20),Mass=4,Health=50,SpoilSeconds=600,
         FuseSeconds=8,FirstPulseRadius=180,RadiusPerRound=90,PulseDamage=18,Fragments=2)
rows=[r for r in rows if r['Name']!='SpicyPepper']+[row]
assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(rows,ensure_ascii=False))
save(table)
assert u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
report=dict(materials={k:v.get_path_name() for k,v in maps.items()},mouth=mouth.get_path_name(),source=source.get_path_name(),uvula=throat.get_editor_property('uvula').static_mesh.get_path_name(),menu=[r['Name'] for r in rows])
Path(u.Paths.project_saved_dir(),'GameplayV3Integration.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_GAMEPLAY_V3_ASSETS_PASS')
