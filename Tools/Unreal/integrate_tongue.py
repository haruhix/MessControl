"""Install the derived tongue and shell; preserve the artist's original assets.
Requires prepare_tongue.py and a build containing AMCTongue.
"""
from pathlib import Path
import unreal as u
root=Path(u.Paths.project_dir()).resolve()
lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
folder='/Game/Gameplay/Arena'
def save(a):
    if not lib.save_loaded_asset(a,only_if_is_dirty=False): raise RuntimeError('Cannot save '+a.get_path_name())
def link(a,b,pin='',output=''):
    if not edit.connect_material_expressions(a,output,b,pin): raise RuntimeError('Cannot connect '+pin)

u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
for name in ('SM_MouthShell','SM_TongueSurface'):
    path=folder+'/'+name
    if not lib.does_asset_exist(path):
        task=u.AssetImportTask(); task.filename=str(root/'ArtSource/ArenaGameplay'/(name+'.fbx'))
        task.destination_path=folder; task.destination_name=name; task.automated=True; task.save=True
        opt=u.FbxImportUI(); opt.import_mesh=True; opt.import_as_skeletal=False; opt.import_materials=False; opt.import_textures=False
        opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
        opt.static_mesh_import_data.combine_meshes=True; opt.static_mesh_import_data.auto_generate_collision=False
        opt.static_mesh_import_data.generate_lightmap_u_vs=False
        task.options=opt; task.factory=u.FbxFactory(); assets.import_asset_tasks([task])
    mesh=lib.load_asset(path)
    if not mesh: raise RuntimeError('Missing '+path)
    mesh.set_editor_property('allow_cpu_access',True)
    body=mesh.get_editor_property('body_setup'); body.set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    body.set_editor_property('double_sided_geometry',True)
    material=lib.load_asset('/Game/Art/Materials/Arena/'+('MI_Arena' if name=='SM_MouthShell' else 'MI_Tong'))
    mesh.set_material(0,material); save(mesh)

matpath=folder+'/M_TonguePain'
if not lib.does_asset_exist(matpath):
    original=lib.load_asset('/Game/Art/Materials/Arena/MI_Tong')
    mat=lib.duplicate_asset(original.get_editor_property('parent').get_path_name(),matpath)
    base=edit.get_material_property_input_node(mat,u.MaterialProperty.MP_BASE_COLOR)
    output=edit.get_material_property_input_node_output_name(mat,u.MaterialProperty.MP_BASE_COLOR)
    if not base: raise RuntimeError('Expected artist BaseColor graph')
    vc=edit.create_material_expression(mat,u.MaterialExpressionVertexColor,800,1800)
    red=edit.create_material_expression(mat,u.MaterialExpressionVectorParameter,800,2000)
    red.set_editor_property('parameter_name','PainColor'); red.set_editor_property('default_value',u.LinearColor(.8,.006,.012,1)); red.set_editor_property('group','Pain wave')
    blend=edit.create_material_expression(mat,u.MaterialExpressionLinearInterpolate,1100,1600)
    link(base,blend,'A',output); link(red,blend,'B'); link(vc,blend,'Alpha','R')
    edit.connect_material_property(blend,'',u.MaterialProperty.MP_BASE_COLOR)
    glow=edit.create_material_expression(mat,u.MaterialExpressionMultiply,1100,1900)
    link(red,glow,'A'); link(vc,glow,'B','R')
    strength=edit.create_material_expression(mat,u.MaterialExpressionScalarParameter,1100,2100)
    strength.set_editor_property('parameter_name','PainGlow'); strength.set_editor_property('default_value',.25); strength.set_editor_property('group','Pain wave')
    multiply=edit.create_material_expression(mat,u.MaterialExpressionMultiply,1350,1900); link(glow,multiply,'A'); link(strength,multiply,'B')
    emission=edit.get_material_property_input_node(mat,u.MaterialProperty.MP_EMISSIVE_COLOR)
    if emission:
        add=edit.create_material_expression(mat,u.MaterialExpressionAdd,1550,1900)
        link(emission,add,'A',edit.get_material_property_input_node_output_name(mat,u.MaterialProperty.MP_EMISSIVE_COLOR)); link(multiply,add,'B'); multiply=add
    edit.connect_material_property(multiply,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    errors=edit.recompile_material(mat)
    if errors: raise RuntimeError(str(errors))
    save(mat)
    mi=lib.duplicate_asset(original.get_path_name(),folder+'/MI_TonguePain')
    edit.set_material_instance_parent(mi,None); edit.set_material_instance_parent(mi,mat); edit.update_material_instance(mi); save(mi)
mi=lib.load_asset(folder+'/MI_TonguePain')
if not lib.does_asset_exist('/Game/Data/DA_Tongue'):
    factory=u.DataAssetFactory(); factory.set_editor_property('data_asset_class',u.MCTongueProfile)
    profile=assets.create_asset('DA_Tongue','/Game/Data',u.MCTongueProfile,factory); save(profile)
profile=lib.load_asset('/Game/Data/DA_Tongue')
if not levels.load_level('/Game/Maps/L_Mouth'): raise RuntimeError('Map load failed')
all_actors=actors.get_all_level_actors()
shell=next(a for a in all_actors if a.get_actor_label()=='SM_Location2')
collision=next(a for a in all_actors if a.get_actor_label()=='COLLISION | Artist mouth')
mouth=lib.load_asset(folder+'/SM_MouthShell')
shell.static_mesh_component.set_static_mesh(mouth)
shell.static_mesh_component.set_editor_property('override_materials',[lib.load_asset('/Game/Art/Materials/Arena/MI_Arena')])
collision.static_mesh_component.set_static_mesh(mouth)
collision.static_mesh_component.set_visibility(False)
tongues=[a for a in all_actors if isinstance(a,u.MCTongue)]
if len(tongues)>1: raise RuntimeError('More than one tongue in map')
tongue=tongues[0] if tongues else actors.spawn_actor_from_class(u.MCTongue,shell.get_actor_location(),shell.get_actor_rotation())
if not tongues: tongue.set_actor_scale3d(shell.get_actor_scale3d())
tongue.set_actor_label('GAMEPLAY | Moving tongue'); tongue.set_folder_path('Gameplay/Tongue')
tongue.set_editor_property('source_mesh',lib.load_asset(folder+'/SM_TongueSurface'))
tongue.set_editor_property('surface_material',mi); tongue.set_editor_property('profile',profile)
tongue.call_method('RebuildSurface')
if not levels.save_current_level(): raise RuntimeError('Map save failed')
u.log('MC_TONGUE_INTEGRATED: derived shell + deforming tongue, one matching collision surface')
u.SystemLibrary.quit_editor()
