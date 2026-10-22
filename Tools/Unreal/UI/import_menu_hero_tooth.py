"""Import the exact evaluated DCC crown into the menu-only namespace."""
import unreal as u
from pathlib import Path
import json,hashlib,shutil
ROOT=Path('E:/DEVGAME/MessControl')
OUT=Path(globals().get('MC_MENU_TOOTH_OUT','E:/CodexWork/MessControlMenuShowcase_20261021/HeroToothImport01'))
assert OUT.drive.upper()=='E:' and not OUT.exists();OUT.mkdir(parents=True)
data=json.loads((ROOT/'ArtSource/UI/MenuShowcase_20261021/Blender/UnrealHandoff/HeroTooth_v06.json').read_text(encoding='utf8'))
source=Path(data['fbx'])
assert hashlib.sha256(source.read_bytes()).hexdigest()=='197e885730e82cf8195c1da8da52848dff866bb813ce5b7dba1726bf522f642f'
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not ls.is_in_play_in_editor()
assert not list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
actors=list(u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors())
assert all('MC_MenuShowcase_OwnedV1' in [str(t) for t in a.tags] for a in actors)
actor=next(a for a in actors if a.get_actor_label()=='MC_MenuShowcase_ExistingArenaTooth_Hero')
dest='/Game/UI/MenuShowcase/Posed/A/SM_Menu05_HeroTooth_v06'
assert not u.EditorAssetLibrary.does_asset_exist(dest)
mapfile=ROOT/'Content/Maps/L_MainMenu.umap';backup=OUT/'L_MainMenu_Before.umap'
shutil.copy2(mapfile,backup);assert hashlib.sha256(mapfile.read_bytes()).digest()==hashlib.sha256(backup.read_bytes()).digest()
task=u.AssetImportTask()
task.set_editor_properties({'filename':str(source),'destination_path':'/Game/UI/MenuShowcase/Posed/A','destination_name':'SM_Menu05_HeroTooth_v06','automated':True,'replace_existing':False,'save':False,'factory':u.FbxFactory()})
options=u.FbxImportUI()
options.set_editor_properties({'automated_import_should_detect_type':False,'mesh_type_to_import':u.FBXImportType.FBXIT_STATIC_MESH,'import_mesh':True,'import_as_skeletal':False,'import_materials':False,'import_textures':False,'import_animations':False})
options.static_mesh_import_data.set_editor_properties({'combine_meshes':False,'auto_generate_collision':False,'convert_scene':True,'convert_scene_unit':True,'force_front_x_axis':False,'normal_import_method':u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,'import_uniform_scale':1.})
task.options=options
flag='Interchange.FeatureFlags.Import.FBX';before=u.SystemLibrary.get_console_variable_int_value(flag)
try:
    u.SystemLibrary.execute_console_command(None,flag+' 0')
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
finally:
    u.SystemLibrary.execute_console_command(None,flag+' '+str(before))
    assert u.SystemLibrary.get_console_variable_int_value(flag)==before
paths=list(task.imported_object_paths)
assert len(paths)==1 and str(paths[0]).startswith('/Game/UI/MenuShowcase/Posed/A/')
mesh=u.load_asset(paths[0]);assert isinstance(mesh,u.StaticMesh)
b=mesh.get_bounds();actual=[[b.origin.x-b.box_extent.x,b.origin.y-b.box_extent.y,b.origin.z-b.box_extent.z],[b.origin.x+b.box_extent.x,b.origin.y+b.box_extent.y,b.origin.z+b.box_extent.z]]
low,high=data['bounds_cm'];expected=[[low[0],-high[1],low[2]],[high[0],-low[1],high[2]]]
error=max(abs(actual[i][j]-expected[i][j]) for i in range(2) for j in range(3))
assert error<.01,str(actual)
u.EditorAssetLibrary.set_metadata_tag(mesh,'MC_MenuShowcaseStage','menu_showcase_static_v1')
u.EditorAssetLibrary.set_metadata_tag(mesh,'MC_MenuShowcaseSourceSHA256',data['fbx_sha256'])
material=u.load_asset('/Game/UI/MenuShowcase/Materials/A/M_MenuEnamelCoffee');assert material
mesh.set_material(0,material)
assert u.EditorAssetLibrary.save_loaded_asset(mesh,only_if_is_dirty=False)
actor.static_mesh_component.set_static_mesh(mesh)
actor.static_mesh_component.set_material(0,material)
actor.set_actor_location(u.Vector(0,0,0),False,True)
actor.set_actor_rotation(u.Rotator(pitch=0,yaw=0,roll=0),False)
actor.set_actor_scale3d(u.Vector(1,1,1))
assert ls.save_current_level()
RESULT={'imported':mesh.get_path_name(),'bounds_error_cm':error,'evaluated_triangles':data['triangles'],'map_saved':True,'source':str(source),'gameplay_assets_changed':False,'visual_review_pending':True}
(OUT/'Report.json').write_text(json.dumps(RESULT,indent=2),encoding='utf8')
