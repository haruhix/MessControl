"""Author task-owned procedural boss UI materials in an idle Unreal editor.

The frame reads a cached distance field of an expanded icon recess. Alpha seed,
elliptical JFA, recess seed and Euclidean JFA use this same material; no extra
materials or frame PNGs exist.
Rectangles are (x, y, width, height) in the same logical pixels as PanelSize.
BodyRect denotes the OUTER metal allocation; its recess is inset by 8 pixels.
Give the image enough padding for icon dilation/shadow (12 px recommended).
Run with runpy inside Unreal; this file does not launch Unreal or a build.
"""
from pathlib import Path
import json

import unreal as u


BASE = '/Game/Gameplay/UI/Materials'
FRAME_MARKER = 'MessControl task-owned procedural boss frame v1'
SHIELD_MARKER = 'MessControl task-owned procedural boss shield v1'

# UE Custom TextureObject inputs expose <InputName>Sampler automatically.
# All color inputs must use their VectorParameter RGBA output, not default RGB.
FRAME_CODE = r'''
// One material also authors its cached expanded-recess distance field.
// Seed/jump outputs are raw UV data, opaque and independent of all theme colors.
float2 fieldSize = max(FieldSize.xy, float2(1.0, 1.0));
float maskPadding = max(MaskPadding, 0.0);
[branch] if (Operation > 0.5)
{
    [branch] if (Operation < 1.5)
    {
        float2 fieldPoint = UV * fieldSize;
        float2 sourceSize = max(fieldSize - maskPadding * 2.0, float2(1.0, 1.0));
        float2 sourceUV = (fieldPoint - maskPadding) / sourceSize;
        float inBounds = all(sourceUV >= 0.0) && all(sourceUV <= 1.0) ? 1.0 : 0.0;
        float alpha = Texture2DSampleLevel(IconMask, IconMaskSampler, saturate(sourceUV), 0).a * inBounds;
        return alpha > 0.5 ? float4(UV, 1.0, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
    }
    [branch] if (Operation > 2.5)
    {
        // The first JFA found nearest opaque alpha in the ellipse metric.
        // Seed every pixel of that expanded recess, not a normal-dependent
        // offset of its Voronoi field. A second Euclidean JFA bevels it evenly.
        float3 seed = Texture2DSampleLevel(DistanceMask, DistanceMaskSampler, UV, 0).rgb;
        float2 metric = max(IconPadding.xy, float2(0.5, 0.5));
        float2 delta = (seed.xy - UV) * fieldSize / metric;
        return seed.z > 0.5 && dot(delta, delta) <= 1.0
            ? float4(UV, 1.0, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
    }
    float bestSquared = 1e20;
    float2 bestSeed = float2(0.0, 0.0);
    float valid = 0.0;
    float stepUV = max(FieldStep, 1.0) / max(FieldResolution, 1.0);
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float2 sampleUV = UV + float2(x, y) * stepUV;
            [branch] if (all(sampleUV >= 0.0) && all(sampleUV <= 1.0))
            {
                float3 seed = Texture2DSampleLevel(DistanceMask, DistanceMaskSampler, sampleUV, 0).rgb;
                float2 metric = MetricMode > 0.5 ? max(IconPadding.xy, float2(0.5, 0.5)) : float2(1.0, 1.0);
                float2 delta = (seed.xy - UV) * fieldSize / metric;
                float distanceSquared = dot(delta, delta);
                if (seed.z > 0.5 && distanceSquared < bestSquared)
                {
                    bestSquared = distanceSquared;
                    bestSeed = seed.xy;
                    valid = 1.0;
                }
            }
        }
    }
    return float4(bestSeed, valid, 1.0);
}

float2 panel = max(PanelSize.xy, float2(1.0, 1.0));
float2 p = UV * panel;
float aa = max(0.65, max(length(ddx(p)), length(ddy(p))));
float2 bodyHalf = max(BodyRect.zw * 0.5, float2(1.0, 1.0));
float2 bodyCenter = BodyRect.xy + bodyHalf;
float bodyRadius = clamp(BodyCornerRadius, 0.0, min(bodyHalf.x, bodyHalf.y));
float2 bodyQ = abs(p - bodyCenter) - (bodyHalf - bodyRadius);
float bodyOuterD = length(max(bodyQ, 0.0)) + min(max(bodyQ.x, bodyQ.y), 0.0) - bodyRadius;
float bodyInnerD = bodyOuterD + 8.0;
float2 bodySign = sign(p - bodyCenter);
float2 bodyNormal = bodyQ.x > bodyQ.y ? float2(bodySign.x, 0.0) : float2(0.0, bodySign.y);
if (max(bodyQ.x, bodyQ.y) > 0.0)
    bodyNormal = normalize(max(bodyQ, 0.0) * bodySign + float2(1e-6, 0.0));
float2 iconSize = max(IconRect.zw, float2(1.0, 1.0));
float2 iconCenter = IconRect.xy + iconSize * 0.5;
float2 iconPadding = clamp(IconPadding.xy, 0.0, 64.0);

// Analytic recess bridge prevents narrow/tall silhouettes disconnecting.
float bridgeLeft = min(iconCenter.x, BodyRect.x + bodyRadius);
float bridgeRight = max(iconCenter.x, BodyRect.x + bodyRadius);
float2 bridgeHalf = float2(max((bridgeRight - bridgeLeft) * 0.5, 0.5), max((BodyRect.w - 16.0) * 0.325, 1.0));
float2 bridgeCenter = float2((bridgeLeft + bridgeRight) * 0.5, bodyCenter.y);
float bridgeRadius = min(8.0, min(bridgeHalf.x, bridgeHalf.y));
float2 bridgeQ = abs(p - bridgeCenter) - (bridgeHalf - bridgeRadius);
float bridgeD = length(max(bridgeQ, 0.0)) + min(max(bridgeQ.x, bridgeQ.y), 0.0) - bridgeRadius;
float2 bridgeSign = sign(p - bridgeCenter);
float2 bridgeNormal = bridgeQ.x > bridgeQ.y ? float2(bridgeSign.x, 0.0) : float2(0.0, bridgeSign.y);
if (max(bridgeQ.x, bridgeQ.y) > 0.0)
    bridgeNormal = normalize(max(bridgeQ, 0.0) * bridgeSign + float2(1e-6, 0.0));
float smoothK = 2.0;
float bridgeWeight = saturate(0.5 + 0.5 * (bridgeD - bodyInnerD) / smoothK);
float connectedD = lerp(bridgeD, bodyInnerD, bridgeWeight) - smoothK * bridgeWeight * (1.0 - bridgeWeight);
float2 connectedNormal = normalize(lerp(bridgeNormal, bodyNormal, bridgeWeight) + float2(1e-6, 0.0));

float iconD = 10000.0;
float2 iconNormal = float2(-1.0, 0.0);
[branch] if (HasIcon > 0.5 && DistanceReady > 0.5)
{
    float2 logicalFieldSize = iconSize + maskPadding * 2.0;
    float2 fieldUV = (p - IconRect.xy + maskPadding) / logicalFieldSize;
    [branch] if (all(fieldUV >= 0.0) && all(fieldUV <= 1.0))
    {
        float3 seed = Texture2DSampleLevel(DistanceMask, DistanceMaskSampler, fieldUV, 0).rgb;
        if (seed.z > 0.5)
        {
            float2 nearest = seed.xy * logicalFieldSize - maskPadding + IconRect.xy;
            float2 delta = p - nearest;
            float distance = length(delta);
            iconNormal = distance > 1e-4 ? delta / distance : normalize(p - iconCenter + float2(1e-6, 0.0));
            // The cached seeds already cover the entire expanded recess.
            // One AA-width guard makes its interior fully opaque; the bevel
            // uses Euclidean distance, with no normal-dependent offset/bulges.
            iconD = max(distance, 0.0) - aa;
        }
    }
}
else
{
    // The cached field is not ready or no icon exists: stable procedural lobe.
    float2 halfLobe = iconSize * 0.5 + iconPadding;
    float lobeRadius = min(halfLobe.x, halfLobe.y) * 0.85;
    float2 lobeQ = abs(p - iconCenter) - (halfLobe - lobeRadius);
    iconD = length(max(lobeQ, 0.0)) + min(max(lobeQ.x, lobeQ.y), 0.0) - lobeRadius;
    float2 lobeSign = sign(p - iconCenter);
    iconNormal = lobeQ.x > lobeQ.y ? float2(lobeSign.x, 0.0) : float2(0.0, lobeSign.y);
    if (max(lobeQ.x, lobeQ.y) > 0.0)
        iconNormal = normalize(max(lobeQ, 0.0) * lobeSign + float2(1e-6, 0.0));
}
float iconWeight = saturate(0.5 + 0.5 * (iconD - connectedD) / smoothK);
float d = lerp(iconD, connectedD, iconWeight) - smoothK * iconWeight * (1.0 - iconWeight);
float2 normal = normalize(lerp(iconNormal, connectedNormal, iconWeight) + float2(1e-6, 0.0));
float u0 = saturate(0.5 - d / aa);
float u1 = saturate(0.5 + (1.0 - d) / aa);
float u5 = saturate(0.5 + (5.0 - d) / aa);
float u6 = saturate(0.5 + (6.0 - d) / aa);
float u8 = saturate(0.5 + (8.0 - d) / aa);
float shadowD = d - normal.y * 1.5;
float shadowMask = saturate(saturate(0.5 + (10.0 - shadowD) / aa) - u8);
float lipMask = saturate(u8 - u6);
float bevelMask = saturate(u6 - u1);
float gasketMask = saturate(u1 - u0);
float topLight = saturate(dot(normal, normalize(float2(-0.45, -0.90))));
float3 metal = max(RimColor.rgb, 0.0) * (0.28 + topLight * 0.92);
float highlightMask = saturate(u6 - u5) * topLight;

// Disjoint masks preserve independently editable RGBA for every theme role.
float aShadow = shadowMask * 0.32 * max(saturate(RimColor.a), saturate(SurfaceColor.a));
float aLip = lipMask * saturate(RimColor.a);
float aBevel = bevelMask * saturate(RimColor.a);
float aGasket = gasketMask * saturate(SurfaceColor.a);
float aSurface = u0 * saturate(SurfaceColor.a);
float3 rgb = max(RimColor.rgb, 0.0) * 0.12 * aLip + metal * aBevel
    + max(SurfaceColor.rgb, 0.0) * (0.30 * aGasket + aSurface);
float alpha = saturate(aShadow + aLip + aBevel + aGasket + aSurface);
float aHighlight = highlightMask * saturate(HighlightColor.a);
rgb = max(HighlightColor.rgb, 0.0) * aHighlight + rgb * (1.0 - aHighlight);
alpha = aHighlight + alpha * (1.0 - aHighlight);
return float4(alpha > 1e-5 ? rgb / alpha : float3(0.0, 0.0, 0.0), alpha * saturate(FrameOpacity));
'''

