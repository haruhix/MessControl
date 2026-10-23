"""Import the fitted test accessories and editable cosmetic DT after an Editor build.

Does not replace the character, rig, morphs, map, bag palette or wardrobe tree.
Backs up every existing package it owns and merges only its three example rows.
"""
import hashlib, json, shutil, time
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
SOURCE=ROOT/'ArtSource/CharacterCurrent/Cosmetics'
DEST='/Game/Gameplay/CharacterCurrent/Cosmetics'
OUT=Path('E:/CodexWork/HubSystems_20261023/Cosmetics/UnrealAuthoring')/time.strftime('%Y%m%d_%H%M%S')
assert ROOT.drive.upper()=='E:' and SOURCE.drive.upper()=='E:' and OUT.drive.upper()=='E:'
OUT.mkdir(parents=True,exist_ok=False)
lib=u.EditorAssetLibrary;assets=u.AssetToolsHelpers.get_asset_tools();edit=u.MaterialEditingLibrary
def require(value,message):
    if not value:raise RuntimeError(message)
require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),'Stop PIE before cosmetic asset authoring.')
dirty={p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}
report={'source_report':str(SOURCE/'CosmeticsReport.json'),'backups':[],'created':[],'meshes':[],'rows':[]}
source_report=json.loads((SOURCE/'CosmeticsReport.json').read_text(encoding='utf8'))
require(u.load_object(None,'/Script/MessControl.MCCosmeticCatalogRow'),'Current Editor build is required.')

def backup(package):
    require(package not in dirty,'Refusing to change an unsaved package: '+package)
    found=False
    for suffix in ('.uasset','.uexp','.ubulk'):
        src=(ROOT/'Content'/package[len('/Game/'):]).with_suffix(suffix)
        if not src.is_file():continue
        found=True;dst=OUT/'BeforePackages'/src.relative_to(ROOT);dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
        digest=hashlib.sha256(src.read_bytes()).hexdigest();require(digest==hashlib.sha256(dst.read_bytes()).hexdigest(),'Backup mismatch: '+package)
        report['backups'].append({'package':package,'backup':str(dst),'sha256':digest})
    if not found:report['created'].append(package)

def save(asset):
    require(lib.save_loaded_asset(asset,only_if_is_dirty=False),'Failed to save '+asset.get_path_name())

base_path=DEST+'/Materials/M_CosmeticBase'
backup(base_path)
if lib.does_asset_exist(base_path):base=lib.load_asset(base_path)
else:
    base=assets.create_asset('M_CosmeticBase',DEST+'/Materials',u.Material,u.MaterialFactoryNew())
    require(base,'Could not create cosmetic material.')
    color=edit.create_material_expression(base,u.MaterialExpressionVectorParameter,-350,0);color.set_editor_property('parameter_name','Color');color.set_editor_property('default_value',u.LinearColor(.02,.3,.4,1))
    rough=edit.create_material_expression(base,u.MaterialExpressionScalarParameter,-350,180);rough.set_editor_property('parameter_name','Roughness');rough.set_editor_property('default_value',.5)
    metal=edit.create_material_expression(base,u.MaterialExpressionScalarParameter,-350,340);metal.set_editor_property('parameter_name','Metallic');metal.set_editor_property('default_value',0)
    require(edit.connect_material_property(color,'',u.MaterialProperty.MP_BASE_COLOR),'Base color connection failed.')
    require(edit.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS),'Roughness connection failed.')
    require(edit.connect_material_property(metal,'',u.MaterialProperty.MP_METALLIC),'Metallic connection failed.')
    require(not edit.recompile_material(base),'Cosmetic material failed to compile.')
