"""Author task-owned Phase 1 UI surfaces and procedural glyphs in an idle editor.

Run via runpy inside the existing editor. This script launches no editor/build,
imports no textures, and never edits boss materials, fonts, widgets or the theme.

Shared MID contract (all names case-sensitive):
  SurfaceColor, EdgeColor, HighlightColor: explicit LINEAR float4 RGBA.
  PanelSize: vector4, XY = allocated logical-pixel dimensions, ZW unused.
  Radius, BorderWidth: logical pixels. Glow: 0..4, not a pixel distance.
  Shape: 0 rounded rectangle, 1 rounded diamond. Segments: 0 off, otherwise count.
Fill adds Fill=1 (0..1) and Dim=.14 (unfilled track RGB factor).
Tool adds Selected=0 and Focus=0 (0..1). All materials expose all scalars so a
common C++ setter may be used; irrelevant parameters simply have no effect.
The Fill material can be used as a ProgressBar brush with Fill=1, or draw its own
rounded end cap when a MID receives Fill directly. Do not apply both percentages.

All geometry is analytic. Leave transparent space for glow inside the allocation;
the material reserves up to 5 logical pixels automatically. Update PanelSize when
the allocated widget size changes to preserve corner/rim/gap thickness.
"""
from pathlib import Path
import json

import unreal as u


BASE = '/Game/Gameplay/UI/Materials'
MARKER_PREFIX = 'MessControl Phase1 task-owned procedural UI surface v1: '
NAMES = ('M_UI_Phase1Panel', 'M_UI_Phase1Fill', 'M_UI_Phase1Tool', 'M_UI_Phase1Glyph')

