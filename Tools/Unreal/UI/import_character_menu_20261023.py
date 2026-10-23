"""Import the self-contained artist menu character without editing the map."""
import unreal as u,json
from pathlib import Path

ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Artifacts/CharacterMenu20261023';OUT.mkdir(parents=True,exist_ok=True)
DEST='/Game/UI/MenuShowcase/CharacterMenu20261023'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
flag='Interchange.FeatureFlags.Import.FBX';old=u.SystemLibrary.get_console_variable_int_value(flag)
try:
    u.SystemLibrary.execute_console_command(None,flag+' 0')
    task=u.AssetImportTask();task.filename=str(ROOT/'ArtSource/UI/CharacterMenu20261023/SK_CharacterMenu.fbx');task.destination_path=DEST;task.destination_name='SK_CharacterMenu';task.automated=True;task.save=False;task.replace_existing=True;task.replace_existing_settings=True;task.factory=u.FbxFactory()
    opt=u.FbxImportUI();opt.automated_import_should_detect_type=False;opt.mesh_type_to_import=u.FBXImportType.FBXIT_SKELETAL_MESH
    opt.import_mesh=True;opt.import_as_skeletal=True;opt.import_animations=True;opt.import_materials=False;opt.import_textures=False;opt.create_physics_asset=False
    for key,value in {'import_morph_targets':True,'update_skeleton_reference_pose':False,'use_t0_as_ref_pose':False,'normal_import_method':u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS}.items():opt.skeletal_mesh_import_data.set_editor_property(key,value)
    for key,value in {'animation_length':u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME,'use_default_sample_rate':False,'custom_sample_rate':24,'import_bone_tracks':True,'remove_redundant_keys':False}.items():opt.anim_sequence_import_data.set_editor_property(key,value)
    task.options=opt;u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
finally:u.SystemLibrary.execute_console_command(None,flag+' '+str(old))
objects=[u.load_asset(p) for p in u.EditorAssetLibrary.list_assets(DEST)]
mesh=next(o for o in objects if isinstance(o,u.SkeletalMesh));clip=next(o for o in objects if isinstance(o,u.AnimSequence))
if clip.get_name()!='A_CharacterMenu_Cleaning_Loop':
    assert u.EditorAssetLibrary.rename_asset(clip.get_path_name(),DEST+'/A_CharacterMenu_Cleaning_Loop')
    clip=u.load_asset(DEST+'/A_CharacterMenu_Cleaning_Loop')
materials=['/Game/UI/MenuShowcase/AnatomyV2/Materials/MI_MenuHeroBody','/Game/UI/MenuShowcase/AnatomyV2/Materials/MI_MenuHeroBag','/Game/Art/Materials/Equipment/MI_MegaBrush']
slots=list(mesh.materials);assert len(slots)==3
for slot,path in zip(slots,materials):
    mat=u.load_asset(path);assert mat;slot.material_interface=mat
mesh.materials=slots
clip.set_editor_property('enable_root_motion',False)
assert abs(clip.get_play_length()-9.125)<.002,clip.get_play_length()
for o in [mesh,mesh.skeleton,clip]:assert u.EditorAssetLibrary.save_loaded_asset(o)
report={'mesh':mesh.get_path_name(),'skeleton':mesh.skeleton.get_path_name(),'clip':clip.get_path_name(),'duration':clip.get_play_length(),'bounds':str(mesh.get_bounds()),'materials':materials,'morphs':[m.get_name() for m in mesh.get_editor_property('morph_targets')],'curves':[str(n) for n in u.AnimationLibrary.get_animation_curve_names(clip,u.RawCurveTrackTypes.RCT_FLOAT)],'imported_paths':list(task.imported_object_paths)}
(OUT/'Import.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('CHARACTER_MENU_IMPORTED',json.dumps(report))
