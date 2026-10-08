"""Author the storm's readable landing ring and radial countdown in Unreal.

Uses existing engine material APIs, so authoring can precede the native build.
Only the exact previous generated soft-shadow graph is migrated; custom graphs
are refused intact. Native code updates LandingProgress and ShadowStrength.
"""
import json
from pathlib import Path

import unreal as u


RING_CODE = """
// MC_NUT_LANDING_RING_V2
float2 p=UV*2-1;
float r=length(p);
float progress=saturate(Progress);
float boundary=smoothstep(.91,.95,r)*(1-smoothstep(.98,1,r));
float fillRadius=sqrt(progress)*.96;
float fill=(1-smoothstep(fillRadius-.03,fillRadius+.03,r))*.25;
float scan=(1-smoothstep(.025,.055,abs(r-fillRadius)))*.28*progress;
float inside=1-smoothstep(.97,1,r);
float visible=saturate(Strength*8);
float density=(boundary*.78+fill+scan)*inside;
float3 tint=lerp(float3(1.8,.52,.025),float3(2.2,.12,.015),progress);
return float4(tint*density, saturate(density*visible));
"""


def old_generated_graph(material, editor):
    """Validate values and wiring, including the original untagged saved graph."""
    nodes = list(editor.get_material_expressions(material))
    classes = (u.MaterialExpressionTextureCoordinate, u.MaterialExpressionConstant2Vector,
               u.MaterialExpressionSphereMask, u.MaterialExpressionScalarParameter,
               u.MaterialExpressionMultiply, u.MaterialExpressionConstant3Vector)
    if len(nodes) != len(classes):
        return False
    grouped = [[node for node in nodes if isinstance(node, cls)] for cls in classes]
    if any(len(items) != 1 for items in grouped):
        return False
    uv, center, mask, strength, opacity, black = [items[0] for items in grouped]
    near = lambda a, b: abs(float(a)-b) < 1e-6
    color = black.get_editor_property("constant")
    if not (uv.get_editor_property("coordinate_index") == 0 and
            near(center.get_editor_property("r"), .5) and near(center.get_editor_property("g"), .5) and
            near(mask.get_editor_property("attenuation_radius"), .5) and near(mask.get_editor_property("hardness_percent"), 55) and
            str(strength.get_editor_property("parameter_name")) == "ShadowStrength" and
            near(strength.get_editor_property("default_value"), .2) and
            near(color.r, .025) and near(color.g, .008) and near(color.b, .004)):
        return False
    inputs = lambda expr: [node for node in editor.get_inputs_for_material_expression(material, expr) if node]
    if inputs(mask) != [uv, center] or inputs(opacity) != [mask, strength]:
        return False
    return (editor.get_material_property_input_node(material, u.MaterialProperty.MP_BASE_COLOR) == black and
            editor.get_material_property_input_node(material, u.MaterialProperty.MP_OPACITY) == opacity and
            editor.get_material_property_input_node(material, u.MaterialProperty.MP_EMISSIVE_COLOR) is None and
            material.get_editor_property("material_domain") == u.MaterialDomain.MD_DEFERRED_DECAL and
            material.get_editor_property("blend_mode") == u.BlendMode.BLEND_TRANSLUCENT)


def build_ring(material, editor):
    editor.delete_all_material_expressions(material)
    material.set_editor_property("material_domain", u.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", u.BlendMode.BLEND_TRANSLUCENT)
    uv = editor.create_material_expression(material, u.MaterialExpressionTextureCoordinate, -700, 0)
    strength = editor.create_material_expression(material, u.MaterialExpressionScalarParameter, -700, 180)
    strength.set_editor_property("parameter_name", "ShadowStrength")
    strength.set_editor_property("default_value", .2)
    progress = editor.create_material_expression(material, u.MaterialExpressionScalarParameter, -700, 300)
    progress.set_editor_property("parameter_name", "LandingProgress")
    progress.set_editor_property("default_value", 0)
    ring = editor.create_material_expression(material, u.MaterialExpressionCustom, -400, 0)
    ring.set_editor_property("code", RING_CODE)
    ring.set_editor_property("output_type", u.CustomMaterialOutputType.CMOT_FLOAT4)
    names = ("UV", "Strength", "Progress")
    entries = []
    for name in names:
        item = u.CustomInput()
        item.set_editor_property("input_name", name)
        entries.append(item)
    ring.set_editor_property("inputs", entries)
    for source, name in zip((uv, strength, progress), names):
        if not editor.connect_material_expressions(source, "", ring, name):
            raise RuntimeError("Could not connect landing ring input " + name)
    color = editor.create_material_expression(material, u.MaterialExpressionComponentMask, -150, 0)
    for key, value in dict(r=True, g=True, b=True, a=False).items():
        color.set_editor_property(key, value)
    alpha = editor.create_material_expression(material, u.MaterialExpressionComponentMask, -150, 180)
    for key, value in dict(r=False, g=False, b=False, a=True).items():
        alpha.set_editor_property(key, value)
    black = editor.create_material_expression(material, u.MaterialExpressionConstant3Vector, -150, 320)
    black.set_editor_property("constant", u.LinearColor(.025, .008, .004, 1))
    for target in (color, alpha):
        # The mask's displayed first pin is unnamed; an empty name selects input 0.
        if not editor.connect_material_expressions(ring, "", target, ""):
            raise RuntimeError("Could not connect landing ring channel mask")
    for node, target in ((color, u.MaterialProperty.MP_EMISSIVE_COLOR),
                         (alpha, u.MaterialProperty.MP_OPACITY), (black, u.MaterialProperty.MP_BASE_COLOR)):
        if not editor.connect_material_property(node, "", target):
            raise RuntimeError("Could not connect landing ring output " + str(target))
    u.EditorAssetLibrary.set_metadata_tag(material, "MCNutLandingMaterialVersion", "2")


def main():
    # LevelEditorSubsystem's PIE query assumes an initialized Level Editor UI.
    # Python commandlets have no UI or PIE, and calling it there can crash native code.
    commandlet = "-run=" in u.SystemLibrary.get_command_line().lower()
    if not commandlet and u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor():
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
    if not isinstance(material, u.Material):
        raise RuntimeError("Landing-shadow path has an unexpected asset type")
    editor = u.MaterialEditingLibrary
    migrated = not created and old_generated_graph(material, editor)
    if created or migrated:
        build_ring(material, editor)
    else:
        generated = [node for node in editor.get_material_expressions(material)
                     if isinstance(node, u.MaterialExpressionCustom) and node.get_editor_property("code") == RING_CODE]
        if len(generated) != 1 or u.EditorAssetLibrary.get_metadata_tag(material, "MCNutLandingMaterialVersion") != "2":
            raise RuntimeError("Preserved unrecognized/custom landing material intact; nodes=" +
                               repr([node.get_class().get_name() for node in editor.get_material_expressions(material)]))
    editor.recompile_material(material)
    if not u.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Could not save landing ring material")
    report = dict(complete=True, created=created, material=material.get_path_name(),
                  migrated_soft_shadow=migrated, generated_version=2,
                  domain=str(material.get_editor_property("material_domain")),
                  blend=str(material.get_editor_property("blend_mode")))
    report_path = Path(u.Paths.project_saved_dir()) / "NutRain" / "LandingShadowAuthoring.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    u.log("MC_NUT_LANDING_SHADOW_AUTHOR_PASS " + json.dumps(report))


if __name__ == "__main__":
    main()
