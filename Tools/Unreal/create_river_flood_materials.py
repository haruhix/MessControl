"""Create separate river flood optics while preserving the saved legacy pool assets.

Run in Unreal Editor through remote_python.py. The procedural river mesh owns
height, crest and surface normals; these materials add optics and flowing detail.
"""
from hashlib import sha256
from pathlib import Path
import json
import unreal as u


lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
folder = '/Game/Gameplay/Liquid/River'
project = Path(u.Paths.project_dir()).resolve()
sources = {
    'M_RiverLiquidSurface': '/Game/Gameplay/Liquid/Stylized/M_StylizedLiquidSurface',
    'M_RiverClearWaterSurface': '/Game/Gameplay/Liquid/Stylized/M_ClearLiquidSurface',
    'MI_RiverCoffee': '/Game/Gameplay/Liquid/Stylized/MI_StylizedCoffee',
    'MI_RiverWater': '/Game/Gameplay/Liquid/Stylized/MI_ClearWater',
    'MI_RiverCola': '/Game/Gameplay/Cold/MI_ColdColaSurface',
}
HEIGHT = 'Shared height: ripples, expanding impact front, inlet mound, throat depression'
NORMAL = 'Analytic surface slopes and directional small ripples'
FOAM = 'Advected microfoam, impact froth, wave crest and player wakes'
OPACITY = 'Wet front expands from inlet; softened arena and object intersections'
COVERAGE = 'Visible liquid footprint and contact fade'
FLOW_UV = 'World flow UV, broad ripples'


def saved_hash(path):
    return sha256((project / 'Content' / (path.removeprefix('/Game/') + '.uasset')).read_bytes()).hexdigest()


def save(asset):
    if not lib.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError('Cannot save ' + asset.get_path_name())


def clone(source, name):
    path = folder + '/' + name
    asset = lib.load_asset(path) if lib.does_asset_exist(path) else lib.duplicate_asset(source, path)
    if not asset:
        raise RuntimeError('Cannot duplicate ' + source + ' to ' + path)
    return asset


def expressions(mat):
    return list(edit.get_material_expressions(mat))


def described(mat, name):
    found = [n for n in expressions(mat) if isinstance(n, u.MaterialExpressionCustom)
             and n.get_editor_property('description') == name]
    if len(found) != 1:
        raise RuntimeError('Expected one custom expression ' + name + ' in ' + mat.get_path_name())
    return found[0]


def parameter(mat, name, value):
    cls = u.MaterialExpressionVectorParameter if isinstance(value, tuple) else u.MaterialExpressionScalarParameter
    found = [n for n in expressions(mat) if isinstance(n, cls)
             and str(n.get_editor_property('parameter_name')) == name]
    if len(found) > 1:
        raise RuntimeError('Duplicate parameter ' + name)
    node = found[0] if found else edit.create_material_expression(mat, cls)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', u.LinearColor(*value) if isinstance(value, tuple) else value)
    node.set_editor_property('group', 'River flood')
    return node


def connect_input(node, name, source):
    entries = list(node.get_editor_property('inputs'))
    if name not in [str(i.get_editor_property('input_name')) for i in entries]:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', name)
        entries.append(entry)
        node.set_editor_property('inputs', entries)
    output = 'RGBA' if isinstance(source, u.MaterialExpressionVectorParameter) else ''
    if not edit.connect_material_expressions(source, output, node, name):
        raise RuntimeError('Cannot connect river input ' + name)


front_code = '''
float2 flow=RiverDirection.xy/max(length(RiverDirection.xy),.001);
float2 side=float2(-flow.y,flow.x);
float along=dot(P.xy-RiverOrigin.xy,flow), across=dot(P.xy-RiverOrigin.xy,side);
// Exact lateral phase used by AMCCoffeeFlood::RiverFrontAt and its CPU mesh.
float shift=sin(across*.0035+RiverCycleTime*1.6)*75
           +sin(across*.0081-RiverCycleTime*2.2)*32;
float front=RiverFront+shift;
'''
wet_code = front_code + '''
float edge=max(RiverEdge,1);
float wet=smoothstep(RiverTail+shift-edge,RiverTail+shift+edge,along)
         *(1-smoothstep(front-edge,front+edge,along));
'''


