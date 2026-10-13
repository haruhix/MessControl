"""Create one Kenney snow-powder material through Epic's native MCP tools.

Import the original whitePuff12 PNG as T_KenneyWhitePuff12 first. Run with
MessControl's L_Mouth open and PIE stopped. Existing target assets are refused;
only the new material is compiled and saved. No build or gameplay changes.
"""
import json
import subprocess
import sys
import uuid
from pathlib import Path

from native_mcp_client import NativeMCP


FOLDER = "/Game/Gameplay/Cold/VFX"
MATERIAL_PATH = FOLDER + "/M_IcePhasePowder"
TEXTURE_PATH = FOLDER + "/T_KenneyWhitePuff12"
REPORT_PATH = (Path(__file__).resolve().parents[2] / "Saved" / "Checks"
               / "KenneySnowPowder_20261013" / "MaterialAuthoring.json")

SPRITE_USAGE_SCRIPT = r'''
from pathlib import Path
import unreal as u

project = Path(u.Paths.project_dir()).resolve().as_posix().casefold()
if project != Path(__PROJECT_ROOT__).resolve().as_posix().casefold():
    raise RuntimeError("Niagara sprite usage follow-up requires this MessControl checkout")
level = u.get_editor_subsystem(u.LevelEditorSubsystem)
if level.is_in_play_in_editor():
    raise RuntimeError("Stop PIE before enabling Niagara sprite usage")
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
if not world or world.get_path_name().split(".")[0] != "/Game/Maps/L_Mouth":
    raise RuntimeError("Niagara sprite usage follow-up requires L_Mouth")
material_path = __MATERIAL_PATH__
dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
dirty += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
if any(package.get_name() in (material_path, "/Game/Maps/L_Mouth") for package in dirty):
    raise RuntimeError("Preserve unsaved material or L_Mouth edits before enabling sprite usage")
material = u.load_asset(material_path)
if not isinstance(material, u.Material):
    raise RuntimeError("The newly authored powder material is missing")
usage = u.MaterialUsage.MATUSAGE_NIAGARA_SPRITES
u.MaterialEditingLibrary.set_base_material_usage(material, usage)
if not u.MaterialEditingLibrary.has_material_usage(material, usage):
    raise RuntimeError("NiagaraSprites usage was not enabled")
# No package save here. The MCP shader barrier and explicit material-only save
# happen in the parent helper after this native assertion succeeds.
u.log("MC_KENNEY_SPRITE_USAGE_ENABLED " + material_path)
'''

