"""Own contact-fitted copy of the existing in-game circular MegaBrush."""
from pathlib import Path
import unreal as u
ROOT = Path('E:/DEVGAME/MessControl')
BASE = '/Game/UI/MenuShowcase/AnatomyV2'
es = u.get_editor_subsystem(u.EditorActorSubsystem)
ls = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not ls.is_in_play_in_editor()
assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().startswith(BASE + '/L_MenuAnatomyV2.')
lib = u.EditorAssetLibrary
path = BASE + '/VFX/SM_MenuMegaBrush'
mesh = lib.load_asset(path)
if not mesh:
    flag = 'Interchange.FeatureFlags.Import.FBX'
    old = u.SystemLibrary.get_console_variable_int_value(flag)
    try:
        u.SystemLibrary.execute_console_command(None, flag + ' 0')
        task = u.AssetImportTask()
        task.filename = str(ROOT / 'ArtSource/UI/MenuSceneV2_20261022/BrushReview/SM_MenuMegaBrush_ContactFitV1.fbx')
        task.destination_path = BASE + '/VFX'
        task.destination_name = 'SM_MenuMegaBrush'
        task.automated = True
        task.save = False
        task.factory = u.FbxFactory()
        opt = u.FbxImportUI()
        opt.automated_import_should_detect_type = False
        opt.mesh_type_to_import = u.FBXImportType.FBXIT_STATIC_MESH
        opt.import_mesh = True
        opt.import_materials = False
        opt.import_textures = False
        opt.static_mesh_import_data.combine_meshes = True
        opt.static_mesh_import_data.auto_generate_collision = False
        task.options = opt
        u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    finally:
        u.SystemLibrary.execute_console_command(None, flag + ' ' + str(old))
    mesh = lib.load_asset(path)
assert isinstance(mesh, u.StaticMesh)
extent = mesh.get_bounds().box_extent
assert abs(extent.x - 53.15085) < .02 and abs(extent.y - 13.7152) < .02 and abs(extent.z - 10.09785) < .02, extent
material = lib.load_asset('/Game/Art/Materials/Equipment/MI_MegaBrush')
assert material
mesh.set_material(0, material)
assert lib.save_loaded_asset(mesh)
brush = next(a for a in es.get_all_level_actors() if 'MC_MenuV2_Brush' in [str(t) for t in a.tags])
brush.static_mesh_component.set_static_mesh(mesh)
brush.static_mesh_component.set_material(0, material)
assert ls.save_current_level()
u.log('MENU_CIRCULAR_BRUSH_CANDIDATE_SAVED')
