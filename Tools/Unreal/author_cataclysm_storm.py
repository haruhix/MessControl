"""Derive storm-only liquid graphs without editing source profiles/materials.

Run in idle MessControl Editor. All writes remain in /Game/Cataclysm/Storm.
Macro constants come from MCCataclysmStormMath.h; copied liquid hierarchy keeps
its coffee/cola identity, micro ripple WPO, optical textures and source overrides.
"""
import hashlib
import json
import re
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
FOLDER = '/Game/Cataclysm/Storm'
OWNER = 'MCCataclysmStormSource'
VERSION = 'MCCataclysmStormGraphVersion'
lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
tools = u.AssetToolsHelpers.get_asset_tools()
HEADER = ROOT / 'Source/MessControl/Private/MCCataclysmStormMath.h'
CONSTANTS = {k: float(v) for k, v in re.findall(r'constexpr float\s+(\w+)\s*=\s*([.\d]+)f;', HEADER.read_text(encoding='utf-8'))}
require_names = ('PrimaryLength', 'SecondaryLength', 'PrimaryAmplitude', 'SecondaryAmplitude',
                 'HarmonicAmplitude', 'PrimarySpeed', 'SecondarySpeed', 'SecondaryCos', 'SecondarySin',
                 'FrontWidth', 'FrontAmplitude', 'FrontCrestWidth', 'DrainRadius', 'DrainDepth', 'OpticalSpeed')
if not all(k in CONSTANTS for k in require_names):
    raise RuntimeError('Storm native constants changed; review the shared wave contract')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def package(obj):
    return obj.get_path_name().split('.')[0]


def save(obj):
    require(package(obj).startswith(FOLDER + '/'), 'Refusing an unrelated package save')
    require(lib.save_loaded_asset(obj, only_if_is_dirty=False), 'Storm package save failed')


def node(mat, cls, x, y, **values):
    result = edit.create_material_expression(mat, cls, x, y)
    require(result, 'Could not create storm expression')
    if values:
        result.set_editor_properties(values)
    return result


def connect(src, pin, dst, name):
    require(edit.connect_material_expressions(src, pin, dst, name), 'Storm graph link failed: ' + name)


def output(src, prop):
    require(edit.connect_material_property(src, '', prop), 'Storm output link failed: ' + str(prop))


def custom(mat, description, code, inputs, dimension, x, y):
    result = node(mat, u.MaterialExpressionCustom, x, y, description=description, code=code,
        output_type={1: u.CustomMaterialOutputType.CMOT_FLOAT1, 2: u.CustomMaterialOutputType.CMOT_FLOAT2,
                     3: u.CustomMaterialOutputType.CMOT_FLOAT3, 4: u.CustomMaterialOutputType.CMOT_FLOAT4}[dimension])
    entries = []
    for key in inputs:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', key)
        entries.append(entry)
    result.set_editor_property('inputs', entries)
    for name, (source, pin) in inputs.items():
        connect(source, pin, result, name)
    return result


def parameter(mat, name, value, vector=False):
    cls = u.MaterialExpressionVectorParameter if vector else u.MaterialExpressionScalarParameter
    existing = [n for n in edit.get_material_expressions(mat)
                if isinstance(n, cls) and str(n.get_editor_property('parameter_name')) == name]
    require(len(existing) <= 1, 'Duplicate material parameter: ' + name)
    if existing:
        return existing[0]
    result = node(mat, cls, -1900, 1100, parameter_name=name, group='Cataclysm Storm',
                  default_value=u.LinearColor(*value) if vector else value)
    return result


def source_input(mat, prop):
    expression = edit.get_material_property_input_node(mat, prop)
    pin = edit.get_material_property_input_node_output_name(mat, prop) if expression else ''
    return expression, pin


def wave_code(constants=None):
    c = CONSTANTS if constants is None else constants
    return f'''float2 q=P.xy-Center.xy;
float2 d=Direction.xy;
float2 e=float2(d.x*{c['SecondaryCos']:.9g}-d.y*{c['SecondarySin']:.9g},d.x*{c['SecondarySin']:.9g}+d.y*{c['SecondaryCos']:.9g});
float a=dot(q,d)*(6.28318530718/{c['PrimaryLength']:.9g})-Time*{c['PrimarySpeed']:.9g}+Phase;
float b=dot(q,e)*(6.28318530718/{c['SecondaryLength']:.9g})-Time*{c['SecondarySpeed']:.9g}+Phase*1.37+1.1;
float h=Strength*({c['PrimaryAmplitude']:.9g}*sin(a)+{c['SecondaryAmplitude']:.9g}*sin(b)+{c['HarmonicAmplitude']:.9g}*sin(2*a+.4));
float2 slope=Strength*(({c['PrimaryAmplitude']:.9g}*cos(a)+2*{c['HarmonicAmplitude']:.9g}*cos(2*a+.4))*(6.28318530718/{c['PrimaryLength']:.9g})*d+{c['SecondaryAmplitude']:.9g}*cos(b)*(6.28318530718/{c['SecondaryLength']:.9g})*e);
float crest=Strength*smoothstep(.50,.91,sin(a)+.13*sin(2*a+.4));
return float4(h,slope,crest);'''


