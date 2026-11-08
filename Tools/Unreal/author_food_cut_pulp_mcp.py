"""Run through Unreal's ProgrammaticToolset.execute_tool_script.

Authors the editable strawberry interior. Fixed source-mesh coordinates keep
the same interior on opposite faces and after further cuts of a fragment.
"""
import json

MATERIAL = "/Game/Gameplay/Food/CutMaterials/M_FoodCutPulp"
INSTANCE = "/Game/Gameplay/Food/CutMaterials/MI_FoodCut_Strawberry"
OWNER = "FoodCutPulp_20261107"

PULP_CODE = r"""
// Sample a continuous volume in the original mesh's coordinates.
float2 coreOffset = 0.022 * float2(sin(P.z * 4.0), sin(P.z * 3.1 + 1.0));
float radial = length(P.xy - coreOffset);
float alongBerry = smoothstep(-0.9, 0.9, P.z);
float coreRadius = max(0.02, CoreWidth * lerp(0.32, 1.35, alongBerry));
float coreDistance = radial / coreRadius;
float tipFade = smoothstep(-0.95, -0.42, P.z);
float core = (1.0 - smoothstep(0.78, 1.20, coreDistance)) * tipFade;
float coreHalo = (1.0 - smoothstep(1.1, 1.75, coreDistance)) * tipFade;

// Smooth value noise in eight neighboring volume cells. The polynomial hashes
// avoid the visible crosshatch of intersecting sine waves and need no textures.
float footprint = max(length(ddx(P)), length(ddy(P)));
float fineFade = 1.0 - smoothstep(0.025, 0.075, footprint);
float3 samplePosition = P * float3(18.0, 18.0, 8.0);
float3 cell = floor(samplePosition);
float3 blend = frac(samplePosition);
blend = blend * blend * (3.0 - 2.0 * blend);
float index = dot(cell, float3(1.0, 57.0, 113.0));
float4 lower = frac((index + float4(0.0, 1.0, 57.0, 58.0)) * 0.1031);
float4 upper = frac((index + float4(113.0, 114.0, 170.0, 171.0)) * 0.1031);
lower *= lower + 33.33;
upper *= upper + 33.33;
lower = frac(lower * (lower + lower));
upper = frac(upper * (upper + upper));
float2 row0 = lerp(lower.xz, lower.yw, blend.x);
float2 row1 = lerp(upper.xz, upper.yw, blend.x);
float value = lerp(lerp(row0.x, row0.y, blend.y), lerp(row1.x, row1.y, blend.y), blend.z);
float pulp = (value * 2.0 - 1.0) * fineFade;

// The stretched cells leave sparse, soft vascular marks within the flesh.
float fibers = smoothstep(0.68, 0.90, value);
fibers *= smoothstep(coreRadius * 1.1, coreRadius * 2.0, radial);
fibers *= 1.0 - smoothstep(0.72, 1.05, radial);
fibers *= fineFade;

float3 flesh = CutColor * (1.0 + PulpContrast * pulp);
flesh = lerp(flesh, CoreColor, coreHalo * 0.10);
flesh = lerp(flesh, CoreColor, fibers * FiberStrength);
float3 color = lerp(flesh, CoreColor * (1.0 + pulp * 0.035), core);
float roughness = clamp(WetRoughness + pulp * 0.045 + core * 0.07, 0.20, 0.65);
return float4(saturate(color), roughness);
"""


def material_tool(name, **arguments):
    return execute_tool("editor_toolset.toolsets.material.MaterialTools." + name, json.dumps(arguments))


def asset_tool(name, **arguments):
    return execute_tool("editor_toolset.toolsets.asset.AssetTools." + name, json.dumps(arguments))


def get_properties(instance, properties):
    result = execute_tool("editor_toolset.toolsets.object.ObjectTools.get_properties",
        json.dumps(dict(instance=instance, properties=properties)))
    return json.loads(result["returnValue"])


def set_properties(instance, **values):
    result = execute_tool("editor_toolset.toolsets.object.ObjectTools.set_properties",
        json.dumps(dict(instance=instance, values=json.dumps(values))))
    if not result["returnValue"]:
        raise RuntimeError("Material property write was rejected")


def custom_inputs(material, expression, names):
    result = execute_tool("MessControlEditor.MCVFXMaterialToolset.SetOwnedCustomMaterialInputs",
        json.dumps(dict(material=material, expression=expression, inputNames=names,
            ownerMetadataKey="MCAuthoringOwner", expectedOwnerValue=OWNER)))
    value = json.loads(result["returnValue"])
    if not value.get("complete"):
        raise RuntimeError(value)


def set_instance_parent(instance, parent):
    return execute_tool("editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_parent",
        json.dumps(dict(instance=instance, parent=parent)))


def set_instance_color(instance, name, color):
    return execute_tool("editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_vector_parameter",
        json.dumps(dict(instance=instance, name=name, value=color)))


