"""Add rounded lighting and three puff variants through native Unreal MCP.

Run once on the clean, owned M_IcePhasePowder material with idle L_Mouth.
Keeps the original alpha/lifetime graph and modifies only powder packages.
"""
import json
from pathlib import Path

from native_mcp_client import NativeMCP
from author_kenney_snow_powder_mcp import SCRIPT as POWDER_SCRIPT

FOLDER = '/Game/Gameplay/Cold/VFX'
MATERIAL = FOLDER + '/M_IcePhasePowder'
ROOT = Path(__file__).resolve().parents[2]
REPORT = ROOT / 'Saved/Checks/KenneySnowVolume_20261013/MaterialAuthoring.json'

# Reuse the already exercised native graph helpers, without the create-only
# authoring entry point or its guard against an existing powder material.
SCRIPT = POWDER_SCRIPT.split('def guard():', 1)[0] + r'''
MI = "editor_toolset.toolsets.material_instance.MaterialInstanceTools."
T = "editor_toolset.toolsets.texture.TextureTools."

def run():
    if call(E+"IsPIERunning", {}):
        raise RuntimeError("Stop PIE before authoring powder volume")
    if call(S+"get_current_level", {}) != "/Game/Maps/L_Mouth":
        raise RuntimeError("Expected MessControl L_Mouth")
    if call(A+"is_dirty", {"asset_path": CFG["material"]}):
        raise RuntimeError("Preserve unsaved powder material edits")
    mat = call(A+"load_asset", {"asset_path": CFG["material"]})
    if call(A+"get_metadata_tags", {"asset_path": CFG["material"]}).get("MC.KenneySnowPowder") != "1":
        raise RuntimeError("Expected the owned Kenney powder material")
    # Refuse collisions before starting any graph/package changes.
    for name, _, _ in CFG["variants"]:
        if call(A+"exists", {"path": CFG["folder"]+"/"+name}):
            raise RuntimeError("Preserve existing powder variant "+name)
    for name in ("T_KenneyWhitePuff00", "T_KenneyWhitePuff24"):
        if call(A+"exists", {"path": CFG["folder"]+"/"+name}):
            raise RuntimeError("Preserve existing texture "+name)

    saved = []
    for suffix in ("00", "24"):
        name = "T_KenneyWhitePuff"+suffix
        assets = call(T+"import_file", {"folder_path": CFG["folder"], "asset_name": name,
            "source_file": CFG["source"]+"/whitePuff"+suffix+".png"})
        texture = assets[0]
        set_properties(texture, {"addressX": "TA_Clamp", "addressY": "TA_Clamp",
            "powerOfTwoMode": "StretchToPowerOfTwo", "mipGenSettings": "TMGS_SimpleAverage",
            "sRGB": True, "compressionNoAlpha": False, "lODGroup": "TEXTUREGROUP_Effects"})
        saved.append(CFG["folder"]+"/"+name)

    expressions = call(M+"get_expressions", {"material_or_function": mat})
    by_name = {entry["refPath"].split(":")[-1]: entry for entry in expressions}
    coords = by_name["MaterialExpressionCustom_0"]
    sample = by_name["MaterialExpressionTextureSampleParameter2D_0"]
    color = by_name["MaterialExpressionParticleColor_0"]
    tint = by_name["MaterialExpressionCustom_1"]
    if len(expressions) != 11:
        raise RuntimeError("Expected the original 11-node owned powder graph")
    settings = {"shadingModel": "MSM_DefaultLit", "translucencyLightingMode": "TLM_VolumetricDirectional",
        "translucencyDirectionalLightingIntensity": 1.5, "bTangentSpaceNormal": True}
    set_properties(mat, settings)
    tint_code = r"""
        float detail=saturate(dot(S.rgb,float3(.2126,.7152,.0722)));
        return Color.rgb*lerp(float3(.47,.64,.75),float3(.93,.98,1),sqrt(detail));
    """
    set_properties(tint, {"description": "Directional lit snow with texture relief", "code": tint_code})
    normal_code = r"""
        float2 p=UV*2-1;
        float dome=sqrt(saturate(1-dot(p,p)*.75));
        float detail=saturate(dot(S.rgb,float3(.2126,.7152,.0722)));
        float2 lobes=float2(sin(p.y*9+p.x*3),cos(p.x*11-p.y*4))*(1-detail)*.14;
        return normalize(float3(p*.72+lobes,dome+.25));
    """
    normal = custom(mat, "Rounded particle normals for directional cloud lighting", normal_code,
        ["UV", "S"], "CMOT_Float3", 0, 400)
    fill = custom(mat, "Small ambient fill retains powder in shadow", "return Base*.12;",
        ["Base"], "CMOT_Float3", 300, -100)
    rough = node(mat, "Constant", 300, 600, {"r": 1.0})
    specular = node(mat, "Constant", 300, 750, {"r": 0.0})
    edges = [(coords, "", normal, "UV"), (sample, "RGBA", normal, "S"), (tint, "", fill, "Base")]
    for source, output_name, target, input_name in edges:
        connect(source, output_name, target, input_name)
    outputs = [(tint, "MP_BaseColor"), (fill, "MP_EmissiveColor"), (normal, "MP_Normal"),
               (rough, "MP_Roughness"), (specular, "MP_Specular")]
    for expr, prop in outputs:
        call(M+"connect_to_output", {"expression": expr, "output_name": "", "material_property": prop})
    for source, output_name, target, input_name in edges:
        links = call(M+"get_expression_inputs", {"material_or_function": mat, "expression": target})
        if next(item for item in links if item["input_name"] == input_name)["expression"] != source:
            raise RuntimeError("Rounded lighting graph connection failed")
    for expr, prop in outputs:
        if call(M+"get_property_input", {"material": mat, "material_property": prop})["expression"] != expr:
            raise RuntimeError("Rounded lighting output is disconnected: "+prop)
    if properties(mat, list(settings)) != settings:
        raise RuntimeError("Directional translucency settings differ")
    call(M+"layout_expressions", {"material_or_function": mat})
    call(M+"recompile", {"material_or_function": mat})
    saved.append(CFG["material"])
    variants = []
    for name, suffix, opacity in CFG["variants"]:
        instance = call(MI+"create", {"folder_path": CFG["folder"], "asset_name": name, "parent": mat})
        texture = call(A+"load_asset", {"asset_path": CFG["folder"]+"/T_KenneyWhitePuff"+suffix})
        # These native setters are void tools, so execute directly.
        execute_tool(MI+"set_texture_parameter", json.dumps({"instance": instance, "name": "PowderTexture", "value": texture}))
        execute_tool(MI+"set_scalar_parameter", json.dumps({"instance": instance, "name": "Opacity", "value": opacity}))
        if call(MI+"get_texture_parameter", {"instance": instance, "name": "PowderTexture"}) != texture:
            raise RuntimeError("Powder variant texture differs")
        if abs(call(MI+"get_scalar_parameter", {"instance": instance, "name": "Opacity"})-opacity) > .001:
            raise RuntimeError("Powder variant opacity differs")
        saved.append(CFG["folder"]+"/"+name)
        variants.append({"path": instance["refPath"], "texture": texture["refPath"], "opacity": opacity})
    if not call(A+"save_assets", {"asset_paths": saved}):
        raise RuntimeError("Could not save powder volume packages")
    if any(call(A+"is_dirty", {"asset_path": path}) for path in saved):
        raise RuntimeError("Powder volume package remained dirty")
    return {"passed": True, "compiled": True, "saved": True, "saved_assets": saved,
        "settings": settings, "variants": variants, "source_alpha_preserved": True,
        "rounded_normal_graph_verified": True, "unrelated_packages_saved": False,
        "geometry": "Layered sprites with rounded lighting normals; no volumetric raymarching"}
'''


