"""Transient architecture-B coffee blended into the real surface material.

Root owns all Engine execution. build_tongue() clones the live original graph,
rewires every expression/output inside /Engine/Transient, and adds the accepted
rev5 coffee PBR inputs. Original tongue cutouts remain untouched. Continuous
Edge2 coverage blends the inputs instead of drawing/dithering a separate mesh.
No save, PIE launch, source build, mesh edits or native mask assignments.

The tooth pair is a separate FUTURE prototype with an explicit shared synthetic
ellipsoid domain and16cubed atlas. It does not port native GrimeRelief islands.
An all255 native GrimeMask alone is not treated as a visible coating domain.
"""
import ast
import hashlib
import json
import runpy
import time
from pathlib import Path
import unreal as u
from editor_toolset.toolsets.material import MaterialTools

ROOT = Path(u.Paths.project_dir()).resolve()
REVISION = 'coffee_surface_blend_lab:1'
TONGUE_SOURCE = '/Game/Gameplay/Arena/Pressure/MI_Pressure_Soft'
TONGUE_BASE = '/Game/Gameplay/Arena/M_TonguePain'
appearance_path = ROOT/'Tools/Unreal/author_coffee_stain_film_v3.py'
edge_path = ROOT/'Tools/Unreal/author_coffee_stain_edges_v1.py'
appearance = runpy.run_path(str(appearance_path), run_name='surface_blend_appearance_support')
edges = runpy.run_path(str(edge_path), run_name='surface_blend_edges_support')
Graph, require = appearance['Graph'], appearance['require']
edit, lib = u.MaterialEditingLibrary, u.EditorAssetLibrary

COPY_PROPERTIES = ('material_domain', 'blend_mode', 'shading_model', 'use_material_attributes',
    'tangent_space_normal', 'two_sided', 'opacity_mask_clip_value', 'subsurface_profile',
    'dither_opacity_mask')