SCRIPT = r'''
import json

M = "editor_toolset.toolsets.material.MaterialTools."
O = "editor_toolset.toolsets.object.ObjectTools."
A = "editor_toolset.toolsets.asset.AssetTools."
S = "editor_toolset.toolsets.scene.SceneTools."
E = "EditorToolset.EditorAppToolset."
CFG = __CONFIG__

def call(name, arguments):
    result = execute_tool(name, json.dumps(arguments))
    # execute_tool raises on native failure. Its result is a sandboxed
    # mapping whose get() raises for missing fields, including void tools.
    if name in (M + "connect_expressions", M + "connect_to_output",
                M + "recompile", M + "layout_expressions"):
        return None
    return result["returnValue"]

def properties(obj, names):
    return json.loads(call(O + "get_properties", {"instance": obj, "properties": names}))

def available_properties(obj):
    return json.loads(call(O + "list_properties", {"instance": obj}))

def set_properties(obj, values):
    available = available_properties(obj)
    missing = [name for name in values if name not in available]
    if missing:
        raise RuntimeError("Unavailable native properties: " + str(missing))
    # ObjectTools resolves nested array properties against their current size.
    # Expand Custom.inputs first, then set the names and other node properties.
    if "inputs" in values:
        current = properties(obj, ["inputs"])["inputs"]
        wanted = values["inputs"]
        if len(current) != len(wanted):
            resized = current[:len(wanted)] + wanted[len(current):]
            if not call(O + "set_properties", {"instance": obj,
                    "values": json.dumps({"inputs": resized})}):
                raise RuntimeError("Cannot resize Custom inputs")
    if not call(O + "set_properties", {"instance": obj, "values": json.dumps(values)}):
        raise RuntimeError("Cannot set native properties: " + str(obj))

def node(mat, kind, x, y, values=None):
    obj = call(M + "add_expression", {"material_or_function": mat,
        "expression_class": {"refPath": "/Script/Engine.MaterialExpression" + kind},
        "x": x, "y": y})
    if not obj:
        raise RuntimeError("Cannot create material expression " + kind)
    if values:
        set_properties(obj, values)
    return obj

def custom(mat, description, code, names, output_type, x, y):
    return node(mat, "Custom", x, y, {"description": description, "code": code,
        "outputType": output_type, "inputs": [{"inputName": name} for name in names]})

def connect(source, output_name, target, input_name):
    inputs = call(M + "get_expression_input_names", {"expression": target})
    outputs = call(M + "get_expression_output_names", {"expression": source})
    if input_name not in inputs or (output_name and output_name not in outputs):
        raise RuntimeError("Invalid material pins: " + str([output_name, input_name, outputs, inputs]))
    call(M + "connect_expressions", {"from_expression": source,
        "from_output_name": output_name, "to_expression": target, "to_input_name": input_name})

def guard():
    if call(E + "IsPIERunning", {}):
        raise RuntimeError("Stop PIE before authoring the Kenney material")
    level = call(S + "get_current_level", {})
    if not isinstance(level, str) or level.split(".")[0] != CFG["map"]:
        raise RuntimeError("Expected MessControl L_Mouth; current level is " + str(level))
    if call(A + "is_dirty", {"asset_path": CFG["map"]}):
        raise RuntimeError("Preserve unsaved L_Mouth edits before authoring")
    if call(A + "exists", {"path": CFG["material"]}):
        raise RuntimeError("Refusing to replace existing material " + CFG["material"])
    if not call(A + "exists", {"path": CFG["texture"]}):
        raise RuntimeError("Import the original whitePuff12 texture first")
    if call(A + "is_dirty", {"asset_path": CFG["texture"]}):
        raise RuntimeError("Save the imported Kenney texture before authoring")
    if call(A + "get_asset_class", {"asset_path": CFG["texture"]}) != "Texture2D":
        raise RuntimeError("Kenney input must be a Texture2D")
    return level

def run():
    level = guard()
    texture = call(A + "load_asset", {"asset_path": CFG["texture"]})
    # The source contains color and alpha. Read sRGB; never rewrite its pixels.
    texture_properties = available_properties(texture)
    srgb_name = next((name for name in texture_properties if name.lower() == "srgb"), None)
    if srgb_name and not properties(texture, [srgb_name])[srgb_name]:
        raise RuntimeError("The Kenney texture must retain sRGB for the Color sampler")

    mat = call(M + "create_material", {"folder_path": CFG["folder"], "asset_name": "M_IcePhasePowder"})
    settings = {"blendMode": "BLEND_Translucent", "shadingModel": "MSM_Unlit",
        "twoSided": True, "bDisableDepthTest": False, "materialDomain": "MD_Surface"}
    set_properties(mat, settings)
    usage_set = False
    usage_note = "Enable NiagaraSprites via MaterialEditingLibrary.set_base_material_usage; deprecated usage fields are not written"

    uv = node(mat, "TextureCoordinate", -1100, -100)
    age = node(mat, "DynamicParameter", -1100, 100, {
        "paramNames": ["Normalized Age", "Unused Y", "Unused Z", "Unused W"]})
    color = node(mat, "ParticleColor", -1100, 300)
    opacity = node(mat, "ScalarParameter", -1100, 500, {
        "parameterName": "Opacity", "defaultValue": .50, "group": "Cold Phase Powder"})
    world = node(mat, "WorldPosition", -1100, 700)
    camera = node(mat, "CameraPositionWS", -1100, 900)
    uv_code = r"""
        float a=saturate(Age);
        float growth=lerp(.60,1.05,a);
        float2 drift=float2(sin(a*4.5),cos(a*3.7)-1)*.02;
        // Clamp keeps the original transparent border rather than repeating
        // the texture when the young cloud occupies less than the sprite.
        return clamp((UV-.5)/growth+.5+drift,0,1);
    """
    coords = custom(mat, "One static Kenney puff with gentle growth and drift", uv_code,
        ["UV", "Age"], "CMOT_Float2", -760, -100)
    sample = node(mat, "TextureSampleParameter2D", -480, -100, {
        "parameterName": "PowderTexture", "texture": texture, "samplerType": "SAMPLERTYPE_Color",
        "group": "Cold Phase Powder"})
    tint_code = r"""
        float light=saturate(dot(S.rgb,float3(.2126,.7152,.0722)));
        float volume=saturate(S.a*.75+light*.25);
        float3 tint=lerp(float3(.56,.76,.88),float3(.93,.98,1),volume);
        return tint*Color.rgb*lerp(.85,1,light);
    """
    tint = custom(mat, "Soft white blue powder without additive glow", tint_code,
        ["S", "Color"], "CMOT_Float3", -100, -150)
    alpha_code = r"""
        float a=saturate(Age);
        float life=smoothstep(0,.07,a)*(1-smoothstep(.45,1,a));
        float nearCamera=smoothstep(35,95,length(P-C));
        // Preserve the source silhouette: its exact sampled alpha remains
        // a multiplicative mask. No threshold or opaque sprite background.
        return saturate(S.a*Color.a*Opacity*life*nearCamera);
    """
    alpha = custom(mat, "Original texture alpha with lifetime and near camera fades", alpha_code,
        ["S", "Color", "Age", "Opacity", "P", "C"], "CMOT_Float1", -100, 200)
    fade = node(mat, "DepthFade", 240, 200, {"fadeDistanceDefault": 24.0})
    edges = [(uv, "", coords, "UV"), (age, "", coords, "Age"),
        (coords, "", sample, "UVs"), (sample, "RGBA", tint, "S"), (color, "RGBA", tint, "Color"),
        (sample, "RGBA", alpha, "S"), (color, "RGBA", alpha, "Color"),
        (age, "", alpha, "Age"), (opacity, "", alpha, "Opacity"),
        (world, "", alpha, "P"), (camera, "", alpha, "C"), (alpha, "", fade, "Opacity")]
    for source, output_name, target, input_name in edges:
        connect(source, output_name, target, input_name)
    outputs = [(tint, "MP_EmissiveColor"), (fade, "MP_Opacity")]
    for expr, prop in outputs:
        call(M + "connect_to_output", {"expression": expr, "output_name": "", "material_property": prop})

    # Check real graph links and authored values before compiling or saving.
    for source, output_name, target, input_name in edges:
        links = call(M + "get_expression_inputs", {"material_or_function": mat, "expression": target})
        link = next((item for item in links if item["input_name"] == input_name), None)
        if not link or link["expression"] != source or (output_name and link["output_name"] != output_name):
            raise RuntimeError("Material graph link differs: " + str(link))
    for expr, prop in outputs:
        link = call(M + "get_property_input", {"material": mat, "material_property": prop})
        if link["expression"] != expr:
            raise RuntimeError("Material output is disconnected: " + prop)
    if properties(mat, list(settings)) != settings:
        raise RuntimeError("Material settings differ from the intended translucent shader")
    for expr, code in [(coords, uv_code), (tint, tint_code), (alpha, alpha_code)]:
        if properties(expr, ["code"])["code"] != code:
            raise RuntimeError("Authored HLSL differs from the source")
    sample_settings = properties(sample, ["texture", "samplerType"])
    if sample_settings["texture"] != texture or sample_settings["samplerType"] != "SAMPLERTYPE_Color":
        raise RuntimeError("The material does not sample the selected Kenney texture")
    call(M + "layout_expressions", {"material_or_function": mat})
    # Native MaterialTools.recompile waits for shader compilation and raises
    # on failure. Save explicitly only this material after that barrier.
    call(M + "recompile", {"material_or_function": mat})
    if not call(A + "save_assets", {"asset_paths": [CFG["material"]]}):
        raise RuntimeError("Could not save the new powder material")
    if call(A + "is_dirty", {"asset_path": CFG["material"]}):
        raise RuntimeError("The powder material remained dirty after save")
    if call(S + "get_current_level", {}) != level or call(E + "IsPIERunning", {}):
        raise RuntimeError("Editor context changed during authoring")
    return {"passed": True, "material": mat["refPath"], "texture": texture["refPath"],
        "created": True, "compiled": True, "graph_verified": True, "saved": True,
        "saved_assets": [CFG["material"]], "native_tools": True,
        "settings": settings, "expressions": len(call(M + "get_expressions", {"material_or_function": mat})),
        "depth_fade_cm": 24, "near_camera_fade_cm": [35, 95], "opacity": .50,
        "age_source": "DynamicParameter.x", "growth": [.60, 1.05], "drift_uv": .02,
        "source_alpha_preserved": True, "flipbook_frame_switching": False,
        "niagara_sprite_usage_set": usage_set,
        "niagara_sprite_usage_note": usage_note,
        "deferred": [] if usage_set else ["Set NiagaraSprites usage with the native MaterialEditingLibrary before runtime integration"],
        "editor_level": level, "pie_running": False, "build_performed": False}
'''


