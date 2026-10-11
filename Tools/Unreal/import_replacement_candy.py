"""Replace winter boss geometry; assign the user's existing material only."""
import unreal as u,json,hashlib
from pathlib import Path
root=Path(u.Paths.project_dir());source=root/'ArtSource/WinterCandy/Replacement'
report=json.loads((source/'ExportReport.json').read_text())
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
lib=u.EditorAssetLibrary
material=u.load_asset('/Game/Art/Materials/Bosses/MI_Candy');assert isinstance(material,u.MaterialInstanceConstant)
material_parameters={name:str(material.get_editor_property(name)) for name in ['scalar_parameter_values','vector_parameter_values','texture_parameter_values']}
registry=u.AssetRegistryHelpers.get_asset_registry()
def material_assets():
    return {str(asset.package_name)+'.'+str(asset.asset_name)
      for asset in registry.get_assets_by_path('/Game',recursive=True)
      if str(asset.asset_class_path.asset_name) in ['Material','MaterialInstanceConstant']}
before_materials=material_assets()
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
skeleton=u.load_asset('/Game/Gameplay/Cold/Candy/Animations/SK_WinterCandy_Skeleton');assert skeleton

def import_mesh(name,skeletal=False):
    task=u.AssetImportTask();task.filename=str(source/(name+'.fbx'))
    task.destination_path='/Game/Gameplay/Cold/Candy'+('/Animations' if skeletal else '')
    task.destination_name=name;task.automated=True;task.replace_existing=True;task.replace_existing_settings=True
    task.save=False;task.factory=u.FbxFactory()
    options=u.FbxImportUI();options.automated_import_should_detect_type=False
    options.mesh_type_to_import=u.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else u.FBXImportType.FBXIT_STATIC_MESH
    options.import_mesh=True;options.import_as_skeletal=skeletal;options.import_animations=False
    options.import_materials=False;options.import_textures=False;options.create_physics_asset=False
    if skeletal:options.skeleton=skeleton
    data=options.skeletal_mesh_import_data if skeletal else options.static_mesh_import_data
    data.set_editor_properties({'convert_scene_unit':True,'import_uniform_scale':1.,
        'normal_import_method':u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
        'vertex_color_import_option':u.VertexColorImportOption.REPLACE})
    if skeletal:data.set_editor_properties({'import_morph_targets':True,'update_skeleton_reference_pose':False,'use_t0_as_ref_pose':False})
    else:data.set_editor_properties({'combine_meshes':True,'auto_generate_collision':True})
    task.options=options;u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    asset=lib.load_asset(task.destination_path+'/'+name)
    assert isinstance(asset,u.SkeletalMesh if skeletal else u.StaticMesh),(name,task.imported_object_paths)
    return asset

body=import_mesh('SM_WinterCandy')
for index in range(max(1,len(body.get_editor_property('static_materials')))):body.set_material(index,material)
static_editor=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
static_editor.remove_collisions(body)
assert static_editor.set_convex_decomposition_collisions(body,1,24,100000)
body.set_editor_property('allow_cpu_access',True)
assert lib.save_loaded_asset(body,only_if_is_dirty=False)
mesh=import_mesh('SK_WinterCandy',True)
slots=list(mesh.get_editor_property('materials'));assert slots
for slot in slots:slot.set_editor_property('material_interface',material)
mesh.set_editor_property('materials',slots)
assert mesh.get_editor_property('skeleton')==skeleton
morphs=[m.get_name() for m in mesh.get_editor_property('morph_targets')]
assert 'scream' in morphs
base=material.get_base_material()
u.MaterialEditingLibrary.set_base_material_usage(base,u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
u.MaterialEditingLibrary.set_base_material_usage(base,u.MaterialUsage.MATUSAGE_MORPH_TARGETS)
assert not list(u.MaterialEditingLibrary.recompile_material(base))
assert lib.save_loaded_asset(base,only_if_is_dirty=False)
assert material_parameters=={name:str(material.get_editor_property(name)) for name in material_parameters}

# The source replacement also contains the authored floor-hit shape key.
old_report=json.loads((root/'ArtSource/WinterCandy/Animations/ExportReport.json').read_text())
clips=[]
for record in old_report['clips']:
    clip=lib.load_asset('/Game/Gameplay/Cold/Candy/Animations/'+Path(record['file']).stem)
    assert clip.get_editor_property('skeleton')==skeleton
    pose=u.AnimPoseExtensions.get_anim_pose_at_time(clip,0,u.AnimPoseEvaluationOptions())
    assert [str(n) for n in u.AnimPoseExtensions.get_bone_names(pose)]==['root']
    if 'floor_hit' in morphs:
        names={str(n) for n in u.AnimationLibrary.get_animation_curve_names(clip,u.RawCurveTrackTypes.RCT_FLOAT)}
        if 'floor_hit' in names:u.AnimationLibrary.remove_curve(clip,'floor_hit',False)
        u.AnimationLibrary.add_curve(clip,'floor_hit',u.RawCurveTrackTypes.RCT_FLOAT,False)
        values=record['morph_samples']['floor_hit']
        u.AnimationLibrary.add_float_curve_keys(clip,'floor_hit',[i/old_report['fps'] for i in range(len(values))],values)
    clip.set_preview_skeletal_mesh(mesh)
    assert lib.save_loaded_asset(clip,only_if_is_dirty=False)
    clips.append({'name':clip.get_name(),'duration':clip.get_play_length()})
for asset in [mesh,skeleton,material]:assert lib.save_loaded_asset(asset,only_if_is_dirty=False)
assert material_assets()==before_materials,'No new Unreal materials may be created.'
assert hashlib.sha256(Path(report['source']).read_bytes()).hexdigest()==report['sha256']
assert max(abs(c) for c in mesh.get_bounds().origin.to_tuple())<.001
assert max(abs(c) for c in body.get_bounds().origin.to_tuple())<.001
# This editor previously had its old collision mesh deleted; refresh the CDO
# immediately. On the next editor load, the native constructor resolves this path.
cdo=u.get_default_object(u.MCIceEvent)
cdo.get_editor_property('body').set_static_mesh(body)
cdo.get_editor_property('candy_visual').set_skeletal_mesh_asset(mesh)
result={'saved':True,'source':report['source'],'source_sha256':report['sha256'],'source_preserved':True,
 'static_mesh':body.get_path_name(),'skeletal_mesh':mesh.get_path_name(),'material':material.get_path_name(),
 'new_materials_created':0,'material_parameters_preserved':True,'bounds':str(mesh.get_bounds()),'morphs':morphs,'clips':clips,
 'dirty_content':[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
 'dirty_maps':[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}
(root/'Artifacts/WinterCandy/ReplacementImport.json').write_text(json.dumps(result,indent=2),encoding='utf8')
lib.sync_browser_to_objects([mesh.get_path_name()])
print('WINTER_CANDY_REPLACED',json.dumps(result))
