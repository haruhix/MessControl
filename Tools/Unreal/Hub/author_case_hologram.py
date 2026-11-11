"""Build the world-space widget material for the Hub slot machine in Unreal Editor."""

from datetime import datetime, timezone
from pathlib import Path
import shutil

import unreal as u


DESTINATION = '/Game/Gameplay/Hub/Economy'
NAME = 'M_CaseHologramProjection'
PACKAGE = f'{DESTINATION}/{NAME}'
OWNER_KEY = 'MCAssetOwner'
OWNER = 'CaseHologramProjection'
ROOT = Path(u.Paths.project_dir()).resolve()
assets = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary

assert ROOT.drive.upper() == 'E:', 'Case hologram authoring requires the E: project checkout.'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE before authoring.'
dirty = {package.get_path_name() for package in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
assert PACKAGE not in dirty, f'Unsaved package preserved: {PACKAGE}'
white = u.load_asset('/Engine/EngineResources/WhiteSquareTexture')
assert isinstance(white, u.Texture2D), 'Missing fallback for SlateUI.'

if assets.does_asset_exist(PACKAGE):
    material = assets.load_asset(PACKAGE)
    assert isinstance(material, u.Material) and assets.get_metadata_tag(material, OWNER_KEY) == OWNER, PACKAGE
    stamp = datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
    backup = Path('E:/CodexWork/MessControl/CaseHologramAuthoring') / stamp
    backup.mkdir(parents=True)
    for extension in ('.uasset', '.uexp', '.ubulk'):
        source = ROOT / 'Content/Gameplay/Hub/Economy' / f'{NAME}{extension}'
        if source.is_file():
            target = backup / source.name
            shutil.copy2(source, target)
            assert source.read_bytes() == target.read_bytes()
    for expression in list(edit.get_material_expressions(material)):
        edit.delete_material_expression(material, expression)
else:
    material = u.AssetToolsHelpers.get_asset_tools().create_asset(NAME, DESTINATION, u.Material, u.MaterialFactoryNew())
    assert isinstance(material, u.Material), PACKAGE

material.set_editor_properties(dict(
    material_domain=u.MaterialDomain.MD_SURFACE,
    blend_mode=u.BlendMode.BLEND_TRANSLUCENT,
    shading_model=u.MaterialShadingModel.MSM_UNLIT,
    two_sided=False,
    disable_depth_test=False,
    enable_responsive_aa=True,
))


def node(node_class, x, y, **properties):
    expression = edit.create_material_expression(material, node_class, x, y)
    assert expression, node_class
    if properties:
        expression.set_editor_properties(properties)
    return expression


def scalar(name, default, y):
    return node(u.MaterialExpressionScalarParameter, -660, y, parameter_name=name, default_value=default)


uv = node(u.MaterialExpressionTextureCoordinate, -660, -500)
slate = node(u.MaterialExpressionTextureObjectParameter, -660, -390,
             parameter_name='SlateUI', texture=white,
             sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
clock = node(u.MaterialExpressionTime, -660, -250)
background = node(u.MaterialExpressionVectorParameter, -660, -130,
                  parameter_name='BackColor', default_value=u.LinearColor(0., 0., 0., 0.))
tint = node(u.MaterialExpressionVectorParameter, -660, 0,
            parameter_name='TintColorAndOpacity', default_value=u.LinearColor(1., 1., 1., 1.))

code = r'''
float2 p = UV;
float4 src = Texture2DSample(Slate, SlateSampler, p);
// Slate renders against a clear color. Restore straight RGB for translucent blending.
float coverage = saturate(src.a);
float3 color = max(0.0, (src.rgb - ClearColor.rgb * (1.0 - coverage)) / max(coverage, 0.045));
color = lerp(src.rgb, color, saturate(TextureOpacity));

float distanceToEdge = min(min(p.x, 1.0 - p.x), min(p.y, 1.0 - p.y));
float fringe = 1.0 - smoothstep(0.025, 0.15, distanceToEdge);
float verticalFringe = 1.0 - smoothstep(0.065, 0.22, min(p.y, 1.0 - p.y));
float cycle = frac(max(Clock, 0.0) * 0.41);
float tracking = (1.0 - smoothstep(0.07, 0.115, cycle)) * verticalFringe;
float stripe = 1.0 - smoothstep(0.012, 0.032, abs(p.y - (0.095 + cycle * 0.63)));
stripe += 0.55 * (1.0 - smoothstep(0.012, 0.028, abs(p.y - (0.915 - cycle * 0.49))));
tracking *= saturate(stripe);

// The rail and its marker keep their UVs. Only the faint fringe separates in RGB.
float ghostOffset = saturate(GhostStrength) * (0.005 + tracking * 0.012);
float4 leftGhost = Texture2DSample(Slate, SlateSampler, saturate(p - float2(ghostOffset, 0.0)));
float4 rightGhost = Texture2DSample(Slate, SlateSampler, saturate(p + float2(ghostOffset, 0.0)));
float silhouette = max(0.0, max(leftGhost.a, rightGhost.a) - coverage);
float ghost = saturate(GhostStrength) * fringe;
float3 chroma = float3(rightGhost.r * 0.20, leftGhost.g * 0.38, leftGhost.b * 0.58);
color += ghost * (chroma + silhouette * float3(0.08, 0.31, 0.58));

float scanPhase = p.y * 38.0 + Clock * 1.18;
float scanFilter = 1.0 - smoothstep(0.22, 0.65, fwidth(scanPhase));
float scan = 0.5 + 0.5 * cos(scanPhase * 6.2831853);
float scanDim = saturate(ScanlineStrength) * scan * scanFilter;
float sweepY = frac(Clock * 0.31) * 1.16 - 0.08;
float sweep = (1.0 - smoothstep(0.024, 0.085, abs(p.y - sweepY))) * saturate(ScanlineStrength) * 0.26;
float2 noiseCell = floor(p * float2(170.0, 81.0)) + floor(Clock * 17.0);
float noise = frac(sin(dot(noiseCell, float2(12.9898, 78.233))) * 43758.5453) - 0.5;
float luma = dot(saturate(color), float3(0.2126, 0.7152, 0.0722));
float lightLine = smoothstep(0.42, 1.0, luma);
color = color * float3(0.86, 1.04, 1.13) * (1.0 - scanDim);
color += lightLine * float3(0.05, 0.16, 0.23);
color += (noise * 0.022 * (0.28 + fringe) + tracking * 0.072 + sweep) * float3(0.27, 0.66, 1.0);

float alpha = lerp(1.0, coverage, saturate(TextureOpacity));
alpha = max(alpha, silhouette * ghost * 0.35);
float centerPanel = smoothstep(0.015, 0.055, distanceToEdge);
alpha *= lerp(lerp(0.68, 0.85, centerPanel), 0.98, lightLine) * saturate(ProjectionOpacity) * saturate(Tint.a);
alpha *= (1.0 - 0.4 * scanDim) * (1.0 - tracking * 0.16);
return float4(max(color, 0.0) * Tint.rgb, saturate(alpha));
'''

projection = node(u.MaterialExpressionCustom, -210, 0, code=code,
                  description='World hologram with filtered scanlines and rare edge tracking',
                  output_type=u.CustomMaterialOutputType.CMOT_FLOAT4)
inputs = {
    'UV': (uv, ''),
    'Slate': (slate, ''),
    'Clock': (clock, ''),
    'ClearColor': (background, 'RGBA'),
    'Tint': (tint, 'RGBA'),
    'TextureOpacity': (scalar('OpacityFromTexture', 1., 130), ''),
    'ProjectionOpacity': (scalar('ProjectionOpacity', .94, 255), ''),
    'ScanlineStrength': (scalar('ScanlineStrength', .15, 380), ''),
    'GhostStrength': (scalar('GhostStrength', .32, 505), ''),
}
pins = []
for name in inputs:
    pin = u.CustomInput()
    pin.set_editor_property('input_name', name)
    pins.append(pin)
projection.set_editor_property('inputs', pins)
for name, (expression, output) in inputs.items():
    assert edit.connect_material_expressions(expression, output, projection, name), name

rgb = node(u.MaterialExpressionComponentMask, 130, -70, r=True, g=True, b=True, a=False)
alpha = node(u.MaterialExpressionComponentMask, 130, 100, r=False, g=False, b=False, a=True)
for expression, output in ((rgb, u.MaterialProperty.MP_EMISSIVE_COLOR), (alpha, u.MaterialProperty.MP_OPACITY)):
    input_name = str(edit.get_material_expression_input_names(expression)[0])
    assert edit.connect_material_expressions(projection, '', expression, input_name), input_name
    assert edit.connect_material_property(expression, '', output), output

assert not edit.recompile_material(material), 'Case hologram material failed to compile.'
assets.set_metadata_tag(material, OWNER_KEY, OWNER)
assert assets.save_loaded_asset(material, only_if_is_dirty=False), PACKAGE
u.log(f'Authored {material.get_path_name()}')