def enable_sprite_usage(invoke):
    """Bridge the one usage API missing from MCP; compile/save with MCP."""
    project_root = Path(__file__).resolve().parents[2]
    remote_helper = Path(__file__).with_name("remote_python.py")
    followup_path = REPORT_PATH.with_name("EnableSpriteUsage_" + uuid.uuid4().hex[:12] + ".py")
    source = SPRITE_USAGE_SCRIPT.replace("__PROJECT_ROOT__", repr(str(project_root)))
    source = source.replace("__MATERIAL_PATH__", repr(MATERIAL_PATH))
    followup_path.parent.mkdir(parents=True, exist_ok=True)
    followup_path.write_text(source, encoding="utf-8")
    # remote_python selects exactly one MessControl editor. The generated
    # script further checks the checkout, level, PIE and target package state.
    completed = subprocess.run([sys.executable, "-X", "utf8", str(remote_helper), str(followup_path)],
        cwd=str(project_root), check=True, capture_output=True, text=True, encoding="utf-8", timeout=90)
    result = json.loads(completed.stdout.strip())
    if result.get("success") is not True:
        raise RuntimeError("Native sprite usage follow-up failed: " + json.dumps(result))

    material_tools = "editor_toolset.toolsets.material.MaterialTools"
    asset_tools = "editor_toolset.toolsets.asset.AssetTools"
    scene_tools = "editor_toolset.toolsets.scene.SceneTools"
    editor_tools = "EditorToolset.EditorAppToolset"
    if invoke(editor_tools, "IsPIERunning", {}):
        raise RuntimeError("PIE started before the sprite usage shader barrier")
    current_level = invoke(scene_tools, "get_current_level", {})
    if not isinstance(current_level, str) or current_level.split(".")[0] != "/Game/Maps/L_Mouth":
        raise RuntimeError("Editor level changed before the sprite usage shader barrier")
    invoke(material_tools, "recompile", {"material_or_function": {
        "refPath": MATERIAL_PATH + ".M_IcePhasePowder"}})
    if not invoke(asset_tools, "save_assets", {"asset_paths": [MATERIAL_PATH]}):
        raise RuntimeError("Cannot save the material after enabling NiagaraSprites usage")
    if invoke(asset_tools, "is_dirty", {"asset_path": MATERIAL_PATH}):
        raise RuntimeError("The powder material remained dirty after sprite usage save")
    if invoke(editor_tools, "IsPIERunning", {}) or invoke(scene_tools, "get_current_level", {}) != current_level:
        raise RuntimeError("Editor context changed during the sprite usage follow-up")
    return {"niagara_sprite_usage_set": True,
        "niagara_sprite_usage_note": "Enabled with MaterialEditingLibrary.set_base_material_usage; has_material_usage asserted; MCP recompiled and saved only this material",
        "niagara_usage_compile_verified": True, "sprite_usage_script": str(followup_path),
        "deferred": [], "compiled": True, "saved": True, "passed": True}