SHIELD_CODE = r'''
// Work in actual pixel distance so the cyan stroke does not stretch with UVs.
float2 uvFootprint = max(float2(length(float2(ddx(UV.x), ddy(UV.x))),
    length(float2(ddx(UV.y), ddy(UV.y)))), float2(1e-5, 1e-5));
float2 pixelsPerUV = min(1.0 / uvFootprint, float2(8192.0, 8192.0));
float2 p = (UV - 0.5) * pixelsPerUV;
static const float2 shape[6] = {
    float2(-0.40,-0.32), float2(0.0,-0.46), float2(0.40,-0.32),
    float2(0.40,0.10), float2(0.0,0.48), float2(-0.40,0.10)
};
float distanceSquared = 1e20;
bool inside = false;
[unroll] for (int i = 0; i < 6; ++i)
{
    float2 a = shape[i] * pixelsPerUV;
    float2 b = shape[(i + 1) % 6] * pixelsPerUV;
    float2 edge = b - a;
    float2 toPoint = p - a;
    float2 closest = toPoint - edge * saturate(dot(toPoint, edge) / max(dot(edge, edge), 1e-6));
    distanceSquared = min(distanceSquared, dot(closest, closest));
    float dy = b.y - a.y;
    float safeDy = dy >= 0.0 ? max(dy, 1e-5) : min(dy, -1e-5);
    bool crosses = (a.y > p.y) != (b.y > p.y);
    if (crosses && p.x < a.x + (p.y - a.y) * edge.x / safeDy) inside = !inside;
}
float sd = sqrt(max(distanceSquared, 0.0)) * (inside ? -1.0 : 1.0);
float outerLine = saturate(0.5 + 1.25 - abs(sd));
float innerLine = saturate(0.5 + 0.55 - abs(sd + 3.1)) * 0.55;
float coverage = max(outerLine, innerLine);
return float4(max(ThemeColor.rgb, 0.0), coverage * saturate(ThemeColor.a) * saturate(ThemeOpacity));
'''


