"""Import source headwear and merge zero-price wardrobe products without replacing existing items."""
import unreal as u,json,hashlib,shutil,sys
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();SRC=ROOT/'ArtSource/CharacterCurrent/HeadCosmetics20261024';OUT=ROOT/'Saved/HeadCosmetics20261024';OUT.mkdir(exist_ok=True)
DEST='/Game/Gameplay/CharacterCurrent/Cosmetics/Head20261024';lib=u.EditorAssetLibrary;edit=u.MaterialEditingLibrary;assets=u.AssetToolsHelpers.get_asset_tools();ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not ls.is_in_play_in_editor()
source=json.loads((SRC/'ExportReport.json').read_text(encoding='utf8'));assert len(source['items'])==7
owned_ids={x['id'] for x in source['items']};prior=json.loads((OUT/'Integration.json').read_text(encoding='utf8')) if (OUT/'Integration.json').exists() else None
assert hashlib.sha256(Path(source['source']).read_bytes()).hexdigest()==source['source_sha256']
paths=['/Game/Data/DT_CharacterCosmetics','/Game/Gameplay/Hub/Economy/DT_HubCatalog','/Game/Gameplay/UI/Data/DT_UI_Localization'];dirty={p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()};assert not dirty.intersection(paths)
tables={p:lib.load_asset(p) for p in paths};before={p:json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(t)) for p,t in tables.items()}
protected=['Content/Gameplay/CharacterCurrent/SK_CharacterGameplay.uasset','Content/Gameplay/CharacterCurrent/Cosmetics/SM_Cosmetic_WorkCap.uasset','Content/Art/Materials/M_TeethGameplay.uasset']
hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in protected}
for p in paths:
    f=ROOT/'Content'/(p[6:]+'.uasset');dest=OUT/'BeforePackages'/f.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True)
    if not dest.exists():shutil.copy2(f,dest)
