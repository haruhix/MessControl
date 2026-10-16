"""Import the symmetric DAY03 candidate into the isolated native preview map."""
import hashlib
import json
from pathlib import Path
import unreal as u

root=Path(u.Paths.project_dir()).resolve()
request=globals().get('MC_DAY03_IMPORT_REQUEST') or {
    'revision':'v06_SymmetricRemodel','mesh_name':'SK_DAY03_Symmetric_v06',
    'fbx_sha256':'9ce7ff7637b33e36ab4a2b3cdaa048dac40cf44a573406c3da93a53a6ada32c6',
    'report_name':'symmetric_v06_import_report.json'}
assert Path(request['revision']).name==request['revision']
assert request['mesh_name'].startswith('SK_DAY03_')
assert Path(request['report_name']).name==request['report_name']
handoff=root/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff'/request['revision']
if request.get('candidate'):
    candidate=(handoff/request['candidate']).resolve()
    assert candidate.is_relative_to(handoff.resolve()), candidate
    handoff=candidate
fbx=handoff/'SK_DAY03.fbx'
assert hashlib.sha256(fbx.read_bytes()).hexdigest()==request['fbx_sha256']
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
base='/Game/UI/MainMenu3D'
assert world and world.get_path_name().startswith(base+'/L_MainMenu3D')
name=request['mesh_name'];path=base+'/Meshes/'+name
skeleton=u.load_asset(base+'/Meshes/SK_DAY03_Seated_v04_Skeleton');assert skeleton
options=u.FbxImportUI()
for field,value in [('automated_import_should_detect_type',False),('import_mesh',True),
    ('import_as_skeletal',True),('import_materials',False),('import_textures',False),
    ('import_animations',False),('create_physics_asset',False),('skeleton',skeleton),
    ('mesh_type_to_import',u.FBXImportType.FBXIT_SKELETAL_MESH),
    ('original_import_type',u.FBXImportType.FBXIT_SKELETAL_MESH)]:
    options.set_editor_property(field,value)
data=options.get_editor_property('skeletal_mesh_import_data')
for field,value in [('convert_scene',False),('convert_scene_unit',False),('force_front_x_axis',False),
    ('import_morph_targets',True),('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)]:
    data.set_editor_property(field,value)
task=u.AssetImportTask()
for field,value in [('filename',str(fbx)),('destination_path',base+'/Meshes'),
    ('destination_name',name),('automated',True),('save',False),('replace_existing',False),
    ('options',options),('factory',u.FbxFactory())]:
    task.set_editor_property(field,value)
assert not u.EditorAssetLibrary.does_asset_exist(path),'Inspect an existing candidate before reauthoring'
u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
mesh=u.load_asset(path);assert mesh
morphs=sorted(str(x) for x in mesh.get_all_morph_target_names())
assert morphs==['Pose_000','Pose_025','Pose_050','Pose_075'],morphs
assert mesh.get_editor_property('skeleton')==skeleton
actors=u.GameplayStatics.get_all_actors_with_tag(world,'Menu3D_DAY03');assert len(actors)==1
actor=actors[0];comp=actor.get_component_by_class(u.SkeletalMeshComponent)
before=comp.get_editor_property('skeletal_mesh_asset').get_path_name()
materials=[comp.get_material(i) for i in range(comp.get_num_materials())]
assert len(materials)==2
comp.set_skeletal_mesh_asset(mesh)
for i,material in enumerate(materials):comp.set_material(i,material)
u.EditorAssetLibrary.save_loaded_asset(mesh)
u.EditorAssetLibrary.save_loaded_asset(skeleton)
assert levels.save_current_level()
report={'status':request.get('status','SYMMETRIC_CANDIDATE_IMPORTED_PENDING_NATIVE_LOWER_ROW_REVIEW'),'source_fbx':str(fbx),
    'source_sha256':hashlib.sha256(fbx.read_bytes()).hexdigest(),'previous_mesh':before,
    'mesh':mesh.get_path_name(),'morphs':morphs,'skeleton':skeleton.get_path_name(),
    'bounds':str(mesh.get_bounds()),'blocking_overrides':[m.get_path_name() for m in materials],
    'saved_map':world.get_path_name(),'startup_map_changed':False,
    'validation':request.get('validation','Source/fresh FBX independently checked. Import and package save; solo PIE and lower panel clipping remain pending.')}
(root/'Tools/Unreal/Menu3D/Evidence'/request['report_name']).write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_SYMMETRIC_IMPORT '+json.dumps(report))