def author_master(name):
    mat = clone(sources[name], name)
    if not isinstance(mat, u.Material):
        raise RuntimeError('River master has unexpected class: ' + mat.get_path_name())
    params = {
        'RiverDirection': parameter(mat, 'RiverDirection', (1, 0, 0, 0)),
        'RiverOrigin': parameter(mat, 'RiverOrigin', (-1400, 0, 0, 0)),
        'RiverFront': parameter(mat, 'RiverFront', 2400.0),
        'RiverTail': parameter(mat, 'RiverTail', -400.0),
        'RiverEdge': parameter(mat, 'RiverEdge', 100.0),
        'RiverCycleTime': parameter(mat, 'RiverCycleTime', 0.0),
    }
    described(mat, HEIGHT).set_editor_property('code', 'return float3(0,0,0);')
    mat.set_editor_property('tangent_space_normal', False)
    normal_ws = next((n for n in expressions(mat) if isinstance(n, u.MaterialExpressionVertexNormalWS)), None)
    if normal_ws is None:
        normal_ws = edit.create_material_expression(mat, u.MaterialExpressionVertexNormalWS)
    normal = described(mat, NORMAL)
    normal.set_editor_property('code', '''
float2 flow=RiverDirection.xy/max(length(RiverDirection.xy),.001);
float2 across=float2(-flow.y,flow.x);
// CPU vertex normals already contain the tongue slope, crest and broad ripples.
float along=dot(P.xy-RiverOrigin.xy,flow), lateral=dot(P.xy-RiverOrigin.xy,across);
float churn=sin(along*.041-WaterTime*9+sin(lateral*.023+WaterTime*2)*2);
float2 detail=(N1.xy+float2(-N2.y,N2.x)*.65
              +float2(churn,cos(lateral*.039+along*.017-WaterTime*7))*.22)*NormalDetail;
return normalize(N-float3(flow*detail.x+across*detail.y,0));
''')
    connect_input(normal, 'N', normal_ws)
    connect_input(normal, 'RiverDirection', params['RiverDirection'])
    connect_input(normal, 'RiverOrigin', params['RiverOrigin'])
    uv = described(mat, FLOW_UV)
    uv.set_editor_property('code', '''
float2 flow=RiverDirection.xy/max(length(RiverDirection.xy),.001);
float2 across=float2(-flow.y,flow.x);
float2 offset=P.xy-RiverOrigin.xy;
float2 uv=float2(dot(offset,flow)-WaterTime*FlowSpeed,dot(offset,across))/max(TileSize,1);
// Advected eddies break up parallel bands without changing the physical height.
float2 eddy=float2(sin(uv.y*2.4+WaterTime*1.9),cos(uv.x*1.7-WaterTime*1.4));
return uv+eddy*.16+float2(sin(uv.y*.85-WaterTime*.8)*.23,0);
''')
    for key in ('RiverDirection', 'RiverOrigin'):
        connect_input(uv, key, params[key])
    foam = described(mat, FOAM)
    foam.set_editor_property('code', front_code + '''
float distance=along-front+80;
float band=distance/(distance>0?155:270);
float cloud=smoothstep(.28,.73,Cells.g);
float rim=pow(saturate(1-Shore),.72)*(.35+.65*cloud);
float breaking=.50+.50*sin(across*.005-RiverCycleTime*3.1);
float crest=exp(-band*band)*(.55+.45*cloud)*(.75+.25*breaking);
float behind=1-smoothstep(-180,-45,along-front);
float wash=behind*exp(-abs(along-front+230)/650);
float streak=pow(saturate(.5+.5*sin(across*.041+sin(along*.011-WaterTime*3)*2+Cells.b*4)),5);
float churn=wash*(streak*.32+smoothstep(.45,.8,Cells.g)*.28);
float wake=0; float4 points[4]={Wake0,Wake1,Wake2,Wake3};
[unroll] for(int i=0;i<4;i++) {
    [branch] if(points[i].z>.001) {
        float wd=length(P.xy-points[i].xy);
        wake+=pow(saturate(.5+.5*sin(wd*.085-WaterTime*5+Cells.b)),5)
             *exp(-wd/95)*smoothstep(12,28,wd)*points[i].z*points[i].w;
    }
}
float islands=pow(saturate((Cells.g-.58)*3),2)*smoothstep(.2,.7,Cells.b)*.10;
return saturate(FoamAmount*(rim*.65+crest*1.12+churn+WakeStrength*wake+islands));
''')
    for key in ('RiverDirection', 'RiverOrigin', 'RiverFront', 'RiverCycleTime'):
        connect_input(foam, key, params[key])
    opacity = described(mat, OPACITY)
    opacity.set_editor_property('code', wet_code + '''
return saturate(lerp(ShallowOpacity,DeepOpacity,Depth)+Fresnel*.10+Foam*.2)*Edge*wet;
''')
    for key, source in params.items():
        connect_input(opacity, key, source)
    if name == 'M_RiverClearWaterSurface':
        coverage = described(mat, COVERAGE)
        coverage.set_editor_property('code', wet_code + 'return saturate(Edge*wet);')
        for key, source in params.items():
            connect_input(coverage, key, source)
    # C++ controls these compatibility uniforms as well; the linear river has no
    # radial inlet mound, vortex, jet or material-side displacement.
    for key, default in [('Filling', 1.0), ('JetAmount', 0.0), ('DrainAmount', 0.0)]:
        parameter(mat, key, default)
    edit.layout_material_expressions(mat)
    errors = edit.recompile_material(mat)
    if errors:
        raise RuntimeError('River material compilation failed: ' + str(errors))
    save(mat)
    return mat