def dirty_packages():
    return dict(content=sorted(p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()),
                maps=sorted(p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


def disk_packages():
    names = ('Content/Gameplay/Arena/M_TonguePain.uasset',
        'Content/Gameplay/Arena/Pressure/MI_Pressure_Soft.uasset',
        'Content/Art/Materials/M_ArenaTooth.uasset',
        'Content/Cataclysm/Jelly/M_CataclysmPaintTrail.uasset')
    return {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
            for name in names if (ROOT/name).is_file()}


def path(obj):
    return obj.get_path_name() if obj is not None else None


def base_material(interface):
    current, seen = interface, set()
    while not isinstance(current, u.Material):
        require(current is not None and path(current) not in seen, 'Invalid material parent chain')
        seen.add(path(current))
        current = current.get_editor_property('parent')
    return current


def material_properties():
    # Query every exposed MP output, preserving connected non-coffee outputs,
    # including opacity, AO and customized UVs omitted from short inventories.
    return [(name, getattr(u.MaterialProperty, name)) for name in dir(u.MaterialProperty)
            if name.startswith('MP_') and name not in ('MP_MAX', 'MP_MAXIMUM')]


def graph_snapshot(material):
    rows = []
    for node in edit.get_material_expressions(material):
        links = []
        sources = []
        for entry in MaterialTools.get_expression_inputs(material, node):
            source = entry.expression
            links.append(dict(input=str(entry.input_name), source=path(source), output=str(entry.output_name)))
            if source is not None:
                sources.append(path(source))
        # The Engine helper returns the FIRST matching source output. It cannot
        # disambiguate two pins fed from different outputs of the same source.
        # The observed tongue graph has none; fail rather than guess later.
        require(len(sources) == len(set(sources)), 'Ambiguous repeated source pins: '+path(node))
        rows.append(dict(node=path(node), kind=node.get_class().get_name(), links=links))
    outputs = {}
    for name, member in material_properties():
        node = edit.get_material_property_input_node(material, member)
        if node is not None:
            outputs[name] = dict(node=path(node), output=str(
                edit.get_material_property_input_node_output_name(material, member)))
    return dict(nodes=rows, outputs=outputs)


def clone_material_graph(source_interface, expected_base=None):
    """Clone a verified legacy graph without references back to source nodes."""
    require(source_interface is not None, 'Missing original material interface')
    source = base_material(source_interface)
    if expected_base:
        require(path(source).split('.')[0] == expected_base, 'Unexpected original material parent')
    require(not source.get_editor_property('use_material_attributes')
            and edit.get_material_property_input_node(source, u.MaterialProperty.MP_FRONT_MATERIAL) is None,
            'This scoped helper requires the live-observed legacy pin graph, not Substrate/attributes')
    require(not edit.get_static_switch_parameter_names(source_interface),
            'Static-switch permutation requires explicit transient MIC support before cloning')
    original = graph_snapshot(source)
    material = u.new_object(u.Material, name='MCCoffeeSurfaceBlend_'+str(time.time_ns()))
    require(path(material).startswith('/Engine/Transient.'), 'Clone must live in transient package')
    copied_properties = {}
    for name in COPY_PROPERTIES:
        value = source.get_editor_property(name)
        material.set_editor_property(name, value)
        copied_properties[name] = str(value)
    mapping = {}
    for node in edit.get_material_expressions(source):
        cloned = edit.duplicate_material_expression(material, None, node)
        require(cloned is not None and cloned.get_outer() == material, 'Expression clone has wrong outer')
        mapping[path(node)] = cloned
    for row in original['nodes']:
        target = mapping[row['node']]
        for link in row['links']:
            if link['source'] is not None:
                require(link['source'] in mapping, 'Unexpected external expression reference')
                require(edit.connect_material_expressions(mapping[link['source']], link['output'],
                    target, link['input']), 'Clone expression reconnect failed: '+str(link))
    outputs = {}
    for name, member in material_properties():
        if name in original['outputs']:
            link = original['outputs'][name]
            require(edit.connect_material_property(mapping[link['node']], link['output'], member),
                    'Clone output reconnect failed: '+name)
            outputs[name] = (mapping[link['node']], link['output'])
    inverse = {path(clone): source_path for source_path, clone in mapping.items()}
    after = graph_snapshot(material)
    normalized = dict(nodes=[], outputs={})
    for row in after['nodes']:
        require(row['node'] in inverse, 'Unexpected clone node')
        links = []
        for link in row['links']:
            if link['source'] is not None:
                require(link['source'] in inverse, 'Clone still references an original/external expression')
            links.append(dict(input=link['input'], source=inverse.get(link['source']), output=link['output']))
        normalized['nodes'].append(dict(node=inverse[row['node']], kind=row['kind'], links=links))
    for name, link in after['outputs'].items():
        require(link['node'] in inverse, 'Clone output still points outside clone')
        normalized['outputs'][name] = dict(node=inverse[link['node']], output=link['output'])
    require(normalized == original, 'Cloned graph wiring differs from original')
    require(graph_snapshot(source) == original, 'Original graph changed while cloning')
    return material, outputs, dict(source_interface=path(source_interface), source_material=path(source),
        cloned_nodes=len(mapping), graph_sha256=hashlib.sha256(json.dumps(original, sort_keys=True).encode()).hexdigest(),
        all_expression_references_remapped=True, copied_material_properties=copied_properties)


def hlsl(description):
    require(appearance['REVISION'] == 'coffee_stain_film_v3:5', 'Reviewed coffee recipe changed')
    for node in ast.walk(ast.parse(appearance_path.read_text(encoding='utf-8-sig'))):
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == 'custom' \
                and len(node.args) >= 2 and isinstance(node.args[0], ast.Constant) and node.args[0].value == description:
            return ast.literal_eval(node.args[1])
    raise RuntimeError('Missing accepted coffee block: '+description)


def output_or_constant(graph, outputs, name, value):
    if name in outputs:
        return outputs[name]
    if isinstance(value, tuple):
        node = graph.node(u.MaterialExpressionConstant3Vector)
        node.set_editor_property('constant', u.LinearColor(*value, 1.))
    else:
        node = graph.node(u.MaterialExpressionConstant)
        node.set_editor_property('r', value)
    return node


def blend_legacy_surface(material, original_outputs, coffee, alpha, suppress_sss=False):
    """Shared legacy-pin blend; caller is responsible for exact surface domain."""
    graph = Graph(material)
    graph.row = 14000
    defaults = {'MP_BASE_COLOR': (.5, .5, .5), 'MP_NORMAL': (0., 0., 1.),
                'MP_ROUGHNESS': .5, 'MP_SPECULAR': .5, 'MP_METALLIC': 0.,
                'MP_EMISSIVE_COLOR': (0., 0., 0.), 'MP_OPACITY': 1.}
    modified = []
    for name in ('MP_BASE_COLOR', 'MP_ROUGHNESS', 'MP_SPECULAR', 'MP_METALLIC', 'MP_NORMAL'):
        before = output_or_constant(graph, original_outputs, name, defaults[name])
        result = graph.custom('AB preserve original '+name+' outside coffee',
            'return normalize(lerp(Before,Coffee,saturate(Alpha)));' if name == 'MP_NORMAL'
            else 'return lerp(Before,Coffee,saturate(Alpha));',
            dict(Before=before, Coffee=coffee[name], Alpha=alpha),
            3 if name in ('MP_BASE_COLOR', 'MP_NORMAL') else 1)
        graph.out(result, getattr(u.MaterialProperty, name))
        modified.append(name)
    before_emissive = output_or_constant(graph, original_outputs, 'MP_EMISSIVE_COLOR', defaults['MP_EMISSIVE_COLOR'])
    graph.out(graph.custom('AB preserve pain emission outside opaque coffee',
        'return Before*(1-saturate(Alpha));', dict(Before=before_emissive, Alpha=alpha), 3),
        u.MaterialProperty.MP_EMISSIVE_COLOR)
    modified.append('MP_EMISSIVE_COLOR')
    if suppress_sss:
        before_opacity = output_or_constant(graph, original_outputs, 'MP_OPACITY', defaults['MP_OPACITY'])
        graph.out(graph.custom('AB suppress subsurface scattering under coffee',
            'return Before*(1-saturate(Alpha));', dict(Before=before_opacity, Alpha=alpha)),
            u.MaterialProperty.MP_OPACITY)
        modified.append('MP_OPACITY')
    # MP_OPACITY_MASK, WPO, original customized UVs and all unrelated outputs
    # remain exactly those cloned from the original, never coffee alpha.
    return modified


def add_coffee_inputs(material):
    graph = Graph(material)
    graph.row = 8000
    world = graph.node(u.MaterialExpressionWorldPosition)
    local = graph.node(u.MaterialExpressionTransformPosition)
    local.set_editor_property('transform_source_type', u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local.set_editor_property('transform_type', u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    require(edit.connect_material_expressions(world, '', local, ''), 'World-to-surface-local first input connection failed')
    row_u = graph.vector('AB_CoffeeUVRowU', (1./1200., 0., .5))
    row_v = graph.vector('AB_CoffeeUVRowV', (0., 1./1200., .5))
    uv = graph.custom('AB stable localXY footprint coordinates',
        'float3 p=float3(Local.xy,1); return float2(dot(p,RowU.xyz),dot(p,RowV.xyz));',
        dict(Local=local, RowU=row_u, RowV=row_v), 2)
    wipe = graph.node(u.MaterialExpressionTextureObjectParameter)
    wipe.set_editor_property('parameter_name', 'AB_CoffeeMask')
    white = u.load_asset('/Engine/EngineResources/WhiteSquareTexture')
    require(isinstance(white, u.Texture2D), 'Runtime white default texture unavailable')
    wipe.set_editor_property('texture', white)
    wipe.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    finish = graph.scalar('AB_CoffeeFinish', 0.)
    coverage = graph.custom('AB exact independent coffee coverage',
        hlsl('Exact persistent cleanable coffee mask'), dict(UV=uv, Wipe=wipe, Finish=finish))
    mottle = graph.custom('AB accepted quiet espresso micrograin',
        hlsl('Quiet fine espresso grain; no streak or camouflage field'), dict(P=world), 4)
    tint = graph.vector('AB_CoffeeTint', (.055, .014, .003))
    crema = graph.vector('AB_CoffeeCrema', (.18, .060, .013))
    color = graph.custom('AB accepted rev5 espresso color',
        hlsl('Dark thin espresso film; quiet fine grain and narrow amber contact edge'),
        {'Tint': (tint, 'RGB'), 'Crema': (crema, 'RGB'), 'Mottle': mottle, 'Coverage': coverage}, 3)
    normal_code = hlsl('Subtle thin-film meniscus; no large raised pigment islands')
    require(normal_code.count('float e=1.0/64.0;') == 1, 'Accepted normal code changed')
    film_normal = graph.custom('AB accepted rev5 normal in original paint frame',
        normal_code.replace('float e=1.0/64.0;', 'float e=1.0/max(Resolution,1.0);'),
        dict(UV=uv, Wipe=wipe, WorldSize=graph.scalar('AB_CoffeeWorldSize', 1200.),
            Depth=graph.scalar('AB_CoffeeMeniscusCm', .35), Mottle=mottle, Coverage=coverage,
            MicroStrength=graph.scalar('AB_CoffeeMicroNormal', .0025),
            Resolution=graph.scalar('AB_CoffeeMaskResolution', 128.)), 3)
    geometric_normal = graph.node(u.MaterialExpressionVertexNormalWS)
    axis_u = graph.vector('AB_CoffeeAxisUWorld', (1., 0., 0.))
    coffee_world_normal = graph.custom('AB preserve paint world tangent basis on real tongue UV frame', '''
float3 n=normalize(GeomNormal);
float3 t=AxisU.xyz-n*dot(n,AxisU.xyz); t=normalize(t);
float3 b=normalize(cross(n,t));
return normalize(t*Film.x+b*Film.y+n*Film.z);
''', dict(GeomNormal=geometric_normal, AxisU=axis_u, Film=film_normal), 3)
    tangent_normal = graph.node(u.MaterialExpressionTransform)
    tangent_normal.set_editor_property('transform_source_type', u.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    tangent_normal.set_editor_property('transform_type', u.MaterialVectorCoordTransform.TRANSFORM_TANGENT)
    require(edit.connect_material_expressions(coffee_world_normal, '', tangent_normal, ''),
            'Coffee world-to-native-tangent conversion failed')
    rough = graph.custom('AB accepted rev5 roughness',
        hlsl('Plain PBR gloss; live-comparable roughness and quiet contact edge'),
        dict(Mottle=mottle, Coverage=coverage, Roughness=graph.scalar('AB_CoffeeRoughness', .085)))
    soft = graph.custom('AB continuous world-bounded coffee edge', edges['FEATHER_CODE'],
                        dict(P=world, Coverage=coverage))
    alpha = graph.custom('AB restrict coffee to exact local footprint and upward tongue triangles', '''
float domain=all(UV>=0)&&all(UV<=1) ? 1. : 0.;
float3 face=cross(ddx(P),ddy(P));
face*=dot(face,GeomNormal)<0 ? -1. : 1.;
domain*=face.z>1.e-8 ? 1. : 0.;
float alpha=Coverage>=.5 ? 1. : Soft;
return Coverage>0 ? saturate(alpha*domain*Enabled) : 0.;
''', dict(UV=uv, P=world, GeomNormal=geometric_normal, Coverage=coverage, Soft=soft,
          Enabled=graph.scalar('AB_CoffeeEnabled', 1.)))
    outputs = dict(MP_BASE_COLOR=color, MP_NORMAL=tangent_normal, MP_ROUGHNESS=rough,
        MP_SPECULAR=graph.scalar('AB_CoffeeSpecular', .65), MP_METALLIC=graph.scalar('AB_CoffeeMetallic', 0.))
    return outputs, alpha


def select_tongue_shading_models(material, alpha, through_attributes=False):
    """One opt-in native FromMaterialExpression trial, not an SSS claim."""
    graph = Graph(material)
    graph.row = 18000
    profile = graph.node(u.MaterialExpressionShadingModel)
    profile.set_editor_property('shading_model', u.MaterialShadingModel.MSM_SUBSURFACE_PROFILE)
    coffee = graph.node(u.MaterialExpressionShadingModel)
    coffee.set_editor_property('shading_model', u.MaterialShadingModel.MSM_DEFAULT_LIT)
    threshold = graph.node(u.MaterialExpressionConstant)
    threshold.set_editor_property('r', .5)
    choose = graph.node(u.MaterialExpressionIf)
    names = [str(n) for n in edit.get_material_expression_input_names(choose)]
    require('A' in names and 'B' in names, 'Native If threshold pins changed')
    for source, pin in ((alpha, 'A'), (threshold, 'B')):
        require(edit.connect_material_expressions(source, '', choose, pin), 'ShadingModel If threshold failed')
    matches = [(name, coffee if '>' in name or '=' in name else profile)
               for name in names if any(op in name for op in ('>', '<', '='))]
    require(len(matches) == 3, 'Review native If branch pin names: '+str(names))
    for pin, source in matches:
        require(edit.connect_material_expressions(source, '', choose, pin), 'ShadingModel If branch failed')
    material.set_editor_property('shading_model', u.MaterialShadingModel.MSM_FROM_MATERIAL_EXPRESSION)
    if not through_attributes:
        require(hasattr(u.MaterialProperty,'MP_SHADING_MODEL'),
                'MP_ShadingModel is hidden from Python; use the opt-in from_attributes trial')
        graph.out(choose,u.MaterialProperty.MP_SHADING_MODEL)
        return None
    attributes = graph.node(u.MaterialExpressionMakeMaterialAttributes)
    input_names = [str(n) for n in edit.get_material_expression_input_names(attributes)]
    normalized = {''.join(c for c in name.upper() if c.isalnum()):name for name in input_names}
    require('SHADINGMODEL' in normalized,'Native MakeMaterialAttributes ShadingModel input unavailable')
    for name,member in material_properties():
        if name in ('MP_MATERIAL_ATTRIBUTES','MP_FRONT_MATERIAL'):
            continue
        node = edit.get_material_property_input_node(material,member)
        if node is None:
            continue
        key = ''.join(c for c in name.removeprefix('MP_') if c.isalnum())
        require(key in normalized,'Connected root has no MakeMaterialAttributes input: '+name)
        output = str(edit.get_material_property_input_node_output_name(material,member))
        require(edit.connect_material_expressions(node,output,attributes,normalized[key]),
                'Original/blended root did not connect to attributes: '+name)
    require(edit.connect_material_expressions(choose,'',attributes,normalized['SHADINGMODEL']),
            'Per-pixel shading model attribute connection failed')
    graph.out(attributes,u.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
    material.set_editor_property('use_material_attributes',True)
    return attributes


def build_tongue(source_interface=None, shading_model_mode='original_profile'):
    require(Path(u.Paths.get_project_file_path()).stem == 'MessControl', 'Wrong project')
    before_dirty, before_packages = dirty_packages(), disk_packages()
    source_interface = source_interface or u.load_asset(TONGUE_SOURCE)
    material, original_outputs, report = clone_material_graph(source_interface, TONGUE_BASE)
    require(material.get_editor_property('tangent_space_normal'), 'Observed tongue tangent-space contract changed')
    require(material.get_editor_property('shading_model') == u.MaterialShadingModel.MSM_SUBSURFACE_PROFILE,
            'Observed tongue SubsurfaceProfile contract changed')
    require(edit.get_material_property_input_node(material, u.MaterialProperty.MP_WORLD_POSITION_OFFSET) is None,
            'Observed no-WPO tongue contract changed')
    coffee, alpha = add_coffee_inputs(material)
    modified = blend_legacy_surface(material, original_outputs, coffee, alpha, suppress_sss=True)
    require(shading_model_mode in ('original_profile','from_expression','from_attributes'),'Unknown tongue shading mode')
    attributes = None
    if shading_model_mode in ('from_expression','from_attributes'):
        attributes = select_tongue_shading_models(material,alpha,
            through_attributes=shading_model_mode=='from_attributes')
        modified.append('MP_SHADING_MODEL')
    # Verify the cutout expression is still the original clone, with identical pin.
    if 'MP_OPACITY_MASK' in original_outputs:
        mask, pin = original_outputs['MP_OPACITY_MASK']
        if attributes is None:
            require(edit.get_material_property_input_node(material,u.MaterialProperty.MP_OPACITY_MASK) == mask
                    and str(edit.get_material_property_input_node_output_name(material,u.MaterialProperty.MP_OPACITY_MASK)) == pin,
                    'Original tongue opacity cutouts changed')
        else:
            links = [entry for entry in MaterialTools.get_expression_inputs(material,attributes)
                     if ''.join(c for c in str(entry.input_name).upper() if c.isalnum()) == 'OPACITYMASK']
            require(len(links)==1 and links[0].expression==mask and str(links[0].output_name)==pin,
                    'Original cutout not preserved inside MakeMaterialAttributes')
    for node in edit.get_material_expressions(material):
        for entry in MaterialTools.get_expression_inputs(material, node):
            require(entry.expression is None or entry.expression.get_outer() == material,
                    'B graph references a node outside the transient clone')
    errors = list(edit.recompile_material(material))
    require(not errors, 'Transient blended tongue compile failed: '+str(errors))
    require(dirty_packages() == before_dirty and disk_packages() == before_packages,
            'Transient blend authoring changed original packages or user dirty state')
    report.update(revision=REVISION, material=path(material), compiled=True, modified_outputs=modified,
        original_opacity_mask_preserved=True, wipe_texture_reads=5, extra_coffee_draws=0,
        coffee_style='Accepted rev5 constants/HLSL, mask128, world-size1200cm; no custom emission/glints',
        edge_difference='B uses continuous Edge2 alpha input blending; A uses masked temporal DitherTemporalAA separate geometry',
        shading_model_mode=shading_model_mode,
        subsurface_limitation=('Original profile retained; per-pixel DefaultLit at coffee alpha>=.5, original SubsurfaceProfile below. Attributes blend continuously while the shading-model boundary selects at.5; visual lighting parity still requires actual gameplay review.'
            if shading_model_mode=='from_attributes' else
            'Original SubsurfaceProfile identity retained; opacity is suppressed under coffee. Engine DefaultLit fallback is permutation-dependent; exact DefaultLit lighting parity is not claimed.'),
        projection_contract='Tongue localXY is stable under native localZ deformation. configure_tongue_mid rejects any footprint coupling from localZ; no arbitrary UV warp or neighboring-surface projection.',
        future_tooth_contract='Separate synthetic prototype: same explicit object-local ellipsoid and16cubed erasure atlas in A/B; not a migration of native two-island GrimeRelief or a native gameplay test',
        protected_packages=before_packages, dirty_packages=before_dirty,
        source_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in (Path(__file__), appearance_path, edge_path)})
    return material, report


def configure_tongue_mid(world, material, source_interface, mask_texture,
                         surface_transform, footprint_transform, world_size_cm=1200., resolution=128):
    """Copy effective original uniforms and set the exact stable footprint map."""
    require(path(material).startswith('/Engine/Transient.'), 'Expected transient B material')
    require(float(world_size_cm) > 0 and int(resolution) in (64, 128, 256), 'Invalid independent mask dimensions')
    original = source_interface if isinstance(source_interface,u.MaterialInstanceDynamic) else \
        u.MaterialLibrary.create_dynamic_material_instance(world,source_interface)
    mid = u.MaterialLibrary.create_dynamic_material_instance(world, material)
    copied = dict(scalars={}, vectors={}, textures={})
    for kind, names in (('scalars', edit.get_scalar_parameter_names(source_interface)),
                        ('vectors', edit.get_vector_parameter_names(source_interface)),
                        ('textures', edit.get_texture_parameter_names(source_interface))):
        for entry in names:
            name = str(entry)
            getter = {'scalars': 'get_scalar_parameter_value', 'vectors': 'get_vector_parameter_value',
                      'textures': 'get_texture_parameter_value'}[kind]
            setter = {'scalars': 'set_scalar_parameter_value', 'vectors': 'set_vector_parameter_value',
                      'textures': 'set_texture_parameter_value'}[kind]
            value = getattr(original, getter)(name)
            if kind != 'textures' or value is not None:
                getattr(mid, setter)(name, value)
            copied[kind][name] = float(value) if kind == 'scalars' else (
                [float(value.r), float(value.g), float(value.b), float(value.a)] if kind == 'vectors' else path(value))
    def mapped(local):
        return footprint_transform.inverse_transform_location(surface_transform.transform_location(u.Vector(*local)))
    origin = mapped((0., 0., 0.))
    dx, dy, dz = (mapped(axis)-origin for axis in ((1.,0.,0.),(0.,1.,0.),(0.,0.,1.)))
    require(abs(dz.x) < 1.e-5 and abs(dz.y) < 1.e-5,
            'Footprint depends on deformed localZ; require an explicit rest-space mapping before B')
    row_u = (dx.x/world_size_cm, dy.x/world_size_cm, origin.x/world_size_cm+.5)
    row_v = (dx.y/world_size_cm, dy.y/world_size_cm, origin.y/world_size_cm+.5)
    mid.set_vector_parameter_value('AB_CoffeeUVRowU', u.LinearColor(*row_u, 0.))
    mid.set_vector_parameter_value('AB_CoffeeUVRowV', u.LinearColor(*row_v, 0.))
    world_axis_u = u.MathLibrary.transform_direction(footprint_transform, u.Vector(1., 0., 0.))
    mid.set_vector_parameter_value('AB_CoffeeAxisUWorld', u.LinearColor(world_axis_u.x, world_axis_u.y, world_axis_u.z, 0.))
    mid.set_texture_parameter_value('AB_CoffeeMask', mask_texture)
    mid.set_scalar_parameter_value('AB_CoffeeMaskResolution', float(resolution))
    mid.set_scalar_parameter_value('AB_CoffeeWorldSize', float(world_size_cm))
    require(mid.get_texture_parameter_value('AB_CoffeeMask') == mask_texture
            and abs(mid.get_scalar_parameter_value('AB_CoffeeMaskResolution')-resolution) < 1.e-6,
            'B candidate mask/resolution readback failed')
    return mid, dict(copied_effective_original_parameters=copied, row_u=row_u, row_v=row_v,
        local_z_coupling=[float(dz.x), float(dz.y)], mask=path(mask_texture),
        resolution=int(resolution), world_size_cm=float(world_size_cm), normal_step=1./resolution)


def tooth_coffee_inputs(material, coat=False):
    """Shared synthetic FUTURE domain; not a replacement for native grime."""
    graph = Graph(material)
    graph.row = 8000
    actual_world = graph.node(u.MaterialExpressionWorldPosition)
    if coat:
        actual_world.set_editor_property('world_position_shader_offset',
            u.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    rest = graph.node(u.MaterialExpressionTransformPosition)
    rest.set_editor_property('transform_source_type',u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    rest.set_editor_property('transform_type',u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    require(edit.connect_material_expressions(actual_world,'',rest,''),
            'Static tooth world-to-local position connection failed')
    world = graph.node(u.MaterialExpressionTransformPosition)
    world.set_editor_property('transform_source_type', u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    world.set_editor_property('transform_type', u.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    require(edit.connect_material_expressions(rest, '', world, ''), 'Tooth rest-position world transform failed')
    vectors = {name: graph.vector(name, value) for name, value in (
        ('AB_ToothMin', (0.,0.,0.)), ('AB_ToothSize', (1.,1.,1.)),
        ('AB_ToothScale', (1.,1.,1.)), ('AB_ToothDomainCenter', (0.,0.,0.)),
        ('AB_ToothDomainU', (1.,0.,0.)), ('AB_ToothDomainV', (0.,1.,0.)),
        ('AB_ToothDomainN', (0.,0.,1.)), ('AB_ToothDomainRadii', (1.,1.,1.)))}
    wipe = graph.node(u.MaterialExpressionTextureObjectParameter)
    wipe.set_editor_property('parameter_name', 'AB_ToothWipe')
    white = u.load_asset('/Engine/EngineResources/WhiteSquareTexture')
    require(isinstance(white, u.Texture2D), 'Runtime tooth default texture unavailable')
    wipe.set_editor_property('texture', white)
    wipe.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    coverage = graph.custom('AB tooth shared analytic ellipsoid x native16cubed atlas', '''
float3 relative=P-Center.xyz;
float3 q=float3(dot(relative,U.xyz),dot(relative,V.xyz),dot(relative,N.xyz))/max(Radii.xyz,1.e-5);
float domain=1-smoothstep(.8,1.,length(q));
float3 grid=saturate((P-Min.xyz)/max(Size.xyz,1.e-5))*15.;
int3 cell=int3(floor(grid)); float3 f=frac(grid);
float value=0.;
[unroll] for(int k=0;k<2;k++) [unroll] for(int j=0;j<2;j++) [unroll] for(int i=0;i<2;i++)
{
    int3 s=min(cell+int3(i,j,k),15);
    float2 at=float2(s.x+(s.z%4)*16,s.y+(s.z/4)*16);
    float weight=(i?f.x:1-f.x)*(j?f.y:1-f.y)*(k?f.z:1-f.z);
    value+=Texture2DSampleLevel(Wipe,WipeSampler,(at+.5)/64.,0).r*weight;
}
return domain*saturate(value)*(1-saturate(Finish));
''', dict(P=rest, Center=vectors['AB_ToothDomainCenter'], U=vectors['AB_ToothDomainU'],
        V=vectors['AB_ToothDomainV'], N=vectors['AB_ToothDomainN'], Radii=vectors['AB_ToothDomainRadii'],
        Min=vectors['AB_ToothMin'], Size=vectors['AB_ToothSize'], Wipe=wipe,
        Finish=graph.scalar('AB_CoffeeFinish', 0.)))
    stable_cm = graph.custom('AB tooth fixed original local coordinates in physical centimeters',
        'return P*Scale.xyz;', dict(P=rest, Scale=vectors['AB_ToothScale']), 3)
    mottle = graph.custom('AB tooth accepted quiet espresso micrograin',
        hlsl('Quiet fine espresso grain; no streak or camouflage field'), dict(P=stable_cm), 4)
    tint = graph.vector('AB_CoffeeTint', (.055,.014,.003))
    crema = graph.vector('AB_CoffeeCrema', (.18,.060,.013))
    color = graph.custom('AB tooth accepted rev5 espresso color',
        hlsl('Dark thin espresso film; quiet fine grain and narrow amber contact edge'),
        {'Tint': (tint,'RGB'), 'Crema': (crema,'RGB'), 'Mottle':mottle, 'Coverage':coverage}, 3)
    rough = graph.custom('AB tooth accepted rev5 roughness',
        hlsl('Plain PBR gloss; live-comparable roughness and quiet contact edge'),
        dict(Mottle=mottle, Coverage=coverage, Roughness=graph.scalar('AB_CoffeeRoughness', .085)))
    vertex_normal = graph.node(u.MaterialExpressionVertexNormalWS)
    vertex_tangent = graph.node(u.MaterialExpressionVertexTangentWS)
    world_normal = graph.custom('AB tooth shared derivative meniscus on original unoffset surface', '''
float3 n=normalize(Normal),dx=ddx(P),dy=ddy(P),rx=cross(dy,n),ry=cross(n,dx);
float determinant=dot(dx,rx);
float3 gradient=(ddx(Coverage)*rx+ddy(Coverage)*ry)*sign(determinant)/max(abs(determinant),1.e-8);
float3 tangent=normalize(Tangent-n*dot(n,Tangent)),bitangent=normalize(cross(n,tangent));
float2 micro=clamp(Mottle.zw*clamp(MicroStrength,0,.006),-.009,.009)*smoothstep(.40,.85,Coverage);
return normalize(n-gradient*clamp(Depth,0,1.2)+tangent*micro.x+bitangent*micro.y);
''', dict(P=world, Normal=vertex_normal, Tangent=vertex_tangent, Coverage=coverage, Mottle=mottle,
            MicroStrength=graph.scalar('AB_CoffeeMicroNormal', .0025),
            Depth=graph.scalar('AB_CoffeeMeniscusCm', .35)), 3)
    normal = graph.node(u.MaterialExpressionTransform)
    normal.set_editor_property('transform_source_type', u.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    normal.set_editor_property('transform_type', u.MaterialVectorCoordTransform.TRANSFORM_TANGENT)
    require(edit.connect_material_expressions(world_normal, '', normal, ''), 'Tooth tangent normal conversion failed')
    soft = graph.custom('AB tooth continuous world-bounded edge', edges['FEATHER_CODE'], dict(P=world,Coverage=coverage))
    alpha = graph.custom('AB tooth exact-zero and fully opaque core gate',
        'return Coverage>0 ? saturate((Coverage>=.5?1.:Soft)*Enabled*ToothEnabled) : 0.;',
        dict(Coverage=coverage, Soft=soft, Enabled=graph.scalar('AB_CoffeeEnabled', 1.),
            ToothEnabled=graph.scalar('AB_ToothEnabled',1.)))
    return dict(MP_BASE_COLOR=color, MP_NORMAL=normal, MP_ROUGHNESS=rough,
        MP_SPECULAR=graph.scalar('AB_CoffeeSpecular', .65), MP_METALLIC=graph.scalar('AB_CoffeeMetallic', 0.)), alpha


def build_tooth_pair(source_interface):
    """A owned static-mesh WPO coat vs B enamel blend; same synthetic field."""
    before_dirty, before_packages = dirty_packages(), disk_packages()
    base = base_material(source_interface)
    require(base.get_editor_property('shading_model') == u.MaterialShadingModel.MSM_DEFAULT_LIT
            and base.get_editor_property('tangent_space_normal'), 'Observed tooth DefaultLit/tangent contract changed')
    require(edit.get_material_property_input_node(base, u.MaterialProperty.MP_WORLD_POSITION_OFFSET) is None,
            'Tooth WPO requires a separate rest-position contract')
    a = u.new_object(u.Material, name='MCCoffeeToothCoatA_'+str(time.time_ns()))
    appearance['reset'](a, True)
    a_coffee, a_alpha = tooth_coffee_inputs(a, coat=True)
    graph = Graph(a)
    graph.row = 17000
    for name, value in a_coffee.items():
        graph.out(value, getattr(u.MaterialProperty,name))
    graph.out(graph.custom('AB tooth coat exact0.6cm world-normal shader offset',
        'return Normal*.6;',dict(Normal=graph.node(u.MaterialExpressionVertexNormalWS)),3),
        u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    dither = graph.node(u.MaterialExpressionMaterialFunctionCall)
    require(dither.set_material_function(u.load_asset(edges['FUNCTION'])), 'Tooth native temporal function unavailable')
    names = [str(n) for n in edit.get_material_expression_input_names(dither)]
    alpha_names = [name for name in names if 'alpha' in name.lower()]
    require(len(alpha_names) == 1, 'Tooth dither alpha input ambiguous')
    require(edit.connect_material_expressions(a_alpha, '', dither, alpha_names[0]), 'Tooth dither connection failed')
    graph.out(graph.custom('AB tooth preserve zero-domain and fully opaque core',
        'return Alpha<=0 ? 0. : (Alpha>=1 ? 1. : Temporal);',
        dict(Alpha=a_alpha,Temporal=dither)),u.MaterialProperty.MP_OPACITY_MASK)
    b, original_outputs, clone_report = clone_material_graph(source_interface)
    b_coffee, b_alpha = tooth_coffee_inputs(b, coat=False)
    modified = blend_legacy_surface(b, original_outputs, b_coffee, b_alpha, suppress_sss=False)
    for material in (a,b):
        require(path(material).startswith('/Engine/Transient.'), 'Tooth material must be transient')
        errors = list(edit.recompile_material(material))
        require(not errors, 'Synthetic tooth pair compile failed: '+str(errors))
        for node in edit.get_material_expressions(material):
            for entry in MaterialTools.get_expression_inputs(material,node):
                require(entry.expression is None or entry.expression.get_outer() == material,
                        'Tooth material still references another graph expression')
    require(dirty_packages() == before_dirty and disk_packages() == before_packages,
            'Transient tooth authoring changed packages or user dirty state')
    report = dict(revision=REVISION, A=path(a), B=path(b), clone=clone_report, compiled=True,
        scope='Synthetic FUTURE coffee prototype; not a port of native GrimeRelief islands or gameplay',
        rest_positions='A uses WorldPosition excluding ALL shader offsets transformed into the owned static mesh local frame; B uses the original static no-WPO Visual local frame. Both evaluate the same original localXYZ before A world-normal WPO0.6cm; no UV channels are modified.',
        non_enamel_slots='AB_ToothEnabled0 makes the A masked coat fully invisible on Dentin/Crack slots; default1 for enamel. B original non-enamel slots remain untouched.',
        domain='Same analytic ellipsoid x same16cubed trilinear wipe, packed64x64. Exact0outside domain; component-local assignment excludes neighbors.',
        coffee_style='Accepted rev5 espresso/PBR defaults; anchored physical fine grain; same normal technique in A/B',
        normal_difference='3D coverage gradient from screen derivatives replaces planar four-neighbor samples; same0.35cm meniscus and0.0025 microstrength',
        texture_budget='Eight logical atlas reads per coverage evaluation; A additionally uses native DitherTemporalAA noise. GPU cost unmeasured.',
        edge_difference='A masked temporal coverage, B continuous attribute blend. Same underlying domain and erasure texture.',
        modified_B_outputs=modified, protected_packages=before_packages, dirty_packages=before_dirty,
        source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    return a,b,report


def configure_tooth_mid(world, material, source_interface, mask_texture, parameters):
    """Flat vector-dictionary callback consumed by coffee_tooth_ab_support."""
    require(path(material).startswith('/Engine/Transient.'), 'Expected transient tooth material')
    require(mask_texture.blueprint_get_size_x() == 64 and mask_texture.blueprint_get_size_y() == 64,
            'Tooth wipe must be the16cubed atlas, not a planar128 mask')
    mid = u.MaterialLibrary.create_dynamic_material_instance(world, material)
    reader = source_interface if isinstance(source_interface,u.MaterialInstanceDynamic) else \
        u.MaterialLibrary.create_dynamic_material_instance(world,source_interface)
    copied = {}
    for kind,names in (('scalar',edit.get_scalar_parameter_names(source_interface)),
                       ('vector',edit.get_vector_parameter_names(source_interface)),
                       ('texture',edit.get_texture_parameter_names(source_interface))):
        copied[kind] = {}
        for entry in names:
            name = str(entry)
            value = getattr(reader,'get_'+kind+'_parameter_value')(name)
            if kind != 'texture' or value is not None:
                getattr(mid,'set_'+kind+'_parameter_value')(name,value)
            copied[kind][name] = float(value) if kind == 'scalar' else (
                [float(value.r),float(value.g),float(value.b),float(value.a)] if kind == 'vector' else path(value))
    required = ('AB_ToothMin','AB_ToothSize','AB_ToothScale','AB_ToothDomainCenter',
                'AB_ToothDomainU','AB_ToothDomainV','AB_ToothDomainN','AB_ToothDomainRadii')
    require(all(name in parameters for name in required), 'Explicit synthetic tooth domain parameters required')
    readback = {}
    for name in required:
        value = tuple(float(v) for v in parameters[name])
        require(len(value) == 3, 'Expected localXYZ vector '+name)
        mid.set_vector_parameter_value(name,u.LinearColor(*value,0.))
        actual = mid.get_vector_parameter_value(name)
        readback[name] = [float(actual.r),float(actual.g),float(actual.b)]
        require(all(abs(a-b) <= max(1.e-5,abs(b)*1.e-6) for a,b in zip(readback[name],value)),
                'Tooth domain MID readback failed: '+name)
    mid.set_texture_parameter_value('AB_ToothWipe',mask_texture)
    require(mid.get_texture_parameter_value('AB_ToothWipe') == mask_texture,'Tooth wipe texture readback failed')
    return mid,dict(copied_effective_original_parameters=copied,domain_vectors=readback,wipe=path(mask_texture))


if __name__ == '__main__':
    material, report = build_tongue()
    print(json.dumps(report, indent=2))