def main():
    client = NativeMCP()

    def payload(response):
        if response.get("isError"):
            raise RuntimeError(response)
        result = json.loads(response["content"][0]["text"])
        if result.get("error"):
            raise RuntimeError(result)
        return result

    def invoke(toolset, name, arguments):
        return payload(client.tool("call_tool", {
            "toolset_name": toolset, "tool_name": name, "arguments": arguments})).get("returnValue")

    toolsets = ["EditorToolset.EditorAppToolset", "editor_toolset.toolsets.scene.SceneTools",
        "editor_toolset.toolsets.material.MaterialTools", "editor_toolset.toolsets.object.ObjectTools",
        "editor_toolset.toolsets.asset.AssetTools", "editor_toolset.toolsets.programmatic.ProgrammaticToolset"]
    report = {"passed": False, "material": MATERIAL_PATH, "texture": TEXTURE_PATH,
              "build_performed": False}
    try:
        # Discover the installed tools, not a guessed API, before authoring.
        for toolset in toolsets:
            payload(client.tool("describe_toolset", {"toolset_name": toolset}))
        programmatic = "editor_toolset.toolsets.programmatic.ProgrammaticToolset"
        invoke(programmatic, "get_execution_environment", {})
        config = {"folder": FOLDER, "material": MATERIAL_PATH, "texture": TEXTURE_PATH,
                  "map": "/Game/Maps/L_Mouth"}
        result = invoke(programmatic, "execute_tool_script", {
            "script": SCRIPT.replace("__CONFIG__", repr(config))})
        report = json.loads(result)
        if not report.get("passed"):
            raise RuntimeError("Native material authoring did not complete successfully")
        # Overall success includes the required Niagara permutation, not just
        # the preview shader. Do not publish a passing report before this ends.
        report["initial_material_saved"] = report.get("saved", False)
        report.update(passed=False, saved=False, niagara_usage_compile_verified=False)
        report.update(enable_sprite_usage(invoke))
    except Exception as exc:
        report["passed"] = False
        report["error"] = str(exc)
        raise
    finally:
        REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
        REPORT_PATH.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print("MC_KENNEY_SNOW_POWDER_MCP_PASS " + json.dumps(report))


if __name__ == "__main__":
    main()
