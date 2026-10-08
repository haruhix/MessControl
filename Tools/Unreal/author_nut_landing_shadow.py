"""Author only the storm's faint soft landing-shadow material in Unreal.

Run after the native build. Existing designer material edits are preserved.
"""
import json
from pathlib import Path

import unreal as u


def main():
    if u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError("Stop the owned PIE session before authoring the landing material")
    dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    dirty += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    if dirty:
        raise RuntimeError("Preserve unsaved packages before landing-shadow authoring: " + ", ".join(p.get_name() for p in dirty))
    path = "/Game/Gameplay/CoreLoop/M_NutLandingShadow"
    material = u.load_asset(path)
    created = not material
    if created:
        tools = u.AssetToolsHelpers.get_asset_tools()
        material = tools.create_asset("M_NutLandingShadow", "/Game/Gameplay/CoreLoop", u.Material, u.MaterialFactoryNew())
        if not material:
            raise RuntimeError("Could not create the nut landing material")
        material.set_editor_property("material_domain", u.MaterialDomain.MD_DEFERRED_DECAL)
        material.set_editor_property("blend_mode", u.BlendMode.BLEND_TRANSLUCENT)
        editor = u.MaterialEditingLibrary
        uv = editor.create_material_expression(material, u.MaterialExpressionTextureCoordinate, -640, 0)
        center = editor.create_material_expression(material, u.MaterialExpressionConstant2Vector, -640, 160)
        center.set_editor_property("r", .5)
        center.set_editor_property("g", .5)
        mask = editor.create_material_expression(material, u.MaterialExpressionSphereMask, -400, 0)
        mask.set_editor_property("attenuation_radius", .5)
        # Keep a readable faint center and a broad soft outer falloff.
        mask.set_editor_property("hardness_percent", 55.0)
        strength = editor.create_material_expression(material, u.MaterialExpressionScalarParameter, -400, 240)
        strength.set_editor_property("parameter_name", "ShadowStrength")
        strength.set_editor_property("default_value", .20)
        opacity = editor.create_material_expression(material, u.MaterialExpressionMultiply, -160, 0)
        black = editor.create_material_expression(material, u.MaterialExpressionConstant3Vector, -160, 240)
        black.set_editor_property("constant", u.LinearColor(0.025, 0.008, 0.004, 1.0))
        connections = [
            (uv, "", mask, "A"), (center, "", mask, "B"),
            (mask, "", opacity, "A"), (strength, "", opacity, "B"),
        ]
        for source, output, target, input_name in connections:
            if not editor.connect_material_expressions(source, output, target, input_name):
                raise RuntimeError("Could not connect soft shadow graph: " + input_name)
        if not editor.connect_material_property(black, "", u.MaterialProperty.MP_BASE_COLOR):
            raise RuntimeError("Could not connect landing shadow color")
        if not editor.connect_material_property(opacity, "", u.MaterialProperty.MP_OPACITY):
            raise RuntimeError("Could not connect landing shadow opacity")
        editor.recompile_material(material)
        if not u.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
            raise RuntimeError("Could not save landing shadow material")
    if not isinstance(material, u.Material):
        raise RuntimeError("Landing-shadow path has an unexpected asset type")
    report = dict(complete=True, created=created, material=material.get_path_name(),
                  domain=str(material.get_editor_property("material_domain")),
                  blend=str(material.get_editor_property("blend_mode")))
    report_path = Path(u.Paths.project_saved_dir()) / "NutRain" / "LandingShadowAuthoring.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    u.log("MC_NUT_LANDING_SHADOW_AUTHOR_PASS " + json.dumps(report))


if __name__ == "__main__":
    main()