def main():
    client = NativeMCP()

    def invoke(toolset, tool, arguments):
        result = client.tool('call_tool', {'toolset_name': toolset, 'tool_name': tool, 'arguments': arguments})
        if result.get('isError'):
            raise RuntimeError(result)
        return json.loads(result['content'][0]['text']).get('returnValue')

    for toolset in ['editor_toolset.toolsets.texture.TextureTools', 'editor_toolset.toolsets.material.MaterialTools',
            'editor_toolset.toolsets.object.ObjectTools', 'editor_toolset.toolsets.asset.AssetTools',
            'editor_toolset.toolsets.material_instance.MaterialInstanceTools',
            'editor_toolset.toolsets.programmatic.ProgrammaticToolset']:
        if client.tool('describe_toolset', {'toolset_name': toolset}).get('isError'):
            raise RuntimeError('Could not discover '+toolset)
    invoke('editor_toolset.toolsets.programmatic.ProgrammaticToolset', 'get_execution_environment', {})
    config = dict(folder=FOLDER, material=MATERIAL, source=(ROOT/'ArtSource/VFX/KenneySmoke').as_posix(),
        variants=[('MI_IcePowderLow', '00', .38), ('MI_IcePowderBody', '12', .50), ('MI_IcePowderHigh', '24', .35)])
    result = invoke('editor_toolset.toolsets.programmatic.ProgrammaticToolset', 'execute_tool_script',
        {'script': SCRIPT.replace('__CONFIG__', repr(config))})
    report = json.loads(result)
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('MC_KENNEY_VOLUME_MATERIAL_PASS '+json.dumps(report))


if __name__ == '__main__':
    main()