def author_instance(name, parent):
    mi = clone(sources[name], name)
    source = lib.load_asset(sources[name])
    if not isinstance(mi, u.MaterialInstanceConstant):
        raise RuntimeError('River preset has unexpected class: ' + mi.get_path_name())
    # Preserve every authored optical override, including texture selections.
    for prop in ('scalar_parameter_values', 'vector_parameter_values', 'texture_parameter_values'):
        mi.set_editor_property(prop, source.get_editor_property(prop))
    edit.set_material_instance_parent(mi, None)
    edit.set_material_instance_parent(mi, parent)
    # River-only presets: faster churning detail and readable breaking whitewater.
    for key, value in dict(OpticalFlowSpeed=260.0, RippleTileSize=175.0,
                          NormalDetail=.40, FoamAmount=1.2, ShoreFoamWidth=48.0,
                          Roughness=.14, HighlightPower=180.0).items():
        edit.set_material_instance_scalar_parameter_value(mi, key, value)
    foam_colors = {'MI_RiverCoffee': (.95, .83, .63, 1),
                   'MI_RiverWater': (.93, .99, 1, 1),
                   'MI_RiverCola': (.82, .59, .36, 1)}
    edit.set_material_instance_vector_parameter_value(mi, 'FoamColor', u.LinearColor(*foam_colors[name]))
    edit.update_material_instance(mi)
    save(mi)
    return mi


assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before creating river materials'
dirty = {p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
protected = set(sources.values()) | {folder + '/' + name for name in sources}
if dirty & protected:
    raise RuntimeError('Preserving unsaved liquid edits: ' + json.dumps(sorted(dirty & protected)))
for source in sources.values():
    if not lib.does_asset_exist(source):
        raise RuntimeError('Missing saved liquid source: ' + source)
before = {source: saved_hash(source) for source in sources.values()}
lib.make_directory(folder)
dense = author_master('M_RiverLiquidSurface')
clear = author_master('M_RiverClearWaterSurface')
presets = {
    'coffee': author_instance('MI_RiverCoffee', dense).get_path_name(),
    'water': author_instance('MI_RiverWater', clear).get_path_name(),
    'cola': author_instance('MI_RiverCola', dense).get_path_name(),
}
after = {source: saved_hash(source) for source in sources.values()}
if before != after:
    raise RuntimeError('Legacy liquid package unexpectedly changed')
remaining_dirty = {p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
if remaining_dirty != dirty:
    raise RuntimeError('Unexpected dirty packages after river material authoring: '
                       + json.dumps(sorted(remaining_dirty - dirty)))
report = dict(masters=[dense.get_path_name(), clear.get_path_name()], presets=presets,
              legacy_hashes_unchanged=after, procedural_mesh_owns_height=True,
              river_parameters=['RiverDirection', 'RiverOrigin', 'RiverFront', 'RiverTail', 'RiverEdge', 'RiverCycleTime'])
Path(u.Paths.project_saved_dir(), 'RiverFloodMaterials.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('MC_RIVER_MATERIALS_PASS ' + json.dumps(report))
