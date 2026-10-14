"""Author only /Game/Cataclysm/Jelly gel assets in a stopped owned Unreal Editor.

Run through Tools/Unreal/remote_python.py after native classes are available.
This script does not import meshes, edit a level, start PIE, or change shared art.
Research: Sam Perzanowski's original slime breakdown (static meshes, masked
sine vertex motion and controlled bubble swelling):
https://realtimevfx.com/t/vfx-experiments-personal-wip-sam-perzanowski/1278
UE 5.8 material inputs and Fresnel tutorials:
https://dev.epicgames.com/documentation/en-us/unreal-engine/material-inputs-in-unreal-engine
https://dev.epicgames.com/documentation/en-us/unreal-engine/using-fresnel-in-your-unreal-engine-materials
Our deformation amplitudes and warning colors are project choices.
"""
import json

import unreal as u

FOLDER = "/Game/Cataclysm/Jelly"
NAME = "M_CataclysmJellyGel"
PATH = FOLDER + "/" + NAME
OWNER_TAG = "MCCataclysmJellyAuthor"
OWNER_VALUE = "gel_v1"
BUILD_TAG = "MCCataclysmJellyBuildState"
REQUIRED_PARAMETERS = {"JellyWarning", "JellyWobble", "JellyHit"}

assert not u.EditorLevelLibrary.get_pie_worlds(False), "Stop PIE before material authoring."
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
tools = u.AssetToolsHelpers.get_asset_tools()
lib.make_directory(FOLDER)
material = lib.load_asset(PATH)
created = material is None
if material is not None:
    assert isinstance(material, u.Material), PATH + " is occupied by a different asset type"
    assert lib.get_metadata_tag(material, OWNER_TAG) == OWNER_VALUE, "Preserve unrelated art at " + PATH
else:
    material = tools.create_asset(NAME, FOLDER, u.Material, u.MaterialFactoryNew())
    assert material, "Failed to create " + PATH
    lib.set_metadata_tag(material, OWNER_TAG, OWNER_VALUE)

# A failed first run can leave an owned, unsaved material in the editor. Rebuild
# only that tagged incomplete graph; never overwrite an untagged or ready asset.
existing_nodes = list(edit.get_material_expressions(material))
existing_parameters = {str(node.get_editor_property("parameter_name")) for node in existing_nodes
                       if isinstance(node, (u.MaterialExpressionScalarParameter, u.MaterialExpressionVectorParameter))}
build_state = lib.get_metadata_tag(material, BUILD_TAG)
if build_state == "ready":
    assert REQUIRED_PARAMETERS.issubset(existing_parameters), "Preserve edited ready material; required runtime parameters are missing: " + PATH
in_progress = build_state == "building"
rebuilding_incomplete = not created and (in_progress or (build_state != "ready" and not REQUIRED_PARAMETERS.issubset(existing_parameters)))
build_graph = created or rebuilding_incomplete


def expression(kind, x, y):
    return edit.create_material_expression(material, kind, x, y)


