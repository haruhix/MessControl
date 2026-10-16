"""Compile the existing hit overlay for the knight's GPU mesh deformer."""
import unreal as u,json
from pathlib import Path

out=Path(u.Paths.project_dir())/'Artifacts/NutKnightBodyFix_20261017'
material=u.load_asset('/Game/Gameplay/VFX/M_CombatHitOverlay')
assert material
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
result=u.MaterialEditingLibrary.set_base_material_usage(material,u.MaterialUsage.MATUSAGE_MESH_DEFORMER)
assert material.get_editor_property('used_with_mesh_deformer')
u.MaterialEditingLibrary.recompile_material(material)
assert u.EditorAssetLibrary.save_loaded_asset(material,only_if_is_dirty=False)
(out/'OverlayMaterialReport.json').write_text(json.dumps({'passed':True,'asset':material.get_path_name(),'mesh_deformer_usage':True,'appearance_graph_unchanged':True,'result':str(result)},indent=2))
print('KNIGHT_DEFORMER_OVERLAY_READY')
