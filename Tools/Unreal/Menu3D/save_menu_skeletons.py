"""Persist required task skeletons; restore the shared exported rig if missing."""
import json
from pathlib import Path
import unreal as u

root=Path(u.Paths.project_dir()).resolve()
assert str(root).replace('\\','/').lower()=='e:/devgame/messcontrol'
report=[]
for name in ('SK_DAY01_Blocking','SK_DAY02_Blocking','SK_DAY03_Seated_v04'):
    mesh=u.load_asset('/Game/UI/MainMenu3D/Meshes/'+name)
    assert mesh
    morphs_before=sorted(str(x) for x in mesh.get_all_morph_target_names())
    skeleton=mesh.get_editor_property('skeleton')
    restored=skeleton is None
    if restored:
        # All three native FBXs use the same MC_MenuMouth_Rig/root hierarchy.
        # Retain a separate task package rather than altering the shared source.
        template=u.load_asset('/Game/UI/MainMenu3D/Meshes/SK_DAY01_Blocking_Skeleton')
        assert template
        skeleton=u.EditorAssetLibrary.duplicate_loaded_asset(template,'/Game/UI/MainMenu3D/Meshes/'+name+'_Skeleton')
        assert skeleton
        mesh.modify()
        mesh.call_method('SetSkeleton',args=(skeleton,))
    assert skeleton and skeleton.get_path_name().startswith('/Game/UI/MainMenu3D/Meshes/'+name+'_Skeleton.')
    assert u.EditorAssetLibrary.save_loaded_asset(skeleton,False),skeleton.get_path_name()
    if restored:
        assert mesh.get_editor_property('skeleton')==skeleton
        assert sorted(str(x) for x in mesh.get_all_morph_target_names())==morphs_before
        assert u.EditorAssetLibrary.save_loaded_asset(mesh,False)
    file=root/'Content/UI/MainMenu3D/Meshes'/(name+'_Skeleton.uasset')
    assert file.is_file(),file
    report.append({'mesh':mesh.get_path_name(),'saved_skeleton':skeleton.get_path_name(),'disk_bytes':file.stat().st_size,
        'restored_from_identical_export_rig_template':restored,'morph_names_preserved':morphs_before})
(root/'Tools/Unreal/Menu3D/Evidence/saved_skeleton_packages.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_MENU_SKELETON_PACKAGES_SAVED '+json.dumps(report))