def scalar(name, value, x, y):
    node = expression(u.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    node.set_editor_property("group", "Jelly")
    return node


def vector(name, value, x, y):
    node = expression(u.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", u.LinearColor(*value))
    node.set_editor_property("group", "Jelly")
    return node


def custom(code, inputs, x, y, description):
    node = expression(u.MaterialExpressionCustom, x, y)
    node.set_editor_property("description", description)
    node.set_editor_property("output_type", u.CustomMaterialOutputType.CMOT_FLOAT3)
    entries = []
    for name, source in inputs.items():
        entry = u.CustomInput()
        entry.set_editor_property("input_name", name)
        entries.append(entry)
    node.set_editor_property("inputs", entries)
    node.set_editor_property("code", code)
    for name, source in inputs.items():
        assert edit.connect_material_expressions(source, "", node, name), name
    return node


if build_graph:
    lib.set_metadata_tag(material, BUILD_TAG, "building")
    if rebuilding_incomplete:
        edit.delete_all_material_expressions(material)
    material.set_editor_property("blend_mode", u.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("shading_model", u.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("two_sided", False)
    # These ordinary exposed expressions work for the native static sphere.
    # Jelly rotations are yaw-only, so centered world Z remains the seated axis.
    world = expression(u.MaterialExpressionWorldPosition, -1450, -100)
    center = expression(u.MaterialExpressionObjectPositionWS, -1450, 100)
    position = expression(u.MaterialExpressionSubtract, -1100, 0)
    assert edit.connect_material_expressions(world, "", position, "A")
    assert edit.connect_material_expressions(center, "", position, "B")
    normal = expression(u.MaterialExpressionVertexNormalWS, -1100, 210)
    time = expression(u.MaterialExpressionTime, -1100, 420)
    fresnel = expression(u.MaterialExpressionFresnel, -1100, 640)
    fresnel.set_editor_property("exponent", 3.5)
    fresnel.set_editor_property("base_reflect_fraction", .04)
    base = vector("JellyColor", (.12, .22, .033, 1), -850, 720)
    rim = vector("JellyRimColor", (.39, .56, .11, 1), -850, 870)
    warning = scalar("JellyWarning", 0, -850, 1060)
    wobble = scalar("JellyWobble", 2, -850, 1240)
    hit = scalar("JellyHit", 0, -850, 1420)
    color = custom("""
float bands = .88 + .07 * sin(P.z*.12 + sin(P.x*.045)*1.3);
float3 gel = lerp(Base.rgb*bands, Rim.rgb, saturate(F*.62));
float pulse = .5 + .5*sin(T*17);
gel = lerp(gel, float3(.62,.31,.035), saturate(Warning)*(.45+.22*pulse));
return lerp(gel, float3(.85,.94,.54), saturate(Hit)*.72);
""", {"P": position, "Base": base, "Rim": rim, "F": fresnel,
        "T": time, "Warning": warning, "Hit": hit}, -340, 600,
        "Opaque gel volume tint, wet rim and gameplay warning")
    deformation = custom("""
float anchor = smoothstep(-47,-12,P.z);
float wave = sin(P.x*.071+T*4.8)*.46 + sin(P.y*.093-T*3.7)*.34
    + sin(P.z*.066+T*5.1)*.20;
return N * wave * anchor * clamp(Wobble,0,10);
""", {"P": position, "N": normal, "T": time, "Wobble": wobble}, -340, 50,
        "Small sine wobble with a seated lower surface")
    emission = custom("""
float pulse=.5+.5*sin(T*17);
return Rim.rgb*(F*.09+saturate(Warning)*pulse*.09+saturate(Hit)*.12);
""", {"Rim": rim, "F": fresnel, "T": time,
        "Warning": warning, "Hit": hit}, -340, 1040,
        "Restrained warning rim; no translucent overlay")
    roughness = scalar("JellyRoughness", .19, -300, 1270)
    specular = scalar("JellySpecular", .65, -300, 1410)
    metallic = scalar("JellyMetallic", 0, -300, 1550)
    for source, target in (
        (color, u.MaterialProperty.MP_BASE_COLOR),
        (deformation, u.MaterialProperty.MP_WORLD_POSITION_OFFSET),
        (emission, u.MaterialProperty.MP_EMISSIVE_COLOR),
        (roughness, u.MaterialProperty.MP_ROUGHNESS),
        (specular, u.MaterialProperty.MP_SPECULAR),
        (metallic, u.MaterialProperty.MP_METALLIC),
    ):
        assert edit.connect_material_property(source, "", target), str(target)
    errors = edit.recompile_material(material)
    if errors:
        raise RuntimeError("Jelly material compilation diagnostics: " + str(errors))
    lib.set_metadata_tag(material, BUILD_TAG, "ready")
    if not lib.save_loaded_asset(material, False):
        lib.set_metadata_tag(material, BUILD_TAG, "building")
        raise RuntimeError("Failed to save jelly material")

nodes = list(edit.get_material_expressions(material))
parameters = sorted(str(node.get_editor_property("parameter_name")) for node in nodes
                    if isinstance(node, (u.MaterialExpressionScalarParameter, u.MaterialExpressionVectorParameter)))
assert REQUIRED_PARAMETERS.issubset(parameters)
print(json.dumps({"asset": PATH, "created": created, "parameters": parameters,
                  "rebuilt_owned_incomplete": rebuilding_incomplete,
                  "geometry": "/Engine/BasicShapes/Sphere", "textures": 0,
                  "runtime_and_gameplay_camera_validation": "pending"}, ensure_ascii=False))
