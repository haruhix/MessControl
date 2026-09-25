"""Create only new day-one assets. Never regenerate L_Mouth or replace authored meshes."""
import json
import os
from pathlib import Path
import unreal as u

root=Path(u.Paths.project_dir()).resolve()
lib=u.EditorAssetLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
edit=u.MaterialEditingLibrary
colors={'Green':(.08,.3,.018), 'Stem':(.3,.48,.08), 'EggWhite':(.95,.86,.64), 'Yolk':(1,.4,.015),
        'Bacon':(.5,.06,.025), 'Fat':(.95,.54,.25), 'Carrot':(.95,.2,.015), 'Fibre':(.56,.34,.1)}
mats={}
for name, color in colors.items():
    path='/Game/Art/Materials/M_Breakfast_'+name
    mat=lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset('M_Breakfast_'+name,'/Game/Art/Materials',u.Material,u.MaterialFactoryNew())
    # Only add nodes to an empty newly created material.
    if edit.get_num_material_expressions(mat)==0:
        v=edit.create_material_expression(mat,u.MaterialExpressionVectorParameter,-300,0)
        v.set_editor_property('parameter_name','Tint'); v.set_editor_property('default_value',u.LinearColor(*color,1))
        edit.connect_material_property(v,'',u.MaterialProperty.MP_BASE_COLOR)
        r=edit.create_material_expression(mat,u.MaterialExpressionConstant,-300,120); r.set_editor_property('r',.35)
        edit.connect_material_property(r,'',u.MaterialProperty.MP_ROUGHNESS)
        edit.recompile_material(mat); lib.save_loaded_asset(mat)
    mats[name]=mat
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
for file in sorted((root/'ArtSource'/'Breakfast').glob('*.fbx')):
    path='/Game/Art/Meshes/Breakfast/'+file.stem
    if lib.does_asset_exist(path): continue
    task=u.AssetImportTask(); task.filename=str(file); task.destination_path='/Game/Art/Meshes/Breakfast'
    task.destination_name=file.stem; task.automated=True; task.save=True
    opt=u.FbxImportUI(); opt.import_mesh=True; opt.import_as_skeletal=False; opt.import_materials=False; opt.import_textures=False
    opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH; opt.static_mesh_import_data.combine_meshes=True
    opt.static_mesh_import_data.auto_generate_collision=False; opt.static_mesh_import_data.generate_lightmap_u_vs=False
    task.options=opt; task.factory=u.FbxFactory(); assets.import_asset_tasks([task])
    mesh=lib.load_asset(path)
    if not mesh: raise RuntimeError('Failed import '+path)
    for i,slot in enumerate(mesh.get_editor_property('static_materials')):
        name=str(slot.get_editor_property('material_slot_name')).split('.')[0]
        if name in mats: mesh.set_material(i,mats[name])
    lib.save_loaded_asset(mesh,only_if_is_dirty=False)

path='/Game/Art/Materials/M_CoffeeLiquid'
if not lib.does_asset_exist(path) or os.environ.get('MC_REBUILD_LIQUID')=='1':
    mat=lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset('M_CoffeeLiquid','/Game/Art/Materials',u.Material,u.MaterialFactoryNew())
    edit.delete_all_material_expressions(mat)
    mat.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('two_sided',True)
    for prop,color in [(u.MaterialProperty.MP_BASE_COLOR,(.1,.026,.005)),(u.MaterialProperty.MP_EMISSIVE_COLOR,(.025,.006,.001))]:
        v=edit.create_material_expression(mat,u.MaterialExpressionConstant3Vector,-300,0)
        v.set_editor_property('constant',u.LinearColor(*color,1)); edit.connect_material_property(v,'',prop)
    for prop,val in [(u.MaterialProperty.MP_OPACITY,.58),(u.MaterialProperty.MP_ROUGHNESS,.12)]:
        c=edit.create_material_expression(mat,u.MaterialExpressionConstant,-300,120); c.set_editor_property('r',val); edit.connect_material_property(c,'',prop)
    # Small surface bob; no fluid solver or collision mesh rebuild.
    t=edit.create_material_expression(mat,u.MaterialExpressionTime,-650,400)
    sine=edit.create_material_expression(mat,u.MaterialExpressionSine,-450,400)
    if not edit.connect_material_expressions(t,'',sine,''): raise RuntimeError('Liquid Time -> Sine connection failed')
    amp=edit.create_material_expression(mat,u.MaterialExpressionConstant3Vector,-450,520); amp.set_editor_property('constant',u.LinearColor(0,0,1.5,1))
    mul=edit.create_material_expression(mat,u.MaterialExpressionMultiply,-250,400); edit.connect_material_expressions(sine,'',mul,'A'); edit.connect_material_expressions(amp,'',mul,'B')
    edit.connect_material_property(mul,'',u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    edit.recompile_material(mat); lib.save_loaded_asset(mat,only_if_is_dirty=False)

path='/Game/Data/DT_BreakfastMenu'
if not lib.does_asset_exist(path):
    factory=u.DataTableFactory(); factory.set_editor_property('struct',u.load_object(None,'/Script/MessControl.MCFoodRow'))
    table=assets.create_asset('DT_BreakfastMenu','/Game/Data',u.DataTable,factory)
    rows=[]
    for name,hp,mass,rot,extent,weight in [('Broccoli',75,12,35,(45,40,40),1),('Egg',50,5,25,(48,35,18),1),('Bacon',100,9,40,(52,26,14),1),('Carrot',75,7,45,(45,20,20),.7),('Fibre',50,6,35,(55,24,15),0)]:
        def paths(part): return [f'/Game/Art/Meshes/Breakfast/SM_{name}_{part}_{v}.SM_{name}_{part}_{v}' for v in 'ABC']
        rows.append(dict(Name=name,Label=name.upper(),WholeMeshes=paths('Whole'),FragmentMeshes=paths('Part'),
            SelectionWeight=weight,Health=hp,Mass=mass,SpoilSeconds=rot,Fragments=3,
            HalfExtent=dict(X=extent[0],Y=extent[1],Z=extent[2])))
    if not u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(rows)): raise RuntimeError('Menu import failed')
    lib.save_loaded_asset(table,only_if_is_dirty=False)
path='/Game/Data/DA_Day01'
if not lib.does_asset_exist(path):
    factory=u.DataAssetFactory(); factory.set_editor_property('data_asset_class',u.MCDayPlan)
    plan=assets.create_asset('DA_Day01','/Game/Data',u.MCDayPlan,factory)
    lib.save_loaded_asset(plan,only_if_is_dirty=False)
u.log('MC_DAY_ONE_ASSETS_PASS')
