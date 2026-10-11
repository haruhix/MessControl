"""Import candy.blend's two coloured parts and author Substrate ice materials."""
import json
from pathlib import Path
import unreal as u

root=Path(u.Paths.project_dir()).resolve()
out=root/'Artifacts/WinterCandy'
out.mkdir(parents=True,exist_ok=True)
folder='/Game/Gameplay/Cold/Candy'
lib,e=u.EditorAssetLibrary,u.MaterialEditingLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
report={'materials':[]}
if '-run=pythonscript' not in u.SystemLibrary.get_command_line().lower():
    assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()

def node(mat,cls,x,y,**props):
    result=e.create_material_expression(mat,cls,x,y)
    result.set_editor_properties(props)
    return result

def link(src,pin,dst,input_name=''):
    assert e.connect_material_expressions(src,pin,dst,input_name),input_name

def custom(mat,description,code,inputs,x,y,scalar=False):
    result=node(mat,u.MaterialExpressionCustom,x,y,description=description,code=code,
        output_type=u.CustomMaterialOutputType.CMOT_FLOAT1 if scalar else u.CustomMaterialOutputType.CMOT_FLOAT3)
    entries=[]
    for name in inputs:
        entry=u.CustomInput();entry.set_editor_property('input_name',name);entries.append(entry)
    result.set_editor_property('inputs',entries)
    for name,(src,pin) in inputs.items():link(src,pin,result,name)
    return result

