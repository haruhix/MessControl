"""Author only the shared monster overlay; no artist materials or map are modified."""
import json
import unreal as u

path = "/Game/Gameplay/VFX/M_CombatHitOverlay"
dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
assert path not in {p.get_name() for p in dirty}, "Preserve unsaved overlay edits before authoring"
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
mat = lib.load_asset(path) if lib.does_asset_exist(path) else None
if mat is None:
    mat = u.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_CombatHitOverlay", "/Game/Gameplay/VFX", u.Material, u.MaterialFactoryNew())
assert mat is not None
edit.delete_all_material_expressions(mat)
mat.set_editor_property("blend_mode", u.BlendMode.BLEND_ADDITIVE)
mat.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)
mat.set_editor_property("used_with_skeletal_mesh", True)
color = edit.create_material_expression(mat, u.MaterialExpressionVectorParameter, -200, 0)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", u.LinearColor(1, .92, .72, 1))
opacity = edit.create_material_expression(mat, u.MaterialExpressionScalarParameter, -200, 160)
opacity.set_editor_property("parameter_name", "Opacity")
opacity.set_editor_property("default_value", 0)
assert edit.connect_material_property(color, "", u.MaterialProperty.MP_EMISSIVE_COLOR)
assert edit.connect_material_property(opacity, "", u.MaterialProperty.MP_OPACITY)
edit.recompile_material(mat)
assert lib.save_loaded_asset(mat, only_if_is_dirty=False)
assert {p.get_name() for p in dirty}.issubset(
    {p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()})
print("COMBAT_OVERLAY " + json.dumps(dict(asset=mat.get_path_name(),
    skeletal_usage=mat.get_editor_property("used_with_skeletal_mesh"), vertex_color_dependency=False)))
