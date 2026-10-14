"""Author the isolated cleanable gel-pen trail material in a stopped owned editor.

Run via remote_python.py after the native tile code is available. Never starts
PIE or changes a level/shared coffee material. Reuses the slime wet-film direction
reviewed in Docs/VFXReferences.md (Sam Perzanowski), plus Epic's material inputs.
The shape comes only from the replicated 64x64 mask, including across tile edges.
This script is prepared offline; authoring and gameplay-camera review are pending.
"""
import json
import unreal as u

FOLDER = "/Game/Cataclysm/Jelly"
NAME = "M_CataclysmPaintTrail"
PATH = FOLDER + "/" + NAME
OWNER_TAG = "MCCataclysmPaintAuthor"
OWNER_VALUE = "mask_tile_v1"
BUILD_TAG = "MCCataclysmPaintBuildState"
PARAMETERS = {"WipeMask", "Finish", "WorldSize", "LiquidColor", "Roughness", "Specular"}

assert not u.EditorLevelLibrary.get_pie_worlds(False), "Stop PIE before material authoring."
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
lib.make_directory(FOLDER)
material = lib.load_asset(PATH)
created = material is None
if material is not None:
    assert isinstance(material, u.Material), "Preserve different asset type at " + PATH
    assert lib.get_metadata_tag(material, OWNER_TAG) == OWNER_VALUE, "Preserve unrelated art at " + PATH
else:
    material = assets.create_asset(NAME, FOLDER, u.Material, u.MaterialFactoryNew())
    assert material
    lib.set_metadata_tag(material, OWNER_TAG, OWNER_VALUE)


def parameters():
    return {str(n.get_editor_property("parameter_name")) for n in edit.get_material_expressions(material)
            if isinstance(n, (u.MaterialExpressionScalarParameter, u.MaterialExpressionVectorParameter,
                              u.MaterialExpressionTextureObjectParameter))}


ready = lib.get_metadata_tag(material, BUILD_TAG) == "ready"
if ready:
    assert PARAMETERS.issubset(parameters()), "Preserve edited ready material with missing parameters"