def linear_color(r, g, b, a=1.0):
    def decode(value):
        value /= 255.0
        return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4
    return u.LinearColor(decode(r), decode(g), decode(b), a)


def assert_owned_or_new(path, marker):
    if not u.EditorAssetLibrary.does_asset_exist(path):
        return None
    material = u.load_asset(path)
    if not isinstance(material, u.Material):
        raise RuntimeError('Preserved unexpected asset at ' + path)
    expressions = u.MaterialEditingLibrary.get_material_expressions(material)
    if not any(isinstance(node, u.MaterialExpressionCustom)
               and node.get_editor_property('description') == marker for node in expressions):
        raise RuntimeError('Preserved material without this task ownership marker: ' + path)
    return material


def author_material(name, marker, code, vectors, scalars, icon_texture=False):
    path = BASE + '/' + name
    material = assert_owned_or_new(path, marker)
    edit = u.MaterialEditingLibrary
    if material:
        edit.delete_all_material_expressions(material)
    else:
        material = u.AssetToolsHelpers.get_asset_tools().create_asset(
            name, BASE, u.Material, u.MaterialFactoryNew())
    assert material, path
    material.set_editor_property('material_domain', u.MaterialDomain.MD_UI)
    material.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
    nodes = []
    uv = edit.create_material_expression(material, u.MaterialExpressionTextureCoordinate, -900, 0)
    nodes.append(('UV', uv, ''))
    if icon_texture:
        for index, parameter in enumerate(('IconMask', 'DistanceMask')):
            texture = edit.create_material_expression(material, u.MaterialExpressionTextureObjectParameter, -1200, 120 + index * 140)
            texture.set_editor_property('parameter_name', parameter)
            texture.set_editor_property('texture', u.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
            nodes.append((parameter, texture, ''))
    for index, (parameter, value) in enumerate(vectors.items()):
        node = edit.create_material_expression(material, u.MaterialExpressionVectorParameter, -900, 250 + index * 100)
        node.set_editor_property('parameter_name', parameter)
        node.set_editor_property('default_value', value)
        nodes.append((parameter, node, 'RGBA'))
    for index, (parameter, value) in enumerate(scalars.items()):
        node = edit.create_material_expression(material, u.MaterialExpressionScalarParameter, -600, 250 + index * 100)
        node.set_editor_property('parameter_name', parameter)
        node.set_editor_property('default_value', value)
        nodes.append((parameter, node, ''))
    custom = edit.create_material_expression(material, u.MaterialExpressionCustom, -250, 0)
    inputs = []
    for parameter, _, _ in nodes:
        item = u.CustomInput()
        item.set_editor_property('input_name', parameter)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)
    custom.set_editor_property('output_type', u.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property('description', marker)
    custom.set_editor_property('code', code)
    for parameter, node, pin in nodes:
        assert edit.connect_material_expressions(node, pin, custom, parameter), parameter
    for channels, output, y in (
        ((True, True, True, False), u.MaterialProperty.MP_EMISSIVE_COLOR, 0),
        ((False, False, False, True), u.MaterialProperty.MP_OPACITY, 180),
    ):
        mask = edit.create_material_expression(material, u.MaterialExpressionComponentMask, 50, y)
        for channel, enabled in zip(('r', 'g', 'b', 'a'), channels):
            mask.set_editor_property(channel, enabled)
        assert edit.connect_material_expressions(custom, '', mask, '')
        assert edit.connect_material_property(mask, '', output)
    edit.recompile_material(material)
    assert u.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False), path
    return {'path': path, 'marker': marker, 'parameters': [x[0] for x in nodes if x[0] != 'UV']}