GLYPH_CODE = r'''
struct ShapeMath {
float capsule(float2 p,float2 a,float2 b,float r) {float2 v=b-a;return length(p-a-v*saturate(dot(p-a,v)/max(dot(v,v),.001)))-r;}
float box(float2 p,float2 c,float2 h,float r) {float2 q=abs(p-c)-h+r;return length(max(q,0))+min(max(q.x,q.y),0)-r;}
float TriSDF(float2 p,float2 a,float2 b,float2 c) {
float2 e0=b-a,e1=c-b,e2=a-c;float2 v0=p-a,v1=p-b,v2=p-c;
float2 q0=v0-e0*saturate(dot(v0,e0)/dot(e0,e0));float2 q1=v1-e1*saturate(dot(v1,e1)/dot(e1,e1));float2 q2=v2-e2*saturate(dot(v2,e2)/dot(e2,e2));
float s=sign(e0.x*e2.y-e0.y*e2.x);float2 d=min(min(float2(dot(q0,q0),s*(v0.x*e0.y-v0.y*e0.x)),float2(dot(q1,q1),s*(v1.x*e1.y-v1.y*e1.x))),float2(dot(q2,q2),s*(v2.x*e2.y-v2.y*e2.x)));return -sqrt(d.x)*sign(d.y);
}
};
ShapeMath S;
float smallest=max(1,min(PanelSize.x,PanelSize.y));
float2 p=(UV-.5)*PanelSize.xy/smallest*80+40;
float aa=max(.7,80/smallest*.7);
float d=100,detail=100;
int kind=(int)floor(GlyphKind+.5);
if(kind<=1) {
 d=min(length(p-float2(28,25))-20,length(p-float2(52,25))-20);
 d=min(d,S.box(p,float2(40,40),float2(24,19),12));
 d=min(d,min(S.capsule(p,float2(24,48),float2(28,68),9),S.capsule(p,float2(56,48),float2(52,68),9)));
 d=max(d,-(length(p-float2(40,73))-13));
 d=max(d,-(length(p-float2(40,-2))-13));
 if(kind==0) {
  if(IsDead>.5) {detail=min(min(S.capsule(p,float2(24,29),float2(32,37),1.5),S.capsule(p,float2(32,29),float2(24,37),1.5)),min(S.capsule(p,float2(48,29),float2(56,37),1.5),S.capsule(p,float2(56,29),float2(48,37),1.5)));}
  else {detail=min(length(p-float2(28,32))-2.1,length(p-float2(52,32))-2.1);}
  float smileY=IsSad>.5?46:42;
  float centerY=IsSad>.5?42:47;
  detail=min(detail,min(S.capsule(p,float2(32,smileY),float2(40,centerY),1.5),S.capsule(p,float2(40,centerY),float2(48,smileY),1.5)));
 }
} else if(kind==2) {
 d=S.capsule(p,float2(23,65),float2(49,26),4.8);
 d=min(d,S.capsule(p,float2(44,25),float2(56,33),6));
 for(int i=0;i<5;i++) d=min(d,S.capsule(p,float2(43+i*3,23+i*2),float2(48+i*3,15+i*2),1.8));
 detail=S.capsule(p,float2(23,60),float2(35,42),1);
} else if(kind==3) {
 d=S.capsule(p,float2(26,66),float2(49,26),4.3);
 d=min(d,S.capsule(p,float2(18,25),float2(37,16),4));
 d=min(d,S.capsule(p,float2(37,16),float2(57,20),4));
 d=min(d,S.capsule(p,float2(57,20),float2(68,30),3));
} else if(kind==4) {
 d=S.capsule(p,float2(24,65),float2(37,48),5);
 d=min(d,S.TriSDF(p,float2(37,48),float2(62,13),float2(59,39)));
 d=min(d,S.TriSDF(p,float2(37,48),float2(59,39),float2(46,50)));
} else if(kind==5) {
 d=S.box(p,float2(40,50),float2(17,22),5);
 d=min(d,S.box(p,float2(38,22),float2(7,9),2));
 d=min(d,S.box(p,float2(47,13),float2(15,4),2));
 detail=min(S.capsule(p,float2(31,50),float2(49,50),2),S.capsule(p,float2(40,41),float2(40,59),2));
} else if(kind==6) {
 d=abs(length(p-float2(40,40))-26)-2.3;
 d=min(d,S.capsule(p,float2(40,40),float2(40,21),2.1));
 d=min(d,S.capsule(p,float2(40,40),float2(52,47),2.1));
} else {
 d=S.box(p,float2(40,43),float2(21,28),17);
 detail=min(S.capsule(p,float2(40,16),float2(40,38),1.7),S.capsule(p,float2(19,38),float2(61,38),1.7));
}
float outer=saturate(.5-d/aa),inner=saturate(.5-(d+2)/aa);
float rim=outer-inner;
float3 tint=SurfaceColor.rgb;
float tintAlpha=SurfaceColor.a;
if(kind==1 && (IsLost>.5 || p.y<lerp(80,0,saturate(HealthFraction)))) {tint=EmptyColor.rgb;tintAlpha=EmptyColor.a;}
if(kind==0 && IsDead>.5) tint=lerp(tint,EmptyColor.rgb,.7);
float highlight=exp(-pow(length((p-float2(26,20))/float2(10,15)),2))*inner;
float3 rgb=lerp(tint,HighlightColor.rgb,highlight*.32*HighlightColor.a)*inner*tintAlpha+InkColor.rgb*rim*InkColor.a;
float alpha=inner*tintAlpha+rim*InkColor.a;
float lines=saturate(.5-detail/aa)*inner;
rgb=lerp(rgb,InkColor.rgb*alpha,lines*saturate(InkColor.a));
if(kind==1 && IsLost>.5) {
 float cross=min(S.capsule(p,float2(20,14),float2(61,69),2),S.capsule(p,float2(60,14),float2(20,69),2));
 float crossA=saturate(.5-cross/aa)*outer;rgb=lerp(rgb,InkColor.rgb*alpha,crossA*saturate(InkColor.a));
}
if(kind==0 && IsHost>.5) {
 float crown=min(S.TriSDF(p,float2(23,17),float2(20,1),float2(34,12)),min(S.TriSDF(p,float2(30,17),float2(40,0),float2(50,17)),S.TriSDF(p,float2(46,12),float2(61,1),float2(57,17))));
 crown=min(crown,S.box(p,float2(40,17),float2(17,3),1));
 float ca=saturate(.5-crown/aa)*HostColor.a;rgb=HostColor.rgb*ca+rgb*(1-ca);alpha=ca+alpha*(1-ca);
}
return float4(alpha>.00001?rgb/alpha:0,saturate(alpha));
'''