def derive_graph_v1(mat):
    expressions = list(edit.get_material_expressions(mat))
    existing = [n for n in expressions if isinstance(n, u.MaterialExpressionCustom)
                and n.get_editor_property('description') == 'MC Storm: shared macro height and derivative']
    if lib.get_metadata_tag(mat, VERSION) == '1':
        require(len(existing) == 1 and existing[0].get_editor_property('code') == wave_code(),
                'Preserved a changed custom storm graph; native/material constants differ')
        return
    require(not existing, 'Incomplete storm graph needs targeted repair before rerun')
    position = node(mat, u.MaterialExpressionWorldPosition, -2300, 0)
    time = parameter(mat, 'StormTime', 0.)
    phase = parameter(mat, 'StormPhase', 0.)
    strength = parameter(mat, 'StormStrength', 0.)
    direction = parameter(mat, 'StormDirection', (1., 0., 0., 0.), True)
    center = parameter(mat, 'ArenaCenter', (0., 0., 0., 0.), True)
    wave = custom(mat, 'MC Storm: shared macro height and derivative', wave_code(),
        {'P': (position, ''), 'Time': (time, ''), 'Phase': (phase, ''), 'Strength': (strength, ''),
         'Direction': (direction, 'RGBA'), 'Center': (center, 'RGBA')}, 4, -1300, 0)
    wpo, pin = source_input(mat, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    require(wpo, 'Source liquid lacks its shared micro-height WPO contract')
    combined = custom(mat, 'MC Storm: preserve micro height plus macro wave', 'return Old+float3(0,0,Wave.x);',
        {'Old': (wpo, pin), 'Wave': (wave, '')}, 3, 1400, 0)
    output(combined, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    normal, pin = source_input(mat, u.MaterialProperty.MP_NORMAL)
    require(normal and not mat.get_editor_property('tangent_space_normal'), 'Source liquid must supply world-space surface normals')
    combined_normal = custom(mat, 'MC Storm: analytic combined surface slope',
        'return normalize(float3(Old.xy/max(.18,Old.z)-Wave.yz,1));',
        {'Old': (normal, pin), 'Wave': (wave, '')}, 3, 1400, 220)
    output(combined_normal, u.MaterialProperty.MP_NORMAL)
    broad = [n for n in expressions if isinstance(n, u.MaterialExpressionCustom)
             and n.get_editor_property('description') == 'World flow UV, broad ripples']
    for flow in broad:
        entries = list(flow.get_editor_property('inputs'))
        if 'StormDirection' not in [str(p.get_editor_property('input_name')) for p in entries]:
            entry = u.CustomInput()
            entry.set_editor_property('input_name', 'StormDirection')
            entries.append(entry)
            flow.set_editor_property('inputs', entries)
        connect(direction, 'RGBA', flow, 'StormDirection')
        flow.set_editor_property('code', 'float2 flow=normalize(StormDirection.xy+float2(.000001,0));\nreturn P.xy/TileSize-flow*WaterTime*FlowSpeed/TileSize;')
    foam_uv = custom(mat, 'MC Storm: continuous foam advection',
        'float2 d=normalize(Direction.xy+float2(.000001,0));return P.xy/180.-d*Time*.72;',
        {'P': (position, ''), 'Direction': (direction, 'RGBA'), 'Time': (time, '')}, 2, -900, 500)
    detail = lib.load_asset('/Game/Gameplay/Liquid/Stylized/T_LiquidDetail_M')
    require(isinstance(detail, u.Texture2D), 'Existing optical liquid detail texture is required, read only')
    cells = node(mat, u.MaterialExpressionTextureSample, -650, 500, texture=detail,
                 sampler_type=u.MaterialSamplerType.SAMPLERTYPE_MASKS)
    connect(foam_uv, '', cells, 'UVs')
    mask = custom(mat, 'MC Storm: broken crest foam, never full white bands',
        'return Wave.w*(.10+.90*smoothstep(.28,.74,Cells.g))*smoothstep(.08,.55,Cells.a);',
        {'Wave': (wave, ''), 'Cells': (cells, 'RGBA')}, 1, -300, 500)
    tint = parameter(mat, 'FoamColor', (.72, .49, .24, 1.), True)
    color, pin = source_input(mat, u.MaterialProperty.MP_BASE_COLOR)
    require(color, 'Source liquid color connection is required for identity preservation')
    froth = custom(mat, 'MC Storm: source coffee or cola under aerated crests',
        'return lerp(Old,FoamColor.rgb,min(.82,Foam));',
        {'Old': (color, pin), 'FoamColor': (tint, 'RGBA'), 'Foam': (mask, '')}, 3, 1400, 480)
    output(froth, u.MaterialProperty.MP_BASE_COLOR)
    rough, pin = source_input(mat, u.MaterialProperty.MP_ROUGHNESS)
    if rough:
        rough_froth = custom(mat, 'MC Storm: porous foam roughness', 'return lerp(Old,.56,Foam*.8);',
            {'Old': (rough, pin), 'Foam': (mask, '')}, 1, 1400, 680)
        output(rough_froth, u.MaterialProperty.MP_ROUGHNESS)
    emission, pin = source_input(mat, u.MaterialProperty.MP_EMISSIVE_COLOR)
    if emission:
        froth_emission = custom(mat, 'MC Storm: subdued crest fill', 'return Old*(1-Foam*.6)+FoamColor.rgb*Foam*.18;',
            {'Old': (emission, pin), 'Foam': (mask, ''), 'FoamColor': (tint, 'RGBA')}, 3, 1400, 880)
        output(froth_emission, u.MaterialProperty.MP_EMISSIVE_COLOR)
    errors = list(edit.recompile_material(mat))
    require(not errors, 'Storm graph compile failed: ' + str(errors))
    lib.set_metadata_tag(mat, VERSION, '1')
    lib.set_metadata_tag(mat, 'MCStormMathSHA256', hashlib.sha256(HEADER.read_bytes()).hexdigest())
    save(mat)


LEGACY_CONSTANTS = dict(PrimaryLength=640., SecondaryLength=370., PrimaryAmplitude=24.,
    SecondaryAmplitude=10., HarmonicAmplitude=4., PrimarySpeed=1.65, SecondarySpeed=2.2,
    SecondaryCos=.838670568, SecondarySin=.544639035)


def replace_custom(mat, description, code, inputs, dimension, x, y):
    matches = [n for n in edit.get_material_expressions(mat)
        if isinstance(n, u.MaterialExpressionCustom) and n.get_editor_property('description') == description]
    require(len(matches) <= 1, 'Duplicate owned storm expression: ' + description)
    if not matches:
        return custom(mat, description, code, inputs, dimension, x, y)
    result = matches[0]
    entries = []
    for key in inputs:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', key)
        entries.append(entry)
    result.set_editor_properties(dict(code=code, inputs=entries,
        output_type={1: u.CustomMaterialOutputType.CMOT_FLOAT1, 2: u.CustomMaterialOutputType.CMOT_FLOAT2,
                     3: u.CustomMaterialOutputType.CMOT_FLOAT3, 4: u.CustomMaterialOutputType.CMOT_FLOAT4}[dimension]))
    for name, (source, pin) in inputs.items():
        connect(source, pin, result, name)
    return result


def stream_surface_code_v2():
    """One native-equivalent sample drives geometry, wet coverage and slope.

    Derivatives sample the *whole* field, including front/domain masks. This
    avoids a smooth normal over a sharply sloping arrival front or drain basin.
    The CPU comparison checks height separately; this is not GPU readback.
    """
    c = CONSTANTS
    return f'''struct MCMouthStreamField {{
float4 Sample(float2 p,float2 inlet,float2 outlet,float2 center,float2 arena,float2 d,
    float time,float phase,float front,float tail,float width,float drainRadius,float drainDepth,
    float wetDepth,float activation,float rippleHeight,float rippleLength,float rippleSpeed) {{
    float2 side=float2(-d.y,d.x),q=p-inlet;
    float along=dot(q,d),across=dot(q,side),outAlong=dot(outlet-inlet,d);
    float jitter=sin(across*.0035+time*1.6)*75+sin(across*.0081-time*2.2)*32;
    float frontAt=front+jitter;
    float halfWidth=abs(side.x)*arena.x+abs(side.y)*arena.y;
    float streamWidth=lerp(halfWidth,drainRadius*1.65,smoothstep(outAlong-1100,outAlong+100,along));
    float wet=saturate(activation)*smoothstep(-120,20,along)
        *(1-smoothstep(frontAt-width,frontAt+width,along))*smoothstep(tail-width,tail+width,along)
        *(1-smoothstep(streamWidth-80,streamWidth+80,abs(across)))
        *(1-smoothstep(outAlong+80,outAlong+360,along));
    float k=6.2831853/max(rippleLength,1),rt=time*rippleSpeed;
    float ra=k*(p.x+.28*p.y)-rt,rb=k*(-.45*p.x+.9*p.y)*1.73+rt*1.21,
          rc=k*(.2*p.x+p.y)*2.7-rt*.73;
    float micro=rippleHeight*(.6*sin(ra)+.28*sin(rb)+.12*sin(rc));
    float2 e=float2(d.x*{c['SecondaryCos']:.9g}-d.y*{c['SecondarySin']:.9g},d.x*{c['SecondarySin']:.9g}+d.y*{c['SecondaryCos']:.9g});
    float a=dot(p-center,d)*(6.28318530718/{c['PrimaryLength']:.9g})-time*{c['PrimarySpeed']:.9g}+phase;
    float b=dot(p-center,e)*(6.28318530718/{c['SecondaryLength']:.9g})-time*{c['SecondarySpeed']:.9g}+phase*1.37+1.1;
    float macro={c['PrimaryAmplitude']:.9g}*sin(a)+{c['SecondaryAmplitude']:.9g}*sin(b)+{c['HarmonicAmplitude']:.9g}*sin(2*a+.4);
    float frontBand=(along-frontAt+100)/{c['FrontCrestWidth']:.9g};
    float crest={c['FrontAmplitude']:.9g}*exp(-frontBand*frontBand)*(1+.12*sin(across*.009-time*5));
    float r=length(p-outlet),drain=exp(-r*r/max(1,drainRadius*drainRadius));
    float height=-(1-wet)*wetDepth+wet*(micro+macro+crest-drainDepth*drain);
    return float4(height,wet,wet*saturate(crest/{c['FrontAmplitude']:.9g}),wet*drain);
}} }};
MCMouthStreamField field;
float2 d=normalize(Direction.xy+float2(.000001,0));
float4 s=field.Sample(P.xy,Inlet.xy,Outlet.xy,Center.xy,Arena.xy,d,Time,Phase,Front,Tail,Width,
    DrainRadius,DrainDepth,WetDepth,Activation,RippleHeight,RippleLength,RippleSpeed);
float hx=field.Sample(P.xy+float2(2,0),Inlet.xy,Outlet.xy,Center.xy,Arena.xy,d,Time,Phase,Front,Tail,Width,
    DrainRadius,DrainDepth,WetDepth,Activation,RippleHeight,RippleLength,RippleSpeed).x;
float lx=field.Sample(P.xy-float2(2,0),Inlet.xy,Outlet.xy,Center.xy,Arena.xy,d,Time,Phase,Front,Tail,Width,
    DrainRadius,DrainDepth,WetDepth,Activation,RippleHeight,RippleLength,RippleSpeed).x;
float hy=field.Sample(P.xy+float2(0,2),Inlet.xy,Outlet.xy,Center.xy,Arena.xy,d,Time,Phase,Front,Tail,Width,
    DrainRadius,DrainDepth,WetDepth,Activation,RippleHeight,RippleLength,RippleSpeed).x;
float ly=field.Sample(P.xy-float2(0,2),Inlet.xy,Outlet.xy,Center.xy,Arena.xy,d,Time,Phase,Front,Tail,Width,
    DrainRadius,DrainDepth,WetDepth,Activation,RippleHeight,RippleLength,RippleSpeed).x;
return float4(s.x,(hx-lx)/4,(hy-ly)/4,s.y);'''


def stream_surface_code_v3():
    """Keep the v2 verifier intact; v3 decouples domain clipping from depth.

    Lowering vertices by the lateral domain mask folded the outer surface into
    a steep curtain. Arrival/tail still lower the genuinely dry front, while
    lateral and outlet masks only clip coverage and taper wave/dip displacement.
    """
    code = stream_surface_code_v2()
    old_mask = '''float wet=saturate(activation)*smoothstep(-120,20,along)
        *(1-smoothstep(frontAt-width,frontAt+width,along))*smoothstep(tail-width,tail+width,along)
        *(1-smoothstep(streamWidth-80,streamWidth+80,abs(across)))
        *(1-smoothstep(outAlong+80,outAlong+360,along));'''
    new_mask = '''float arrival=saturate(activation)*smoothstep(-120,20,along)
        *(1-smoothstep(frontAt-width,frontAt+width,along))*smoothstep(tail-width,tail+width,along);
    float domain=(1-smoothstep(streamWidth-80,streamWidth+80,abs(across)))
        *(1-smoothstep(outAlong+80,outAlong+360,along));
    float wet=arrival*domain;'''
    old_height = 'float height=-(1-wet)*wetDepth+wet*(micro+macro+crest-drainDepth*drain);'
    new_height = 'float height=-(1-arrival)*wetDepth+wet*(micro+macro+crest-drainDepth*drain);'
    require(code.count(old_mask) == 1 and code.count(old_height) == 1, 'V2 surface verifier changed')
    return code.replace(old_mask,new_mask).replace(old_height,new_height)


def stream_surface_code():
    """V4 floods the complete mouth rectangle; the sink never clips coverage."""
    code = stream_surface_code_v3()
    old_domain = '''float domain=(1-smoothstep(streamWidth-80,streamWidth+80,abs(across)))
        *(1-smoothstep(outAlong+80,outAlong+360,along));'''
    new_domain = '''float domain=step(abs(p.x-center.x),arena.x)*step(abs(p.y-center.y),arena.y);'''
    require(code.count(old_domain) == 1, 'V3 surface verifier changed')
    return code.replace(old_domain,new_domain)


def derive_graph(mat):
    version = lib.get_metadata_tag(mat, VERSION)
    fresh = not version
    if not version:
        derive_graph_v1(mat)
        version = '1'
    require(version in ('1', '2', '3', '4'), 'Preserved an unknown owned storm graph version')
    expressions = list(edit.get_material_expressions(mat))
    by_desc = {str(n.get_editor_property('description')): n for n in expressions
               if isinstance(n, u.MaterialExpressionCustom)}
    required = ('MC Storm: shared macro height and derivative', 'MC Storm: preserve micro height plus macro wave',
        'MC Storm: analytic combined surface slope', 'World flow UV, broad ripples',
        'Analytic surface slopes and directional small ripples',
        'Advected microfoam, impact froth, wave crest and player wakes',
        'Wet front expands from inlet; softened arena and object intersections')
    require(all(name in by_desc for name in required), 'Incomplete source/owned graph; preserve before targeted repair')
    expected = (wave_code(CONSTANTS if fresh else LEGACY_CONSTANTS) if version == '1'
                else stream_surface_code_v2() if version == '2'
                else stream_surface_code_v3() if version == '3' else stream_surface_code())
    require(by_desc['MC Storm: shared macro height and derivative'].get_editor_property('code') == expected,
        'Preserved a hand-edited storm surface graph; review before migration')
    parameters = {str(n.get_editor_property('parameter_name')): n for n in expressions
        if isinstance(n, (u.MaterialExpressionScalarParameter, u.MaterialExpressionVectorParameter))}
    def param(name, default, vector=False):
        return parameter(mat, name, default, vector)
    position = next(n for n in expressions if isinstance(n, u.MaterialExpressionWorldPosition))
    position.set_editor_property('world_position_shader_offset', u.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    time, phase = param('StormTime', 0.), param('StormPhase', 0.)
    direction = param('StormDirection', (1.,0.,0.,0.), True)
    inlet, outlet = parameters['Inlet'], parameters['Outlet']
    front, tail = param('StormFrontDistance', 0.), param('StormTailDistance', -100000.)
    width = param('StormFrontWidth', CONSTANTS['FrontWidth'])
    drain_radius, drain_depth = param('StormDrainRadius', CONSTANTS['DrainRadius']), param('StormDrainDepth', CONSTANTS['DrainDepth'])
    wet_depth, activation = param('StormWetDepth', 150.), param('StormActivation', 0.)
    speed = param('StormOpticalSpeed', CONSTANTS['OpticalSpeed'])
    wave = replace_custom(mat, 'MC Storm: shared macro height and derivative', stream_surface_code(),
        {'P': (position,''), 'Time': (time,''), 'Phase': (phase,''), 'Direction': (direction,'RGBA'),
         'Inlet': (inlet,'RGBA'), 'Outlet': (outlet,'RGBA'), 'Center': (parameters['ArenaCenter'],'RGBA'),
         'Arena': (parameters['ArenaSize'],'RGBA'), 'Front': (front,''), 'Tail': (tail,''), 'Width': (width,''),
         'DrainRadius': (drain_radius,''), 'DrainDepth': (drain_depth,''), 'WetDepth': (wet_depth,''),
         'Activation': (activation,''), 'RippleHeight': (parameters['RippleHeight'],''),
         'RippleLength': (parameters['RippleLength'],''), 'RippleSpeed': (parameters['RippleSpeed'],'')}, 4, -1300, 0)
    combined = replace_custom(mat, 'MC Storm: preserve micro height plus macro wave',
        'return float3(0,0,Wave.x);', {'Wave': (wave,'')}, 3, 1400, 0)
    output(combined, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    # Keep texture normal detail, remove the old radial inlet mound/front/pit.
    by_desc['Analytic surface slopes and directional small ripples'].set_editor_property('code',
        'return normalize(float3((N1.xy+float2(-N2.y,N2.x)*.50)*NormalDetail,1));')
    # The source's player wakes and shoreline remain; all arrival/drain foam
    # comes from the directionally moving storm layer below.
    by_desc['Advected microfoam, impact froth, wave crest and player wakes'].set_editor_property('code', '''
float cloud=smoothstep(.28,.73,Cells.g);
float rim=pow(saturate(1-Shore),.72)*(.50+.50*cloud);
float wake=0; float4 points[4]={Wake0,Wake1,Wake2,Wake3};
[unroll] for(int i=0;i<4;i++) {
    [branch] if(points[i].z>.001) {
        float wd=length(P.xy-points[i].xy);
        wake+=pow(saturate(.5+.5*sin(wd*.085-WaterTime*5+Cells.b)),5)
            *exp(-wd/95)*smoothstep(12,28,wd)*points[i].z*points[i].w;
    }
}
return saturate(FoamAmount*(rim+WakeStrength*wake));''')
    old_opacity = by_desc['Wet front expands from inlet; softened arena and object intersections']
    opacity_inputs = edit.get_material_expression_input_names(old_opacity)
    require('Depth' in opacity_inputs and 'Edge' in opacity_inputs, 'Unknown source opacity inputs')
    # Add a single wet-field input without replacing artist depth/opacity pins.
    append_input(old_opacity, 'StormField', wave)
    old_opacity.set_editor_property('code', '''
float border=saturate(min(ArenaSize.x-abs(P.x-ArenaCenter.x),ArenaSize.y-abs(P.y-ArenaCenter.y))/40);
return saturate(lerp(ShallowOpacity,DeepOpacity,Depth)+Fresnel*.10+Foam*.2)*Edge*border*StormField.w;''')
    # The fixed optical direction has no spatially varying time multiplier, so
    # it does not stretch further and further during the hold.
    append_input(by_desc['World flow UV, broad ripples'], 'StormOpticalSpeed', speed)
    by_desc['World flow UV, broad ripples'].set_editor_property('code', '''
float2 flow=normalize(StormDirection.xy+float2(.000001,0));
return P.xy/TileSize-flow*WaterTime*StormOpticalSpeed/TileSize;''')
    foam_uv = replace_custom(mat, 'MC Storm: continuous foam advection', '''
float2 d=normalize(Direction.xy+float2(.000001,0)),side=float2(-d.y,d.x),q=P.xy-Inlet.xy;
return float2((dot(q,d)-Time*Speed)/480,dot(q,side)/72);''',
        {'P': (position,''), 'Inlet': (inlet,'RGBA'), 'Direction': (direction,'RGBA'),
         'Time': (time,''), 'Speed': (speed,'')}, 2, -900, 500)
    detail = lib.load_asset('/Game/Gameplay/Liquid/Stylized/T_LiquidDetail_M')
    require(isinstance(detail,u.Texture2D), 'Missing existing optical detail texture')
    cells = next(n for n in expressions if isinstance(n,u.MaterialExpressionTextureSample)
        and n.get_editor_property('texture') == detail)
    connect(foam_uv,'',cells,'UVs')
    drain_uv = replace_custom(mat, 'MC Storm: inward throat foam coordinates', '''
float2 q=P.xy-Outlet.xy; float r=max(length(q),.01),theta=atan2(q.y,q.x);
// Positive radial phase moves texels inward; integer angular tiling closes the seam.
return float2(theta*(4/6.28318530718)+r/max(Radius,1)*.42-Time*.55,r/105+Time*Speed*.72/105);''',
        {'P': (position,''), 'Outlet': (outlet,'RGBA'), 'Time': (time,''),
         'Speed': (speed,''), 'Radius': (drain_radius,'')}, 2, -900, 800)
    drain_samples = [n for n in edit.get_material_expressions(mat) if isinstance(n,u.MaterialExpressionTextureSample)
        and n.get_editor_property('desc') == 'MC Storm: inward throat foam detail']
    require(len(drain_samples)<=1, 'Duplicate throat foam sample')
    drain_cells = drain_samples[0] if drain_samples else node(mat,u.MaterialExpressionTextureSample,-650,800,
        texture=detail,sampler_type=u.MaterialSamplerType.SAMPLERTYPE_MASKS,desc='MC Storm: inward throat foam detail')
    connect(drain_uv,'',drain_cells,'UVs')
    mask = replace_custom(mat, 'MC Storm: broken crest foam, never full white bands', f'''
float2 d=normalize(Direction.xy+float2(.000001,0)),side=float2(-d.y,d.x),q=P.xy-Inlet.xy;
float along=dot(q,d),across=dot(q,side),r=length(P.xy-Outlet.xy);
float jitter=sin(across*.0035+Time*1.6)*75+sin(across*.0081-Time*2.2)*32;
float head=exp(-pow((along-Front-jitter+100)/{CONSTANTS['FrontCrestWidth']:.9g},2));
float pores=smoothstep(.23,.77,Cells.a),broken=smoothstep(.30,.72,Cells.g);
float stream=(.08+.50*broken)*pores;
float frontFoam=head*(.35+.65*broken)*(.35+.65*pores);
float drainEnvelope=(1-smoothstep(Radius*.20,Radius,r));
float drainFoam=drainEnvelope*(.18+.82*smoothstep(.24,.72,DrainCells.g))
    *smoothstep(.16,.70,DrainCells.a);
return Wave.w*saturate(stream+frontFoam*.88+drainFoam*.86);''',
        {'P': (position,''), 'Inlet': (inlet,'RGBA'), 'Outlet': (outlet,'RGBA'), 'Direction': (direction,'RGBA'),
         'Time': (time,''), 'Front': (front,''), 'Radius': (drain_radius,''),
         'Wave': (wave,''), 'Cells': (cells,'RGBA'), 'DrainCells': (drain_cells,'RGBA')}, 1, -300, 500)
    # The existing color, roughness and restrained emission wrappers already
    # reference this exact mask node, preserving both liquid identities.
    errors = list(edit.recompile_material(mat))
    require(not errors, 'Mouth stream graph compile failed: '+str(errors))
    lib.set_metadata_tag(mat,VERSION,'4')
    lib.set_metadata_tag(mat,'MCStormMathSHA256',hashlib.sha256(HEADER.read_bytes()).hexdigest())
    save(mat)


def append_input(expression, name, source, pin=''):
    entries = list(expression.get_editor_property('inputs'))
    if name not in [str(i.get_editor_property('input_name')) for i in entries]:
        entry = u.CustomInput()
        entry.set_editor_property('input_name',name)
        entries.append(entry)
        expression.set_editor_property('inputs',entries)
    connect(source,pin,expression,name)


def clone(original, name, cls):
    destination = FOLDER + '/' + name
    obj = lib.load_asset(destination) if lib.does_asset_exist(destination) else None
    if obj is None:
        obj = tools.duplicate_asset(name, FOLDER, original)
        require(obj, 'Storm material duplication failed')
        lib.set_metadata_tag(obj, OWNER, original.get_path_name())
    require(isinstance(obj, cls) and lib.get_metadata_tag(obj, OWNER) == original.get_path_name(),
            'Preserved unrelated or artist asset occupying ' + destination)
    return obj


def derive_profile(profile_path, role):
    profile = lib.load_asset(profile_path)
    require(profile, 'Missing saved liquid profile: ' + profile_path)
    source = u.SystemLibrary.load_asset_blocking(profile.get_editor_property('surface_material'))
    require(isinstance(source, u.MaterialInterface), 'Profile has no material: ' + profile_path)
    chain, cursor = [], source
    while isinstance(cursor, u.MaterialInstanceConstant):
        require(cursor not in chain and len(chain) < 8, 'Unexpected material parent chain')
        chain.append(cursor)
        cursor = cursor.get_editor_property('parent')
    require(isinstance(cursor, u.Material), 'No editable source liquid base')
    mat = clone(cursor, 'M_Storm' + role + 'Base', u.Material)
    derive_graph(mat)
    parent = mat
    for index, original in enumerate(reversed(chain)):
        name = 'MI_Storm' + role if original == source else 'MI_Storm' + role + 'Parent' + str(index)
        instance = clone(original, name, u.MaterialInstanceConstant)
        edit.set_material_instance_parent(instance, parent)
        edit.update_material_instance(instance)
        save(instance)
        parent = instance
    if not chain:
        name = 'MI_Storm' + role
        destination = FOLDER + '/' + name
        parent = lib.load_asset(destination) if lib.does_asset_exist(destination) else tools.create_asset(name, FOLDER,
            u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
        require(parent, 'Could not create storm preset')
        existing = lib.get_metadata_tag(parent, OWNER)
        require(not existing or existing == source.get_path_name(), 'Preserved unrelated storm preset')
        lib.set_metadata_tag(parent, OWNER, source.get_path_name())
        edit.set_material_instance_parent(parent, mat)
        edit.update_material_instance(parent)
        save(parent)
    return dict(profile=profile_path, source=source.get_path_name(), base=mat.get_path_name(), runtime=parent.get_path_name())


def spray_material():
    path = FOLDER + '/M_StormSpray'
    mat = lib.load_asset(path) if lib.does_asset_exist(path) else None
    if mat:
        require(isinstance(mat, u.Material) and lib.get_metadata_tag(mat, OWNER) == 'procedural_spray:1', 'Preserved custom storm spray')
        return mat
    mat = tools.create_asset('M_StormSpray', FOLDER, u.Material, u.MaterialFactoryNew())
    require(mat, 'Could not create bounded spray material')
    lib.set_metadata_tag(mat, OWNER, 'procedural_spray:1')
    mat.set_editor_properties({'blend_mode': u.BlendMode.BLEND_TRANSLUCENT, 'shading_model': u.MaterialShadingModel.MSM_DEFAULT_LIT,
        'translucency_lighting_mode': u.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING,
        'used_with_instanced_static_meshes': True})
    tint = parameter(mat, 'FoamColor', (.72, .49, .24, 1.), True)
    require(edit.connect_material_property(tint, 'RGB', u.MaterialProperty.MP_BASE_COLOR), 'Spray color link failed')
    opacity = node(mat, u.MaterialExpressionConstant, -250, 150, r=.78)
    output(opacity, u.MaterialProperty.MP_OPACITY)
    rough = node(mat, u.MaterialExpressionConstant, -250, 300, r=.16)
    output(rough, u.MaterialProperty.MP_ROUGHNESS)
    errors = list(edit.recompile_material(mat))
    require(not errors, 'Spray material compile failed: ' + str(errors))
    save(mat)
    return mat


def main():
    require(Path(u.Paths.get_project_file_path()).name == 'MessControl.uproject', 'Wrong project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Preserve the existing PIE')
    dirty = [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    require(not any(p.startswith(FOLDER + '/') for p in dirty), 'Preserve unsaved storm authoring before running')
    source_files = [p for p in Path(u.Paths.project_content_dir()).rglob('*.uasset')
                    if '/Cataclysm/Storm/' not in p.as_posix() and ('Coffee' in p.name or 'Cola' in p.name or 'Liquid' in p.name)]
    before = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files}
    result = [derive_profile('/Game/Data/DA_CoffeeWater', 'Coffee'), derive_profile('/Game/Data/DA_ColdColaLiquid', 'Cola')]
    spray = spray_material()
    after = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files}
    require(before == after, 'A source liquid package changed')
    report = dict(materials=result, spray=spray.get_path_name(), constants=CONSTANTS,
        graph_version='4', stream_parameters=['StormFrontDistance','StormTailDistance','StormFrontWidth',
            'StormDrainRadius','StormDrainDepth','StormWetDepth','StormActivation','StormOpticalSpeed'],
        stream_contract='Full mouth rectangle coverage; arrival controls depth; directional front/tail, micro plus macro waves, continuous localized throat depression and inward foam',
        native_math_sha256=hashlib.sha256(HEADER.read_bytes()).hexdigest(), source_packages_unchanged=True,
        source_package_hashes=before, saved_folder=FOLDER, dirty_before=dirty,
        dirty_after=[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
        validation='Materials compiled/saved; actual gameplay CPU/GPU height, coverage and cost await solo capture')
    target = ROOT / 'Saved/Checks/CataclysmStorm'
    target.mkdir(parents=True, exist_ok=True)
    (target / 'MaterialReport.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('CATACLYSM_STORM_SAVED ' + json.dumps(report))


if __name__ == '__main__':
    main()