save(base)
materials={}
for name,color,rough,metal in [('MI_WorkTeal',(.025,.47,.56),.52,.05),('MI_DeepMarine',(.012,.035,.055),.36,.15),('MI_CreamGold',(.98,.64,.055),.3,.2),('MI_WorkCoral',(.66,.055,.035),.52,.03)]:
    path=DEST+'/Materials/'+name;backup(path)
    material=lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset(name,DEST+'/Materials',u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
    require(material,'Could not create '+name);edit.set_material_instance_parent(material,base)
    edit.set_material_instance_vector_parameter_value(material,'Color',u.LinearColor(*color,1))
    edit.set_material_instance_scalar_parameter_value(material,'Roughness',rough);edit.set_material_instance_scalar_parameter_value(material,'Metallic',metal)
    edit.update_material_instance(material);save(material);materials[name]=material

u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
meshes={}
for entry in source_report['items']:
    name=entry['mesh'];path=DEST+'/'+name;backup(path)
    task=u.AssetImportTask();task.filename=str(SOURCE/(name+'.fbx'));task.destination_path=DEST;task.destination_name=name;task.automated=True;task.replace_existing=True;task.replace_existing_settings=True;task.save=False;task.factory=u.FbxFactory()
    options=u.FbxImportUI();options.import_mesh=True;options.import_as_skeletal=False;options.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH;options.import_materials=False;options.import_textures=False;options.import_animations=False
    data=options.static_mesh_import_data;data.set_editor_property('combine_meshes',True);data.set_editor_property('auto_generate_collision',False);data.set_editor_property('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    task.options=options;assets.import_asset_tasks([task]);mesh=lib.load_asset(path);require(isinstance(mesh,u.StaticMesh),'Mesh import failed: '+path)
    actual=mesh.get_bounds();actual_size=[actual.box_extent.x*2,actual.box_extent.y*2,actual.box_extent.z*2];expected_size=[hi-lo for lo,hi in zip(*entry['bounds_cm'])]
    require(all(abs(a-b)<.03 for a,b in zip(actual_size,expected_size)),'Imported dimensions differ: '+str((name,actual_size,expected_size)))
    slots=['MI_WorkTeal','MI_DeepMarine'] if name.endswith('WorkCap') else ['MI_DeepMarine','MI_CreamGold']
    require(len(mesh.get_editor_property('static_materials'))==2,'Unexpected cosmetic material slots: '+name)
    for index,material_name in enumerate(slots):mesh.set_material(index,materials[material_name])
    save(mesh);meshes[name]=mesh;report['meshes'].append({'path':path,'dimensions_cm':actual_size})

table_path='/Game/Data/DT_CharacterCosmetics';backup(table_path)
if lib.does_asset_exist(table_path):table=lib.load_asset(table_path)
else:
    factory=u.DataTableFactory();factory.set_editor_property('struct',u.load_object(None,'/Script/MessControl.MCCosmeticCatalogRow'))
    table=assets.create_asset('DT_CharacterCosmetics','/Game/Data',u.DataTable,factory)
require(table,'Could not create cosmetic table.')
before=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table));by_id={row['Name']:row for row in before}
for item,slot,label,mesh_name,mat_names in [
    ('Hat_WorkCap','Hat','Рабочая кепка','SM_Cosmetic_WorkCap',['MI_WorkTeal','MI_DeepMarine']),
    ('Hat_WorkCapRed','Hat','Коралловая кепка','SM_Cosmetic_WorkCap',['MI_WorkCoral','MI_DeepMarine']),
    ('Face_SportGlasses','Face','Круглые очки','SM_Cosmetic_SportGlasses',['MI_DeepMarine','MI_CreamGold'])]:
    by_id[item]={'Name':item,'Slot':slot,'DisplayName':label,'Description':'Тестовый аксессуар','Mesh':meshes[mesh_name].get_path_name(),'AttachBone':'head_x','bMeshSpaceFit':True,
        'LocalTransform':{'Rotation':{'X':0,'Y':0,'Z':0,'W':1},'Translation':{'X':0,'Y':0,'Z':0},'Scale3D':{'X':1,'Y':1,'Z':1}},
        'Materials':[materials[n].get_path_name() for n in mat_names],'bEnabled':True,'bDefaultUnlocked':False}
    report['rows'].append(item)
require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(list(by_id.values()),ensure_ascii=False)),'Cosmetic DT import failed.')
require(set(report['rows'])<=set(str(n) for n in u.DataTableFunctionLibrary.get_data_table_row_names(table)),'Cosmetic rows missing after import.')
save(table)
report['status']='saved';report['deferred']='Runtime/solo PIE is performed by the root agent; this script does not run Play or change any map.'
(OUT/'CosmeticsAuthoringReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
u.log('MC_COSMETICS_AUTHORING_PASS '+str(OUT/'CosmeticsAuthoringReport.json'))