# Colors connect from the VectorParameter RGBA output, not default RGB. The UI
# output is straight alpha: each layer is accumulated premultiplied then divided.
SURFACE_CODE = r'''
static const int mode = __MODE__;
float2 size = max(PanelSize.xy, float2(1.0, 1.0));
float2 p = (UV - 0.5) * size;
float aa = max(0.65, max(length(ddx(p)), length(ddy(p))));
float selected = mode == 2 ? saturate(Selected) : 0.0;
float focus = mode == 2 ? saturate(Focus) : 0.0;
float glow = clamp(Glow + selected * 0.45 + focus * 0.65, 0.0, 4.0);
float reserve = clamp(1.0 + glow * 1.5, 1.0, 5.0);
float2 halfSize = max(size * 0.5 - reserve, float2(0.5, 0.5));
float radius = clamp(Radius, 0.0, min(halfSize.x, halfSize.y));
float width = clamp(BorderWidth + selected * 0.25 + focus * 0.35,
    0.0, min(halfSize.x, halfSize.y));

// Rounded rectangle SDF in logical pixels.
float2 q = abs(p) - (halfSize - radius);
float rectangleD = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;

// Rounded rhombus: finite line-segment distance, so non-square tool slots keep
// a constant outline width rather than stretching a square texture.
float2 diamondHalf = max(halfSize - radius, float2(0.5, 0.5));
float2 ap = abs(p);
float2 edgeA = float2(diamondHalf.x, 0.0);
float2 edgeV = float2(-diamondHalf.x, diamondHalf.y);
float along = saturate(dot(ap - edgeA, edgeV) / max(dot(edgeV, edgeV), 1e-5));
float diamondD = length(ap - (edgeA + edgeV * along));
float diamondSign = ap.x * diamondHalf.y + ap.y * diamondHalf.x
    > diamondHalf.x * diamondHalf.y ? 1.0 : -1.0;
diamondD = diamondD * diamondSign - radius;
float d = lerp(rectangleD, diamondD, saturate(Shape));
float outer = saturate(0.5 - d / aa);
float inner = saturate(0.5 - (d + width) / aa);
float border = max(outer - inner, 0.0);

float3 surfaceRGB = max(SurfaceColor.rgb, 0.0);
float3 edgeRGB = max(EdgeColor.rgb, 0.0);
float3 highlightRGB = max(HighlightColor.rgb, 0.0);
float surfaceAlpha = saturate(SurfaceColor.a);
float edgeAlpha = saturate(EdgeColor.a);
float highlightAlpha = saturate(HighlightColor.a);
float vertical = saturate(0.5 - p.y / max(halfSize.y * 2.0, 1.0));
float lightMode = smoothstep(0.30, 0.65,
    dot(surfaceRGB, float3(0.2126, 0.7152, 0.0722)));
float shade = lerp(lerp(0.78, 1.10, vertical), lerp(0.94, 1.02, vertical), lightMode);
float3 baseRGB = surfaceRGB * shade;
float active = inner;
float segmentMask = 1.0;

if (mode == 1)
{
    // Track and fill share the same silhouette. The active part has its own
    // rounded end cap; Fill=1 leaves clipping to an ordinary UProgressBar.
    float2 ih = max(halfSize - width, float2(0.25, 0.25));
    float fraction = saturate(Fill);
    float fillWidth = ih.x * 2.0 * fraction;
    float2 fh = float2(max(fillWidth * 0.5, 0.01), ih.y);
    float2 fp = p - float2(-ih.x + fillWidth * 0.5, 0.0);
    float fr = min(max(radius - width, 0.0), min(fh.x, fh.y));
    float2 fq = abs(fp) - (fh - fr);
    float fd = length(max(fq, 0.0)) + min(max(fq.x, fq.y), 0.0) - fr;
    active = saturate(0.5 - fd / aa) * step(0.0001, fraction) * inner;
    float count = clamp(floor(Segments + 0.5), 0.0, 128.0);
    if (count > 1.5)
    {
        float usableWidth = max(ih.x * 2.0, 1.0);
        float localX = saturate((p.x + ih.x) / usableWidth);
        float phase = frac(localX * count);
        float gapDistance = min(phase, 1.0 - phase) * usableWidth / count;
        segmentMask = saturate((gapDistance - 0.65) / aa + 0.5);
    }
    float3 trackRGB = surfaceRGB * clamp(Dim, 0.0, 1.0);
    float3 fillRGB = surfaceRGB * lerp(0.88, 1.08, vertical);
    // Interpolate RGB within one surface layer. Compositing a second surface
    // over the track would incorrectly turn SurfaceColor.a=.5 into .75.
    baseRGB = lerp(trackRGB, fillRGB, saturate(active / max(inner, 1e-5)));
}

// Premultiplied over-compositing; all layer opacities come from the authored
// RGBA colors. Setting all three alpha channels to zero makes a clear material.
float glowCoverage = exp(-max(d, 0.0) / max(0.65, 0.85 + glow * 0.8))
    * (1.0 - inner) * glow * 0.15;
float a = saturate(glowCoverage) * edgeAlpha;
float3 rgb = edgeRGB * a;

float surfaceCoverage = inner * (mode == 1 ? segmentMask : 1.0);
float layerA = surfaceCoverage * surfaceAlpha;
rgb = baseRGB * layerA + rgb * (1.0 - layerA);
a = layerA + a * (1.0 - layerA);

layerA = border * edgeAlpha;
float3 rimRGB = edgeRGB * lerp(0.78, 1.04, vertical);
rgb = rimRGB * layerA + rgb * (1.0 - layerA);
a = layerA + a * (1.0 - layerA);

// Thin cool upper lip and a soft restrained gloss, also suitable for cream cards.
float topLip = border * smoothstep(0.42, 0.92, vertical) * 0.40;
float gloss = exp(-pow((vertical - 0.72) / 0.20, 2.0));
gloss *= mode == 1 ? active * segmentMask * 0.24 : inner * 0.035;
float focusRing = mode == 2 ? border * max(selected * 0.12, focus * 0.30) : 0.0;
layerA = saturate(topLip + gloss + focusRing) * highlightAlpha;
rgb = highlightRGB * layerA + rgb * (1.0 - layerA);
a = layerA + a * (1.0 - layerA);
return float4(a > 1e-5 ? rgb / a : float3(0.0, 0.0, 0.0), saturate(a));
'''


