"""Import the saved candy's baked bone motion and morph curves, retaining ice colours."""
import unreal as u,json
from pathlib import Path
root=Path(u.Paths.project_dir());source=root/'ArtSource/WinterCandy/Animations'
folder='/Game/Gameplay/Cold/Candy/Animations'
lib,e=u.EditorAssetLibrary,u.MaterialEditingLibrary
report=json.loads((source/'ExportReport.json').read_text(encoding='utf8'))
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
def run(filename,name,mesh=False,skeleton=None):
    task=u.AssetImportTask();task.filename=str(source/filename)
    task.destination_path=folder;task.destination_name=name;task.automated=True
    task.replace_existing=True;task.replace_existing_settings=True;task.save=False;task.factory=u.FbxFactory()
    opt=u.FbxImportUI();opt.automated_import_should_detect_type=False
    opt.mesh_type_to_import=u.FBXImportType.FBXIT_SKELETAL_MESH if mesh else u.FBXImportType.FBXIT_ANIMATION
    opt.import_mesh=mesh;opt.import_as_skeletal=True;opt.import_animations=not mesh
    opt.import_materials=False;opt.import_textures=False;opt.create_physics_asset=False
    if skeleton:opt.skeleton=skeleton
    data=opt.skeletal_mesh_import_data if mesh else opt.anim_sequence_import_data
    data.set_editor_properties({'convert_scene_unit':True,'import_uniform_scale':1.})
    if mesh:data.set_editor_properties({'import_morph_targets':True,'update_skeleton_reference_pose':False,
        'use_t0_as_ref_pose':False,'normal_import_method':u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
        'vertex_color_import_option':u.VertexColorImportOption.REPLACE})
    else:data.set_editor_properties({'animation_length':u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME,
        'use_default_sample_rate':False,'custom_sample_rate':24,'import_bone_tracks':True,
        'preserve_local_transform':True,'import_custom_attribute':True})
    task.options=opt;u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    cls=u.SkeletalMesh if mesh else u.AnimSequence
    asset=next((u.load_asset(p) for p in task.imported_object_paths if isinstance(u.load_asset(p),cls)),None)
    assert asset,(name,task.imported_object_paths)
    return asset
mesh=run('SK_WinterCandy.fbx','SK_WinterCandy',True)
skeleton=mesh.get_editor_property('skeleton')
morphs=[m.get_name() for m in mesh.get_editor_property('morph_targets')]
assert 'scream' in morphs,morphs
slots=list(mesh.get_editor_property('materials'))
for slot in slots:
    role='Face' if 'Face' in str(slot.get_editor_property('imported_material_slot_name')) else 'Body'
    mat=u.load_asset('/Game/Gameplay/Cold/Candy/M_WinterCandyIce'+role)
    e.set_base_material_usage(mat,u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    e.set_base_material_usage(mat,u.MaterialUsage.MATUSAGE_MORPH_TARGETS)
    assert not list(e.recompile_material(mat))
    assert lib.save_loaded_asset(mat,only_if_is_dirty=False)
    slot.set_editor_property('material_interface',mat)
mesh.set_editor_property('materials',slots)
clips=[]
pose_options=u.AnimPoseEvaluationOptions()
for record in report['clips']:
    name=Path(record['file']).stem
    clip=run(record['file'],name,skeleton=skeleton)
    assert clip.get_editor_property('skeleton')==skeleton
    assert abs(clip.get_play_length()-record['duration'])<.045,(name,clip.get_play_length(),record['duration'])
    clip.set_editor_property('enable_root_motion',False);clip.set_preview_skeletal_mesh(mesh)
    names={str(n) for n in u.AnimationLibrary.get_animation_curve_names(clip,u.RawCurveTrackTypes.RCT_FLOAT)}
    for morph in morphs:
        if morph in names:u.AnimationLibrary.remove_curve(clip,morph,False)
        u.AnimationLibrary.add_curve(clip,morph,u.RawCurveTrackTypes.RCT_FLOAT,False)
        values=record['morph_samples'][morph]
        u.AnimationLibrary.add_float_curve_keys(clip,morph,[i/report['fps'] for i in range(len(values))],values)
    if record['role']=='Scream':
        times,values=u.AnimationLibrary.get_float_keys(clip,'scream')
        assert max(values)>.6,'Scream must carry its paired morph curve.'
        pose=u.AnimPoseExtensions.get_anim_pose_at_time(clip,.5,pose_options)
        assert u.AnimPoseExtensions.get_curve_weight(pose,'scream')>.6
    if record['role'] in ('Idle','FloorHit'):
        before=u.AnimPoseExtensions.get_anim_pose_at_time(clip,0,pose_options)
        peak=u.AnimPoseExtensions.get_anim_pose_at_time(clip,1 if record['role']=='Idle' else .5,pose_options)
        before_z=u.AnimPoseExtensions.get_bone_pose(before,'root').translation.z
        peak_z=u.AnimPoseExtensions.get_bone_pose(peak,'root').translation.z
        assert peak_z-before_z>(34 if record['role']=='Idle' else 200),'Root motion must use centimetres, matching the artist mesh.'
    assert lib.save_loaded_asset(clip,only_if_is_dirty=False)
    clips.append({'role':record['role'],'asset':clip.get_path_name(),'duration':clip.get_play_length()})
for asset in (mesh,skeleton):assert lib.save_loaded_asset(asset,only_if_is_dirty=False)
result={'mesh':mesh.get_path_name(),'skeleton':skeleton.get_path_name(),'morphs':morphs,'bounds':str(mesh.get_bounds()),'clips':clips}
(root/'Artifacts/WinterCandy/AnimationImport.json').write_text(json.dumps(result,indent=2),encoding='utf8')
print('CANDY_ANIMATIONS_IMPORTED',json.dumps(result))