else:
    lib.set_metadata_tag(material, BUILD_TAG, "building")
    # Delete a stable snapshot: UE 5.8's bulk helper can retain duplicate nodes.
    for item in list(edit.get_material_expressions(material)):
        edit.delete_material_expression(material, item)
    assert not edit.get_num_material_expressions(material)
    material.set_editor_property("blend_mode", u.BlendMode.BLEND_MASKED)
    material.set_editor_property("opacity_mask_clip_value", .25)
    material.set_editor_property("two_sided", False)
    material.set_editor_property("shading_model", u.MaterialShadingModel.MSM_DEFAULT_LIT)

    def node(kind, x, y):
        return edit.create_material_expression(material, kind, x, y)

    def scalar(name, value, y):
        n = node(u.MaterialExpressionScalarParameter, -1400, y)
        n.set_editor_property("parameter_name", name)
        n.set_editor_property("default_value", value)
        n.set_editor_property("group", "Gel Paint")
        return n

    def custom(description, code, inputs, x, y, output=u.CustomMaterialOutputType.CMOT_FLOAT1):
        n = node(u.MaterialExpressionCustom, x, y)
        n.set_editor_property("description", description)
        n.set_editor_property("code", code)
        n.set_editor_property("output_type", output)
        entries = []
        for key in inputs:
            item = u.CustomInput()
            item.set_editor_property("input_name", key)
            entries.append(item)
        n.set_editor_property("inputs", entries)
        for key, (source, output_pin) in inputs.items():
            assert edit.connect_material_expressions(source, output_pin, n, key), key
        return n

    def output(n, target):
        assert edit.connect_material_property(n, "", target), str(target)

    uv = node(u.MaterialExpressionTextureCoordinate, -1800, 0)
    mask = node(u.MaterialExpressionTextureObjectParameter, -1800, 200)
    mask.set_editor_property("parameter_name", "WipeMask")
    mask.set_editor_property("texture", lib.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
    mask.set_editor_property("sampler_type", u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    finish = scalar("Finish", 0, 400)
    size = scalar("WorldSize", 1200, 550)
    inputs = {"UV": (uv, ""), "Wipe": (mask, ""), "Finish": (finish, "")}
    coverage = custom("Exact tile coverage: no seeded blob or global care fraction", """
return Texture2DSampleLevel(Wipe,WipeSampler,UV,0).r*(1-saturate(Finish));
""", inputs, -750, 0)
    output(coverage, u.MaterialProperty.MP_OPACITY_MASK)

    # Rounded wet edge comes from the same mask. No UV displacement can refill
    # an old cleared track or make neighboring tile edges diverge.
    normal = custom("Small wet meniscus from coverage", """
float e=1.0/64.0;
float x=Texture2DSampleLevel(Wipe,WipeSampler,UV+float2(e,0),0).r
       -Texture2DSampleLevel(Wipe,WipeSampler,UV-float2(e,0),0).r;
float y=Texture2DSampleLevel(Wipe,WipeSampler,UV+float2(0,e),0).r
       -Texture2DSampleLevel(Wipe,WipeSampler,UV-float2(0,e),0).r;
return normalize(float3(-x*1.6/max(WorldSize*e*2,1),-y*1.6/max(WorldSize*e*2,1),1));
""", {"UV": (uv, ""), "Wipe": (mask, ""), "WorldSize": (size, "")}, -750, 450,
        u.CustomMaterialOutputType.CMOT_FLOAT3)
    output(normal, u.MaterialProperty.MP_NORMAL)

    color = node(u.MaterialExpressionVectorParameter, -1400, 800)
    color.set_editor_property("parameter_name", "LiquidColor")
    color.set_editor_property("default_value", u.LinearColor(.12, .22, .033, 1))
    color.set_editor_property("group", "Gel Paint")
    tint = custom("Opaque wet gel film", "return Tint.rgb*(.80+.20*saturate(Coverage));",
                  {"Tint": (color, "RGBA"), "Coverage": (coverage, "")}, -350, 800,
                  u.CustomMaterialOutputType.CMOT_FLOAT3)
    output(tint, u.MaterialProperty.MP_BASE_COLOR)
    output(scalar("Roughness", .18, 1000), u.MaterialProperty.MP_ROUGHNESS)
    output(scalar("Specular", .72, 1150), u.MaterialProperty.MP_SPECULAR)
    # Modest reflected gleam makes the thin line read in the enclosed arena.
    # This is opaque/masked film, with no extra transparent gel surface.
    view = node(u.MaterialExpressionCameraVectorWS, -1400, 1350)
    pixel_normal = node(u.MaterialExpressionPixelNormalWS, -1400, 1500)
    gleam = custom("Restrained wet glint", """
float3 reflected=reflect(-normalize(View),normalize(Normal));
float key=pow(saturate(dot(reflected,normalize(float3(.65,.25,.72)))),80);
return float3(.45,.62,.20)*key*.18;
""", {"View": (view, ""), "Normal": (pixel_normal, "")}, -350, 1300,
        u.CustomMaterialOutputType.CMOT_FLOAT3)
    output(gleam, u.MaterialProperty.MP_EMISSIVE_COLOR)
    errors = edit.recompile_material(material)
    assert not errors, "Paint material compilation diagnostics: " + str(errors)
    lib.set_metadata_tag(material, BUILD_TAG, "ready")
    if not lib.save_loaded_asset(material, False):
        lib.set_metadata_tag(material, BUILD_TAG, "building")
        raise RuntimeError("Cannot save " + PATH)

assert PARAMETERS.issubset(parameters())
print(json.dumps({"asset": PATH, "created": created, "parameters": sorted(parameters()),
                  "shape": "replicated 64x64 coverage; masked clip .25", "textures": 0,
                  "gameplay_camera_validation": "pending"}))