def linear_color(r, g, b, a=1.0):
    """Decode authoring sRGB bytes once; the material/MID contract is LINEAR."""
    def decode(value):
        value = value / 255.0
        return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4
    return u.LinearColor(decode(r), decode(g), decode(b), a)


def require_idle():
    if u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError('Phase 1 material authoring requires an idle editor; stop PIE first')


def assert_owned_or_new(name):
    if name not in NAMES:
        raise RuntimeError('Unexpected material outside this task: ' + name)
    path = BASE + '/' + name
    if not u.EditorAssetLibrary.does_asset_exist(path):
        return None
    material = u.load_asset(path)
    if not isinstance(material, u.Material):
        raise RuntimeError('Preserved unexpected non-material asset: ' + path)
    marker = MARKER_PREFIX + name
    if not any(isinstance(node, u.MaterialExpressionCustom)
               and node.get_editor_property('description') == marker
               for node in u.MaterialEditingLibrary.get_material_expressions(material)):
        raise RuntimeError('Preserved material without exact Phase 1 ownership marker: ' + path)
    return material


def author_material(name, mode):
    require_idle()
    material = assert_owned_or_new(name)
    edit = u.MaterialEditingLibrary
    if material:
        edit.delete_all_material_expressions(material)
    else:
        material = u.AssetToolsHelpers.get_asset_tools().create_asset(
            name, BASE, u.Material, u.MaterialFactoryNew())
    if not material:
        raise RuntimeError('Could not create ' + BASE + '/' + name)
    material.set_editor_property('material_domain', u.MaterialDomain.MD_UI)
    material.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
    vectors = {
        'SurfaceColor': linear_color(33, 239, 246, 1.0) if mode == 1 else linear_color(13, 58, 79, 0.96),
        'EdgeColor': linear_color(35, 222, 237, 0.94),
        'HighlightColor': linear_color(232, 255, 248, 0.80),
        'PanelSize': u.LinearColor(220, 18, 0, 0) if mode == 1 else u.LinearColor(64, 64, 0, 0) if mode == 2 else u.LinearColor(320, 180, 0, 0),
    }
    if mode==3:
        vectors.update({'InkColor':linear_color(3,43,64),'EmptyColor':linear_color(47,89,104),
                        'HostColor':linear_color(255,222,83)})
    scalars = {
        'Radius': 7.0 if mode == 1 else 5.0 if mode == 2 else 12.0,
        'BorderWidth': 1.15,
        'Glow': 0.18 if mode == 1 else 0.25,
        'Segments': 0.0,
        'Shape': 1.0 if mode == 2 else 0.0,
        'Fill': 1.0,
        'Dim': 0.14,
        'Selected': 0.0,
        'Focus': 0.0,
    }
    if mode==3:
        scalars.update({'GlyphKind':0.,'HealthFraction':1.,'IsLost':0.,'IsHost':0.,'IsDead':0.,'IsSad':0.,'IsHappy':0.})
    nodes = [('UV', edit.create_material_expression(material, u.MaterialExpressionTextureCoordinate, -900, 0), '')]
    for index, (parameter, value) in enumerate(vectors.items()):
        node = edit.create_material_expression(material, u.MaterialExpressionVectorParameter, -900, 150 + index * 100)
        node.set_editor_property('parameter_name', parameter)
        node.set_editor_property('default_value', value)
        nodes.append((parameter, node, 'RGBA'))
    for index, (parameter, value) in enumerate(scalars.items()):
        node = edit.create_material_expression(material, u.MaterialExpressionScalarParameter, -600, 150 + index * 90)
        node.set_editor_property('parameter_name', parameter)
        node.set_editor_property('default_value', value)
        nodes.append((parameter, node, ''))
    custom = edit.create_material_expression(material, u.MaterialExpressionCustom, -240, 0)
    inputs = []
    for parameter, _, _ in nodes:
        item = u.CustomInput()
        item.set_editor_property('input_name', parameter)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)
    custom.set_editor_property('output_type', u.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property('description', MARKER_PREFIX + name)
    custom.set_editor_property('code', GLYPH_CODE if mode==3 else SURFACE_CODE.replace('__MODE__', str(mode)))
    for parameter, node, pin in nodes:
        if not edit.connect_material_expressions(node, pin, custom, parameter):
            raise RuntimeError('Could not connect explicit input ' + parameter + ' on ' + name)
    for channels, output, y in (
        ((True, True, True, False), u.MaterialProperty.MP_EMISSIVE_COLOR, 0),
        ((False, False, False, True), u.MaterialProperty.MP_OPACITY, 180),
    ):
        mask = edit.create_material_expression(material, u.MaterialExpressionComponentMask, 50, y)
        for channel, enabled in zip(('r', 'g', 'b', 'a'), channels):
            mask.set_editor_property(channel, enabled)
        if not edit.connect_material_expressions(custom, '', mask, ''):
            raise RuntimeError('Could not connect Phase 1 output mask')
        if not edit.connect_material_property(mask, '', output):
            raise RuntimeError('Could not connect Phase 1 UI material output')
    edit.recompile_material(material)
    if not u.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError('Could not save ' + BASE + '/' + name)
    return {'path': BASE + '/' + name, 'marker': MARKER_PREFIX + name,
            'mode': mode, 'parameters': [item[0] for item in nodes if item[0] != 'UV']}


def main():
    root = Path(u.Paths.project_dir()).resolve()
    if root.drive.upper() != 'E:':
        raise RuntimeError('Phase 1 project and generated reports must remain on drive E:')
    require_idle()
    # Preflight the complete target set before mutating any graph.
    for name in NAMES:
        assert_owned_or_new(name)
    report = [author_material(name, mode) for mode, name in enumerate(NAMES)]
    output = root / 'Saved/GameplayUI/phase1_material_authoring.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({
        'materials': report,
        'contract': 'Linear RGBA; PanelSize XY logical pixels; Shape 0 rect/1 diamond; Fill=1 when UProgressBar owns clipping.',
        'validation': 'Authoring/recompile requested. Shader diagnostics and native-scale visual QA are required before acceptance.',
    }, indent=2), encoding='utf-8')
    u.log('MC_PHASE1_PROCEDURAL_MATERIALS_AUTHORED ' + str(output))


if __name__ == '__main__':
    main()
