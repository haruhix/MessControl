"""Read-only editor inventory for the approved October mechanics batch."""
import json
from pathlib import Path
import unreal as u

editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
world = editor.get_editor_world()
bp = u.load_asset('/Game/Blueprints/BP_PlayerCharacter')
hero = u.get_default_object(bp.generated_class()) if bp else None
mesh = hero.get_component_by_class(u.SkeletalMeshComponent) if hero else None
asset = mesh.get_editor_property('skeletal_mesh_asset') if mesh else None
appearance = u.load_asset('/Game/Data/DA_PlayerAppearance')
if appearance:
    asset = appearance.get_editor_property('skeletal_mesh') or asset
report = dict(map=world.get_path_name(), player_mesh=asset.get_path_name() if asset else None,
              tutorial_map_exists=u.EditorAssetLibrary.does_asset_exist('/Game/Maps/L_Tutorial'),
              dirty_content=[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
              dirty_maps=[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
if asset:
    try:
        report['morphs'] = [m.get_name() for m in asset.get_editor_property('morph_targets')]
    except Exception as error:
        report['morph_query_limitation'] = str(error)
report['map_assets'] = [p for p in u.EditorAssetLibrary.list_assets('/Game/Maps', recursive=False)
                       if not p.endswith('_BuiltData')]
out = Path(u.Paths.project_saved_dir()) / 'Checks' / 'ApprovedMechanicsInspection.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_APPROVED_INSPECTION ' + json.dumps(report, ensure_ascii=False))