def run():
    if asset_tool("exists", path=MATERIAL)["returnValue"]:
        tags = asset_tool("get_metadata_tags", asset_path=MATERIAL)["returnValue"]
        if tags.get("MCAuthoringOwner") != OWNER:
            raise RuntimeError("The existing material belongs to a different authoring task")
        material = {"refPath": MATERIAL + ".M_FoodCutPulp"}
    else:
        material = material_tool("create_material", folder_path=MATERIAL.rsplit("/", 1)[0],
            asset_name="M_FoodCutPulp")["returnValue"]
        asset_tool("update_metadata_tags", asset_path=MATERIAL, set_tags={"MCAuthoringOwner": OWNER})

    existing = material_tool("get_expressions", material_or_function=material)["returnValue"]
    by_label = {get_properties(node, ["desc"])["desc"]: node for node in existing}

    def node(label, kind, x, y, **values):
        description = "FoodCutPulp / " + label
        expression = by_label.get(description)
        if expression is None:
            expression = material_tool("add_expression", material_or_function=material,
                expression_class={"refPath": "/Script/Engine.MaterialExpression" + kind}, x=x, y=y)["returnValue"]
        set_properties(expression, desc=description, **values)
        return expression

    def connect(source, target, pin):
        material_tool("connect_expressions", from_expression=source, from_output_name="",
            to_expression=target, to_input_name=pin)

    def vector(label, name, rgb, x, y, group):
        return node(label, "VectorParameter", x, y, parameterName=name, group=group,
            defaultValue=dict(r=rgb[0], g=rgb[1], b=rgb[2], a=1.0))

    def scalar(name, value, x, y):
        return node(name, "ScalarParameter", x, y, parameterName=name,
            defaultValue=value, group="Pulp", sliderMin=0.0, sliderMax=1.0)

    world = node("World position", "WorldPosition", -1500, -400,
        worldPositionShaderOffset="WPT_ExcludeAllShaderOffsets")
    local = node("Original local position", "TransformPosition", -1260, -400,
        transformSourceType="TRANSFORMPOSSOURCE_World", transformType="TRANSFORMPOSSOURCE_Local")
    center = vector("Original mesh center", "PulpCenter", (0, 0, 4.15), -1500, -180, "Source mesh")
    inverse_extent = vector("Original inverse extent", "PulpInvExtent", (0.32, 0.32, 0.24),
        -1260, 60, "Source mesh")
    subtract = node("Center local position", "Subtract", -1000, -400)
    normalized = node("Normalize source mesh coordinates", "Multiply", -760, -400)
    connect(world, local, "")
    connect(local, subtract, "A")
    connect(center, subtract, "B")
    connect(subtract, normalized, "A")
    connect(inverse_extent, normalized, "B")

    surface = node("Strawberry pulp volume", "Custom", -230, -230,
        code=PULP_CODE, outputType="CMOT_Float4", description="Pulp RGB / wet roughness A")
    inputs = {
        "P": normalized,
        "CutColor": vector("Flesh color", "CutColor", (0.62, 0.045, 0.065), -760, -180, "Pulp"),
        "CoreColor": vector("Core color", "CoreColor", (0.90, 0.66, 0.53), -760, 40, "Pulp"),
        "CoreWidth": scalar("CoreWidth", 0.24, -760, 250),
        "PulpContrast": scalar("PulpContrast", 0.065, -530, 250),
        "FiberStrength": scalar("FiberStrength", 0.045, -760, 450),
        "WetRoughness": scalar("WetRoughness", 0.32, -530, 450),
    }
    custom_inputs(material, surface, list(inputs))
    for name, expression in inputs.items():
        connect(expression, surface, name)

    rgb = node("Flesh RGB", "ComponentMask", 110, -230, r=True, g=True, b=True, a=False)
    roughness = node("Wet roughness", "ComponentMask", 110, 10, r=False, g=False, b=False, a=True)
    specular = scalar("PulpSpecular", 0.42, 110, 210)
    connect(surface, rgb, "")
    connect(surface, roughness, "")
    for expression, output in [(rgb, "MP_BaseColor"), (roughness, "MP_Roughness"), (specular, "MP_Specular")]:
        material_tool("connect_to_output", expression=expression, output_name="", material_property=output)
    material_tool("recompile", material_or_function=material)

    instance = {"refPath": INSTANCE + ".MI_FoodCut_Strawberry"}
    set_instance_parent(instance, material)
    set_instance_color(instance, "CutColor", dict(r=0.62, g=0.045, b=0.065, a=1))
    if not asset_tool("save_assets", asset_paths=[MATERIAL, INSTANCE])["returnValue"]:
        raise RuntimeError("Saving the cut materials failed")
    return {"material": material, "instance": instance, "custom_expression": surface,
        "local_coordinates": "Fixed original mesh coordinates", "authored_texture_samples": 0}