legacy=e.get_material_property_input_node(u.load_asset('/Game/Art/Materials/ice/M_Ice'),u.MaterialProperty.MP_FRONT_MATERIAL).get_editor_property('material_function')
materials={}
for role in ('Body','Face'):
    name='M_WinterCandyIce'+role
    path=folder+'/'+name
    if lib.does_asset_exist(path):
        mat=lib.load_asset(path)
        materials[role]=mat
        report['materials'].append(mat.get_path_name())
        continue
    mat=assets.create_asset(name,folder,u.Material,u.MaterialFactoryNew())
    body=role=='Body'
    mat.set_editor_properties({'blend_mode':u.BlendMode.BLEND_TRANSLUCENT if body else u.BlendMode.BLEND_OPAQUE,
        'two_sided':False,'translucency_lighting_mode':u.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING})
    front=node(mat,u.MaterialExpressionMaterialFunctionCall,1150,0,
        desc='Substrate ice surface; original painted vertex colours are retained.')
    assert front.set_material_function(legacy)
    vertex=node(mat,u.MaterialExpressionVertexColor,-1400,0,desc='Artist Attribute: RGB drives ice colour and face details.')
    world=node(mat,u.MaterialExpressionWorldPosition,-1400,250)
    local=node(mat,u.MaterialExpressionTransformPosition,-1150,250,
        transform_source_type=u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
        transform_type=u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    link(world,'',local)
    rim=node(mat,u.MaterialExpressionFresnel,-1150,510,exponent=3.6,base_reflect_fraction=.03)
    frost=node(mat,u.MaterialExpressionScalarParameter,-1150,720,parameter_name='Frost Amount',
        group='Ice',default_value=.28 if body else .12,slider_min=0.0,slider_max=1.0)
    field=custom(mat,'Uneven frost and fine ice veins fixed to the mesh',
        'float n=sin(P.x*.17+sin(P.z*.095)*2.8)*sin(P.y*.22+P.z*.075);\n'
        'float patches=smoothstep(.12,.78,n*.5+.5);\n'
        'float grain=sin(dot(P,float3(3.8,5.2,2.9)))*sin(dot(P,float3(5.3,-3.7,4.1)));\n'
        'float vein=1-smoothstep(.012,.07,abs(sin(P.z*.18+sin(P.y*.15)*1.3+sin(P.x*.13))));\n'
        'return float3(saturate(Frost*(.18+.82*patches)+vein*.09),vein,grain*.5+.5);',
        {'P':(local,''),'Frost':(frost,'')},-720,200)
    colour=custom(mat,'Vertex colour under a thin frosted ice surface',
        'float brightness=max(max(Paint.r,Paint.g),Paint.b);\n'
        'float protection=smoothstep(.16,.8,brightness);\n'
        'float coverage=saturate(Field.x*.50+Rim*.08)*protection;\n'
        'return lerp(Paint*float3(.87,.97,1.04),float3(.72,.9,1),coverage);',
        {'Paint':(vertex,''),'Field':(field,''),'Rim':(rim,'')},-150,0)
    rough=custom(mat,'Polished ice interrupted by rough frost',
        'return clamp(.13+Field.x*.46+Field.z*.035,.12,.55);',{'Field':(field,'')},-150,280,True)
    spec=node(mat,u.MaterialExpressionConstant,450,480,r=.62)
    link(colour,'',front,'Base Color');link(rough,'',front,'Roughness');link(spec,'',front,'Specular')
    # Keep the face opaque so dark eyes, mouth and pale teeth survive the glass shell.
    if body:
        opacity=custom(mat,'Dense blue ice becomes cloudy in frosted patches',
            'return clamp(.68+Field.x*.24+Rim*.14,.65,.94);',{'Field':(field,''),'Rim':(rim,'')},200,650,True)
        link(opacity,'',front,'Opacity')
        assert e.connect_material_property(opacity,'',u.MaterialProperty.MP_OPACITY)
    assert e.connect_material_property(front,'Result',u.MaterialProperty.MP_FRONT_MATERIAL)
    errors=list(e.recompile_material(mat));assert not errors,errors
    assert lib.save_loaded_asset(mat,only_if_is_dirty=False)
    materials[role]=mat
    report['materials'].append(mat.get_path_name())

u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
task=u.AssetImportTask()
task.filename=str(root/'ArtSource/WinterCandy/SM_WinterCandy.fbx')
task.destination_path=folder;task.destination_name='SM_WinterCandy'
task.automated=True;task.replace_existing=True;task.replace_existing_settings=True
task.save=False;task.factory=u.FbxFactory()
options=u.FbxImportUI()
options.import_mesh=True;options.import_as_skeletal=False
options.automated_import_should_detect_type=False
options.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
options.import_materials=False;options.import_textures=False;options.import_animations=False
data=options.static_mesh_import_data
data.set_editor_properties({'combine_meshes':True,'auto_generate_collision':False,
    'vertex_color_import_option':u.VertexColorImportOption.REPLACE,
    'normal_import_method':u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
    'generate_lightmap_u_vs':False,'build_nanite':False,'convert_scene_unit':True,
    'import_uniform_scale':1.0})
task.options=options
# Legacy FBX reimport uses the mesh's stored import data instead of task options.
if lib.does_asset_exist(folder+'/SM_WinterCandy'):
    previous=lib.load_asset(folder+'/SM_WinterCandy').get_editor_property('asset_import_data')
    previous.set_editor_properties({'import_uniform_scale':1.0,'convert_scene_unit':True,
        'vertex_color_import_option':u.VertexColorImportOption.REPLACE,
        'combine_meshes':True,'auto_generate_collision':False,'generate_lightmap_u_vs':False})
assets.import_asset_tasks([task])
mesh=lib.load_asset(folder+'/SM_WinterCandy');assert isinstance(mesh,u.StaticMesh),task.imported_object_paths
bounds=mesh.get_bounds()
source_report=json.loads((root/'ArtSource/WinterCandy/ExportReport.json').read_text(encoding='utf8'))
expected=source_report['source_dimensions_cm']
actual=[float(bounds.box_extent.x*2),float(bounds.box_extent.y*2),float(bounds.box_extent.z*2)]
assert all(abs(a-b)<.01 for a,b in zip(actual,expected)),(actual,expected)
report['source_dimensions_cm']=expected
report['imported_dimensions_cm']=actual
report['authored_size_preserved']=True
slots=list(mesh.get_editor_property('static_materials'));assert len(slots)==2,str(slots)
report['slots']=[]
for index,slot in enumerate(slots):
    slot_name=str(slot.get_editor_property('imported_material_slot_name'))
    role='Face' if 'Face' in slot_name else 'Body'
    mesh.set_material(index,materials[role])
    report['slots'].append({'name':slot_name,'material':materials[role].get_path_name()})
editor=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
assert editor.has_vertex_colors(mesh), 'FBX vertex colours must reach the Unreal render mesh.'
assert editor.set_convex_decomposition_collisions(mesh,1,32,100000)
mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX)
assert lib.save_loaded_asset(mesh,only_if_is_dirty=False)
report['mesh']=mesh.get_path_name()
report['bounds']=str(bounds)
report['triangles']=mesh.get_num_triangles(0)
report['vertex_colors']=editor.has_vertex_colors(mesh)
report['collision_hulls']=len(mesh.get_editor_property('body_setup').get_editor_property('agg_geom').get_editor_property('convex_elems'))
report['dirty']=[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
(out/'ImportReport.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('WINTER_CANDY_SAVED',json.dumps(report))