def main():
    root = Path(u.Paths.project_dir()).resolve()
    if root.drive.upper() != 'E:':
        raise RuntimeError('This authoring task requires the project and outputs on E:')
    if u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError('Stop PIE before authoring procedural boss materials')
    # Validate both existing assets before mutating either graph.
    assert_owned_or_new(BASE + '/M_UI_BossFrameProcedural', FRAME_MARKER)
    assert_owned_or_new(BASE + '/M_UI_BossShieldProcedural', SHIELD_MARKER)
    report = [author_material(
        'M_UI_BossFrameProcedural', FRAME_MARKER, FRAME_CODE,
        {'PanelSize': u.LinearColor(816, 108, 0, 0),
         'IconRect': u.LinearColor(8.03, 17.74, 73.45, 73.45),
         'BodyRect': u.LinearColor(74, 22, 742, 64),
         'IconPadding': u.LinearColor(12, 6, 0, 0),
         'FieldSize': u.LinearColor(121.45, 121.45, 0, 0),
         'SurfaceColor': linear_color(10, 11, 16),
         'RimColor': linear_color(72, 76, 89),
         'HighlightColor': linear_color(112, 122, 138)},
        {'FrameOpacity': 1.0, 'HasIcon': 0.0, 'BodyCornerRadius': 15.0,
         'Operation': 0.0, 'FieldStep': 128.0, 'FieldResolution': 256.0,
         'DistanceReady': 0.0, 'MaskPadding': 24.0, 'MetricMode': 0.0}, icon_texture=True),
        author_material('M_UI_BossShieldProcedural', SHIELD_MARKER, SHIELD_CODE,
                        {'ThemeColor': linear_color(0, 239, 247)}, {'ThemeOpacity': 1.0})]
    output = root / 'Saved/GameplayUI/procedural_boss_material_authoring.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({'materials': report, 'validation': 'Recompile requested; inspect shader diagnostics and native-size previews before acceptance.'}, indent=2), encoding='utf-8')
    u.log('MC_PROCEDURAL_BOSS_MATERIALS_AUTHORED ' + str(output))


if __name__ == '__main__':
    main()
