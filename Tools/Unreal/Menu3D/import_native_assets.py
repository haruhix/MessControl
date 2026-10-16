"""Import task-owned real mouth meshes and registered radiance materials.

Uses legacy FBX explicitly so one root bone + four morph targets are imported.
No existing game mesh/material/animation is edited; no level is saved here.
"""
import json
import hashlib
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir())/'ArtSource/UI/MainMenu3D/20261015_refB_v1'
HANDOFF = ROOT/'Assembly/UnrealHandoff/v04_SeatedSymmetricNative'
BASE = '/Game/UI/MainMenu3D'
assert (HANDOFF/'unreal_mouth_handoff.json').exists()
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
assets = u.AssetToolsHelpers.get_asset_tools()
report = {'imported':[], 'materials':[], 'policy':'Real geometry; baked radiance is explicitly not albedo. Native character unchanged.'}

def import_mesh(filename, skeletal=False):
    name = Path(filename).stem + '_Seated_v04'
    dest = BASE+'/Meshes/'+name
    digest = hashlib.sha256((HANDOFF/filename).read_bytes()).hexdigest()
    if u.EditorAssetLibrary.does_asset_exist(dest):
        ob = u.load_asset(dest)
        if u.EditorAssetLibrary.get_metadata_tag(ob,'Menu3DSourceSHA') == digest and (not skeletal or len(ob.get_all_morph_target_names()) == 4):
            return ob
    options = u.FbxImportUI()
    options.set_editor_property('automated_import_should_detect_type',False)
    options.set_editor_property('import_mesh',True)
    options.set_editor_property('import_as_skeletal',skeletal)
    options.set_editor_property('mesh_type_to_import',u.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else u.FBXImportType.FBXIT_STATIC_MESH)
    options.set_editor_property('original_import_type',u.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else u.FBXImportType.FBXIT_STATIC_MESH)
    options.set_editor_property('import_materials',False)
    options.set_editor_property('import_textures',False)
    options.set_editor_property('import_animations',False)
    data = options.get_editor_property('skeletal_mesh_import_data' if skeletal else 'static_mesh_import_data')
    data.set_editor_property('convert_scene',False)
    data.set_editor_property('convert_scene_unit',False)
    data.set_editor_property('force_front_x_axis',False)
    data.set_editor_property('import_uniform_scale',1.)
    data.set_editor_property('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    if skeletal:
        data.set_editor_property('import_morph_targets',True)
        data.set_editor_property('update_skeleton_reference_pose',False)
        options.set_editor_property('create_physics_asset',False)
    else:
        data.set_editor_property('combine_meshes',True)
        data.set_editor_property('auto_generate_collision',False)
        data.set_editor_property('generate_lightmap_u_vs',False)
    task = u.AssetImportTask()
    task.set_editor_property('filename',str(HANDOFF/filename))
    task.set_editor_property('destination_path',BASE+'/Meshes')
    task.set_editor_property('destination_name',name)
    task.set_editor_property('automated',True)
    task.set_editor_property('save',True)
    task.set_editor_property('replace_existing',True)
    task.set_editor_property('replace_existing_settings',True)
    task.set_editor_property('options',options)
    task.set_editor_property('factory',u.FbxFactory())
    assets.import_asset_tasks([task])
    ob = u.load_asset(dest)
    assert ob, list(task.get_editor_property('imported_object_paths'))
    u.EditorAssetLibrary.set_metadata_tag(ob,'Menu3DSourceSHA',digest)
    u.EditorAssetLibrary.save_loaded_asset(ob)
    report['imported'].append(ob.get_path_name())
    return ob

mouth = import_mesh('SK_DAY03.fbx',True)
report['morphs'] = [str(n) for n in mouth.get_all_morph_target_names()]
assert all(any(x.endswith(name) for x in report['morphs']) for name in ('Pose_000','Pose_025','Pose_050','Pose_075')), report['morphs']
report['bounds'] = str(mouth.get_bounds())
inside = import_mesh('SM_DAY03_Interior.fbx')
tongue = import_mesh('SM_DAY03_Tongue.fbx')

open_binding_path = HANDOFF/'Textures_sRGB8/open_binding.json'
closed_binding_path = HANDOFF/'Textures_sRGB8/closed_binding.json'
open_binding = json.loads(open_binding_path.read_text(encoding='utf8'))
closed_binding = json.loads(closed_binding_path.read_text(encoding='utf8'))

def texture(path, name):
    dest = BASE+'/Textures/'+name
    digest = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    existing = u.load_asset(dest) if u.EditorAssetLibrary.does_asset_exist(dest) else None
    if not existing or u.EditorAssetLibrary.get_metadata_tag(existing,'Menu3DSourceSHA') != digest:
        task = u.AssetImportTask()
        task.set_editor_property('filename',str(path))
        task.set_editor_property('destination_path',BASE+'/Textures')
        task.set_editor_property('destination_name',name)
        task.set_editor_property('automated',True)
        task.set_editor_property('save',True)
        task.set_editor_property('replace_existing',True)
        task.set_editor_property('replace_existing_settings',True)
        assets.import_asset_tasks([task])
    tex = u.load_asset(dest)
    assert tex
    tex.set_editor_property('srgb',True)
    tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property('never_stream',True)
    u.EditorAssetLibrary.set_metadata_tag(tex,'Menu3DSourceSHA',digest)
    u.EditorAssetLibrary.save_loaded_asset(tex)
    return tex

for i, material_name in enumerate(('MC_Enamel','MC_Gum','MC_Interior','MC_Tongue')):
    path = BASE+'/Materials/M_'+material_name+'_Radiance'
    material = u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset('M_'+material_name+'_Radiance',BASE+'/Materials',u.Material,u.MaterialFactoryNew())
    u.MaterialEditingLibrary.delete_all_material_expressions(material)
    material.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided',True)
    if i < 2:
        material.set_editor_property('used_with_skeletal_mesh',True)
        material.set_editor_property('used_with_morph_targets',True)
    tex_open = texture(open_binding['materials'][material_name]['radiance_not_albedo'],'T_'+material_name+'_OpenRadiance')
    # Interior and tongue are opening-only; no unobserved Closed radiance is invented.
    closed_state = closed_binding['materials'].get(material_name,open_binding['materials'][material_name])
    tex_closed = texture(closed_state['radiance_not_albedo'],'T_'+material_name+'_ClosedRadiance')
    opened = u.MaterialEditingLibrary.create_material_expression(material,u.MaterialExpressionTextureSampleParameter2D,-600,0)
    opened.set_editor_property('parameter_name','OpenRadiance')
    opened.set_editor_property('texture',tex_open)
    closed = u.MaterialEditingLibrary.create_material_expression(material,u.MaterialExpressionTextureSampleParameter2D,-600,200)
    closed.set_editor_property('parameter_name','ClosedRadiance')
    closed.set_editor_property('texture',tex_closed)
    amount = u.MaterialEditingLibrary.create_material_expression(material,u.MaterialExpressionScalarParameter,-600,400)
    amount.set_editor_property('parameter_name','OpenAmount')
    amount.set_editor_property('default_value',1.)
    mix = u.MaterialEditingLibrary.create_material_expression(material,u.MaterialExpressionLinearInterpolate,-200,0)
    u.MaterialEditingLibrary.connect_material_expressions(closed,'RGB',mix,'A')
    u.MaterialEditingLibrary.connect_material_expressions(opened,'RGB',mix,'B')
    u.MaterialEditingLibrary.connect_material_expressions(amount,'',mix,'Alpha')
    assert u.MaterialEditingLibrary.connect_material_property(mix,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    u.MaterialEditingLibrary.recompile_material(material)
    u.EditorAssetLibrary.save_loaded_asset(material)
    report['materials'].append(material.get_path_name())
    if i < 2:
        # Material order is explicit in the FBX handoff.
        slots = list(mouth.get_editor_property('materials'))
        assert len(slots) == 2
        slots[i].set_editor_property('material_interface',material)
        mouth.set_editor_property('materials',slots)
    else:
        (inside if i == 2 else tongue).set_material(0,material)
for ob in (mouth,inside,tongue):
    u.EditorAssetLibrary.save_loaded_asset(ob)
(HANDOFF/'native_asset_import_report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_MENU3D_NATIVE_IMPORT '+json.dumps(report))