(OUT/'BeforeTables.json').write_text(json.dumps(before,ensure_ascii=False,indent=2),encoding='utf8') if not (OUT/'BeforeTables.json').exists() else None
def save(a):assert lib.save_loaded_asset(a,only_if_is_dirty=False),a.get_path_name()
basepath=DEST+'/Materials/M_HeadCosmeticDeform';base=lib.load_asset(basepath) if lib.does_asset_exist(basepath) else lib.duplicate_asset('/Game/Gameplay/CharacterCurrent/Cosmetics/Materials/M_CosmeticBase',basepath);assert base
base.set_editor_property('two_sided',True)
if edit.get_material_property_input_node(base,u.MaterialProperty.MP_WORLD_POSITION_OFFSET) is None:
    for node in list(u.ObjectIterator(u.MaterialExpression)):
        if node.get_outer()!=base:continue
        if isinstance(node,(u.MaterialExpressionWorldPosition,u.MaterialExpressionCustom)) or (isinstance(node,(u.MaterialExpressionScalarParameter,u.MaterialExpressionVectorParameter)) and str(node.get_editor_property('parameter_name')).startswith('MCBody')):edit.delete_material_expression(base,node)
    world=edit.create_material_expression(base,u.MaterialExpressionWorldPosition,-700,750)
    def parameter(cls,name,index,value,y):
        n=edit.create_material_expression(base,cls,-700,y);n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',value);n.set_editor_property('use_custom_primitive_data',True);n.set_editor_property('primitive_data_index',index);return n
    amount=parameter(u.MaterialExpressionScalarParameter,'MCBodyStretch',0,0.,900)
    origin=parameter(u.MaterialExpressionVectorParameter,'MCBodyOrigin',1,u.LinearColor(0,0,0,0),1050)
    rotation=parameter(u.MaterialExpressionVectorParameter,'MCBodyRotation',5,u.LinearColor(0,0,0,1),1200)
    scale=parameter(u.MaterialExpressionVectorParameter,'MCBodyScale',9,u.LinearColor(1,1,1,0),1350)
    custom=edit.create_material_expression(base,u.MaterialExpressionCustom,-100,800);custom.set_editor_property('description','Match the current player BodyStretch in the body mesh frame after head attachment. CPD 0..12, no dynamic material allocation.');custom.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs=[]
    for name in ['P','S','O','Q','BScale']:
        i=u.CustomInput();i.set_editor_property('input_name',name);inputs.append(i)
    custom.set_editor_property('inputs',inputs)
    custom.set_editor_property('code','''float4 q=dot(Q,Q)>.5 ? Q*rsqrt(dot(Q,Q)) : float4(0,0,0,1);
float3 b=float3(abs(BScale.x)>1e-6?BScale.x:1,abs(BScale.y)>1e-6?BScale.y:1,abs(BScale.z)>1e-6?BScale.z:1);
float3 v=P-O.xyz; float3 iq=-q.xyz;
float3 p=(v+2*cross(iq,cross(iq,v)+q.w*v))/b;
float h=clamp(1+S,.65,1.4); float w=rsqrt(h);
float3 d=p*float3(w-1,w-1,h-1)*b;
return d+2*cross(q.xyz,cross(q.xyz,d)+q.w*d);''')
    for node,pin,output in [(world,'P',''),(amount,'S',''),(origin,'O','RGBA'),(rotation,'Q','RGBA'),(scale,'BScale','RGBA')]:assert edit.connect_material_expressions(node,output,custom,pin)
    assert edit.connect_material_property(custom,'',u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
assert not edit.recompile_material(base);save(base)
materials={}
for name,color in [('Neutral',(.8,.8,.8)),('Black',(.0074877683,)*3),('Brows',(.045,.023,.015))]:
    p=DEST+'/Materials/MI_Head_'+name;m=lib.load_asset(p) if lib.does_asset_exist(p) else assets.create_asset('MI_Head_'+name,DEST+'/Materials',u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew());edit.set_material_instance_parent(m,base);edit.set_material_instance_vector_parameter_value(m,'Color',u.LinearColor(*color,1));edit.set_material_instance_scalar_parameter_value(m,'Roughness',.5);edit.set_material_instance_scalar_parameter_value(m,'Metallic',0);edit.update_material_instance(m);save(m);materials[name]=m
previous=u.SystemLibrary.get_console_variable_int_value('Interchange.FeatureFlags.Import.FBX');u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
items=[]
try:
    for item in source['items']:
        task=u.AssetImportTask();task.filename=item['fbx'];task.destination_path=DEST;task.destination_name=item['mesh'];task.automated=True;task.replace_existing=True;task.save=False;task.factory=u.FbxFactory();opt=u.FbxImportUI();opt.automated_import_should_detect_type=False;opt.import_mesh=True;opt.import_as_skeletal=False;opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH;opt.import_materials=False;opt.import_textures=False;opt.static_mesh_import_data.combine_meshes=True;opt.static_mesh_import_data.auto_generate_collision=False;opt.static_mesh_import_data.normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS;task.options=opt;assets.import_asset_tasks([task]);mesh=lib.load_asset(DEST+'/'+item['mesh']);assert isinstance(mesh,u.StaticMesh)
        mesh.set_editor_property('positive_bounds_extension',u.Vector());mesh.set_editor_property('negative_bounds_extension',u.Vector());actual=mesh.get_bounds();expected=[hi-lo for lo,hi in zip(*item['bounds_unreal_cm'])];assert max(abs(x-y) for x,y in zip((actual.box_extent*2).to_tuple(),expected))<.05,(item['slug'],actual,expected)
        material=materials[item['material_role']];mesh.set_material(0,material);mesh.set_editor_property('positive_bounds_extension',u.Vector(80,80,80));mesh.set_editor_property('negative_bounds_extension',u.Vector(80,80,80));save(mesh)
        icon_task=u.AssetImportTask();icon_task.filename=item['icon'];icon_task.destination_path=DEST+'/Icons';icon_task.destination_name='T_'+item['id'];icon_task.automated=True;icon_task.replace_existing=True;icon_task.save=False;assets.import_asset_tasks([icon_task]);icon=lib.load_asset(DEST+'/Icons/T_'+item['id']);assert isinstance(icon,u.Texture2D);icon.set_editor_property('lod_group',u.TextureGroup.TEXTUREGROUP_UI);icon.set_editor_property('mip_gen_settings',u.TextureMipGenSettings.TMGS_NO_MIPMAPS);save(icon)
        items.append(dict(item,mesh_path=mesh.get_path_name(),material_path=material.get_path_name(),icon_path=icon.get_path_name(),price=0))
finally:u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX '+str(previous))
cosmetics={x['Name']:x for x in before[paths[0]]};catalog={x['Name']:x for x in before[paths[1]]}
for index,item in enumerate(items):
    ident=item['id'];assert (ident not in cosmetics and ident not in catalog) or (prior and prior['source_sha256']==source['source_sha256'] and ident in {x['id'] for x in prior['items']}),'Do not replace an unrelated product'
    cosmetics[ident]={'Name':ident,'Slot':item['slot'],'DisplayName':item['label_ru'],'Description':'Косметический аксессуар. Повторяет движения головы и растяжение персонажа.','Mesh':item['mesh_path'],'AttachBone':'head_x','bMeshSpaceFit':False,'LocalTransform':{'Rotation':{'X':0,'Y':0,'Z':0,'W':1},'Translation':{'X':0,'Y':0,'Z':0},'Scale3D':{'X':1,'Y':1,'Z':1}},'Materials':[item['material_path']],'bEnabled':True,'bDefaultUnlocked':False}
    catalog[ident]={'Name':ident,'bEnabled':True,'bInShop':True,'Kind':'Cosmetic','DisplayName':item['label_ru'],'Description':'Бесплатный аксессуар для шкафчика.','DisplayNameKey':'UI.HeadCosmetics.'+item['slug']+'.Name','DescriptionKey':'UI.HeadCosmetics.Description','Icon':item['icon_path'],'Rarity':'Common','Price':0,'PerkID':'None','CosmeticID':ident,'ConsumableID':'None','Quantity':1,'MaximumQueuedStacks':1,'DuplicateCoins':0,'SortOrder':30+index,'ReelSymbol':'None'}
for p,rows in [(paths[0],list(cosmetics.values())),(paths[1],list(catalog.values()))]:
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(tables[p],json.dumps(rows,ensure_ascii=False));after={x['Name']:x for x in json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(tables[p]))};assert all(after[x['Name']]==x for x in before[p] if x['Name'] not in owned_ids),'Unrelated catalog rows changed';save(tables[p])
sys.path.insert(0,str(ROOT/'Tools/Unreal/UI'));from localization_catalog import migrate_localization_row
localized=before[paths[2]];known={x['Name'].casefold() for x in localized};copy=[('UI.Product.Free','Бесплатно','Free'),('UI.Product.Get','Получить','Get'),('UI.HeadCosmetics.Description','Бесплатный аксессуар для шкафчика.','A free wardrobe accessory.')]+[('UI.HeadCosmetics.'+x['slug']+'.Name',x['label_ru'],x['label_en']) for x in items]
additions=[migrate_localization_row(dict(Name=k,RU=ru,EN=en,SourceText=ru,Context='Player head cosmetics from head_cosmetic.blend')) for k,ru,en in copy if k.casefold() not in known]
assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(tables[paths[2]],json.dumps(localized+additions,ensure_ascii=False));save(tables[paths[2]])
assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==v for p,v in hashes.items())
report={'status':'saved_pending_solo','source':source['source'],'source_sha256':source['source_sha256'],'items':items,'protected_hashes':hashes,'preload':'Existing MCPreloadManifest roots both catalogs in Hub and arena; each new row hard-references its mesh/material and references its thumbnail. CPD adds no dynamic material variants.','shader_deformation':'Exact BodyStretch affine function in the player mesh frame, supplied as per-component primitive data after actor/mesh updates.','unrelated_dirty_packages_preserved':sorted(dirty)}
(OUT/'Integration.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');print('HEAD_COSMETICS_IMPORTED_FREE',json.dumps([{'id':x['id'],'price':0,'vertices':x['vertices']} for x in items]))
