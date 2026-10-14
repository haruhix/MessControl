"""Import Blender fracture variants without modifying the original arena assets."""
import json
from pathlib import Path
import unreal as u
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
root=Path(u.Paths.project_dir()).resolve()
source=root/'ContentSource/Meshes/ToothDamage'
folder='/Game/Gameplay/Arena/Damage'
artifacts=Path(globals().get('TOOTH_DAMAGE_ARTIFACTS',root/'Artifacts/ToothDamage_20261014'));artifacts.mkdir(parents=True,exist_ok=True)
tools=u.AssetToolsHelpers.get_asset_tools();lib=u.MaterialEditingLibrary
materials={}
for name,color,roughness in [('MC_Damage_Dentin',(.53,.68,.74),.72),('MC_Damage_Crack',(.018,.055,.09),.85)]:
    path=folder+'/M_'+name
    mat=u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset('M_'+name,folder,u.Material,u.MaterialFactoryNew())
    lib.delete_all_material_expressions(mat)
    mat.set_editor_property('two_sided',True)
    rgb=lib.create_material_expression(mat,u.MaterialExpressionConstant3Vector,-250,0)
    rgb.set_editor_property('constant',u.LinearColor(*color,1))
    rough=lib.create_material_expression(mat,u.MaterialExpressionConstant,-250,140);rough.set_editor_property('r',roughness)
    lib.connect_material_property(rgb,'',u.MaterialProperty.MP_BASE_COLOR);lib.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
    lib.recompile_material(mat);u.EditorAssetLibrary.save_loaded_asset(mat);materials[name]=mat
rows=[]
for row in json.loads((source/'Variants.json').read_text()):
    name=row['variant'];task=u.AssetImportTask()
    task.set_editor_properties({'filename':str(source/(name+'.fbx')),'destination_path':folder,'destination_name':name,
                               'automated':True,'replace_existing':True,'save':False,'factory':u.FbxFactory()})
    options=u.FbxImportUI();options.set_editor_properties({'import_mesh':True,'import_materials':False,'import_textures':False,
        'import_as_skeletal':False,'mesh_type_to_import':u.FBXImportType.FBXIT_STATIC_MESH,'automated_import_should_detect_type':False})
    options.static_mesh_import_data.set_editor_properties({'combine_meshes':True,'generate_lightmap_u_vs':False,'auto_generate_collision':False,
        'convert_scene':True,'convert_scene_unit':True,'normal_import_method':u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS})
    task.options=options;tools.import_asset_tasks([task])
    paths=task.get_editor_property('imported_object_paths');assert len(paths)==1,(name,paths)
    mesh=u.load_asset(paths[0]);slots=[]
    for i,slot in enumerate(mesh.get_editor_property('static_materials')):
        slotname=str(slot.material_slot_name);slots.append(slotname)
        mesh.set_material(i,materials.get(slotname,u.load_asset('/Game/Gameplay/Arena/V2/M_ArenaToothCareV2')))
    mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    u.EditorAssetLibrary.save_loaded_asset(mesh)
    box=mesh.get_bounding_box();original=u.load_asset(row['source']).get_bounding_box()
    size=box.max-box.min;original_size=original.max-original.min
    assert abs(box.min.z-original.min.z)<.1 and all(.55*b<a<1.02*b for a,b in zip(size.to_tuple(),original_size.to_tuple())),(name,box,original)
    rows.append({'mesh':mesh.get_path_name(),'slots':slots,'min':list(box.min.to_tuple()),'max':list(box.max.to_tuple())})
(artifacts/'ImportedAssets.json').write_text(json.dumps(rows,indent=2))
print('TOOTH_DAMAGE_IMPORTED',len(rows),rows[0])
