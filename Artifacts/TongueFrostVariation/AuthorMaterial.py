import json
from pathlib import Path
import unreal as u

e, a = u.MaterialEditingLibrary, u.EditorAssetLibrary
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
mat = u.load_asset('/Game/Gameplay/Cold/Frost/M_TongueFrost')
assert mat and mat.get_blend_mode() == u.BlendMode.BLEND_MASKED
nodes = list(e.get_material_expressions(mat))
params = {str(n.get_editor_property('parameter_name')): n for n in nodes
    if isinstance(n, (u.MaterialExpressionScalarParameter, u.MaterialExpressionVectorParameter))}
assert 'Ice Patch Coverage' not in params, 'Variation is already authored; inspect before changing it.'
dither = e.get_material_property_input_node(mat, u.MaterialProperty.MP_OPACITY_MASK)
old_front = e.get_material_property_input_node(mat, u.MaterialProperty.MP_FRONT_MATERIAL)
assert isinstance(old_front, u.MaterialExpressionMaterialFunctionCall)
front_inputs = dict(zip([str(v) for v in e.get_material_expression_input_names(old_front)], e.get_inputs_for_material_expression(mat, old_front)))
original_normal = front_inputs['Normal']
growth = next(n for n in nodes if isinstance(n, u.MaterialExpressionCustom) and str(n.get_editor_property('description')) == 'Frost crystals grow with the replicated freeze meter')
warm = next(n for n in nodes if isinstance(n, u.MaterialExpressionCustom) and str(n.get_editor_property('description')) == 'Smooth frost fade outside the current and next warm circles')
warm_names = [str(v) for v in e.get_material_expression_input_names(warm)]
warm_sources = list(e.get_inputs_for_material_expression(mat, warm))
coverage = list(e.get_inputs_for_material_expression(mat, dither))[0]
assert isinstance(coverage, u.MaterialExpressionMultiply)
out = Path(u.Paths.project_dir())/'Artifacts/TongueFrostVariation'
out.mkdir(parents=True, exist_ok=True)
backup_path = '/Game/Gameplay/Cold/Frost/M_TongueFrost_BeforePatchVariation'
if a.does_asset_exist(backup_path):
    backup = u.load_asset(backup_path)
else:
    backup = a.duplicate_asset(mat.get_path_name().split('.')[0], backup_path)
    assert backup and a.save_loaded_asset(backup, only_if_is_dirty=False)
u.get_editor_subsystem(u.AssetEditorSubsystem).close_all_editors_for_asset(mat)

def node(cls, x, y, **values):
    n = e.create_material_expression(mat, cls, x, y)
    assert n
    n.set_editor_properties(values)
    return n

def scalar(name, value, x, y, group, desc, lo=0.0, hi=1.0):
    return node(u.MaterialExpressionScalarParameter, x, y, parameter_name=name,
        default_value=value, group=group, desc=desc, slider_min=lo, slider_max=hi)

def link(src, output, dst, pin):
    assert e.connect_material_expressions(src, output, dst, pin), (src.get_name(), dst.get_name(), pin)

def custom(description, code, output_type, sources, x, y):
    inputs = []
    for name, src, output in sources:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    n = node(u.MaterialExpressionCustom, x, y, inputs=inputs, code=code,
        description=description, output_type=output_type)
    for name, src, output in sources:
        link(src, output, n, name)
    return n

def channel(src, name, x, y):
    n = node(u.MaterialExpressionComponentMask, x, y, r=name=='r', g=name=='g', b=name=='b', a=name=='a')
    link(src, '', n, '')
    return n

with u.ScopedEditorTransaction('Create uneven Substrate ice plates and clumped frost on the tongue'):
    uv = node(u.MaterialExpressionTextureCoordinate, -4350, 4700, coordinate_index=0)
    scale = scalar('Ice Patch Scale', 7.0, -4350, 4850, 'Patch Coverage',
        'Large organic islands in the authored tongue UVs. Lower values make wider ice patches.', .5, 16.0)
    offset = node(u.MaterialExpressionVectorParameter, -4350, 5020,
        parameter_name='Pattern Offset', group='Patch Coverage', default_value=u.LinearColor(.31, 4.7, 0, 1))
    field_code = '''struct TongueIceField
{
    float hash(float2 p)
    {
        float3 q=frac(float3(p.x,p.y,p.x)*0.1031);
        q+=dot(q,q.yzx+33.33);
        return frac((q.x+q.y)*q.z);
    }
    float noise(float2 p)
    {
        float2 i=floor(p), f=frac(p);
        float2 u=f*f*(3.0-2.0*f);
        return lerp(lerp(hash(i),hash(i+float2(1,0)),u.x),
                    lerp(hash(i+float2(0,1)),hash(i+1.0),u.x),u.y);
    }
    float fbm(float2 p)
    {
        return noise(p)*0.65+noise(p*2.07+13.4)*0.25+noise(p*4.13-8.7)*0.10;
    }
};
TongueIceField f;
float2 p=UV*max(Scale,0.1)+Offset.xy;
float2 warp=float2(f.noise(p*0.83+6.2),f.noise(p*0.91-5.4))-0.5;
float2 q=p+warp*1.8;
return float4(f.fbm(q),f.noise(q*2.6+13.4),f.fbm(q*1.42-5.3),f.noise(q*7.0+22.1));'''
    field = custom('Stable UV field: ice islands, frost clumps, thaw breakup and gloss variation', field_code,
        u.CustomMaterialOutputType.CMOT_FLOAT4, [('UV', uv, ''), ('Scale', scale, ''), ('Offset', offset, 'RGB')], -3950, 4710)
    density = scalar('Ice Patch Coverage', .58, -4350, 5270, 'Patch Coverage',
        'Amount of smooth ice islands at full cold. Open tongue remains between islands.', .05, .95)
    softness = scalar('Ice Patch Softness', .075, -4350, 5430, 'Patch Coverage',
        'Soft feather around individual ice plates.', .01, .25)
    clumps = scalar('Frost Clump Coverage', .58, -4350, 5600, 'Patch Coverage',
        'Controls where white frost clusters gather; independent from the glossy ice islands.', .05, .95)
    shape_code = '''float a=saturate(Amount);
if(a<=0.001) return float4(0,0,0,0);
float threshold=lerp(1.08,1.0-clamp(Density,0.02,0.98),a);
float plate=smoothstep(threshold,threshold+max(Softness,0.005),Fields.r);
float region=smoothstep(1.0-saturate(Clumps),1.18-saturate(Clumps),Fields.g);
float rim=4.0*plate*(1.0-plate);
float cluster=smoothstep(0.24,0.57,Fields.r)*saturate(region*0.85+rim*0.65);
float frost=saturate(Crystals)*cluster;
float total=max(plate,frost);
float mix=saturate(frost*1.85/max(total,0.001))*saturate(FrostStrength);
return float4(total,mix,plate,region);'''
    shapes = custom('Growing ice plates with clustered frost and exposed tissue between them', shape_code,
        u.CustomMaterialOutputType.CMOT_FLOAT4, [('Fields', field, ''), ('Amount', params['IceAmount'], ''),
        ('Density', density, ''), ('Softness', softness, ''), ('Clumps', clumps, ''),
        ('Crystals', growth, ''), ('FrostStrength', params['Frost Strength'], '')], -3460, 4760)
    total = channel(shapes, 'r', -2990, 4710)
    frost_mix = channel(shapes, 'g', -2990, 4910)
    shade = channel(field, 'a', -3490, 5470)
    link(total, '', coverage, 'A')
    coverage.set_editor_property('desc', 'Irregular ice plates and clumped crystals, softened by the local warmth field.')

    variation = scalar('Warm Edge Variation', 95.0, -2600, 4550, 'Warm Zone',
        'Uneven thaw fringe in centimetres outside the safe circle. Zero restores a uniform gradient.', 0, 220)
    inputs = []
    for name in warm_names+['Fields', 'Variation']:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    warm_code = '''float edge=max(Variation,0.0);
float variation=saturate(edge/95.0);
float w=max(Width,1.0)*lerp(1.0,lerp(0.45,1.85,Fields.b),variation);
float fringe=edge*(0.12+0.88*Fields.g);
float r=max(Radius,0.0), nr=max(NextRadius,0.0);
float outside=smoothstep(r+fringe,r+fringe+w,length(P.xy-Center.xy));
float nextOutside=smoothstep(nr+fringe,nr+fringe+w,length(P.xy-NextCenter.xy));
return lerp(1.0,outside,saturate(Enabled))*lerp(1.0,nextOutside,saturate(NextEnabled));'''
    warm.set_editor_properties({'inputs': inputs, 'code': warm_code,
        'description': 'Uneven thaw fringe with locally varying softness, outside both gameplay-safe circles'})
    for name, src in zip(warm_names, warm_sources):
        link(src, 'RGB' if name in ['Center','NextCenter'] else '', warm, name)
    link(field, '', warm, 'Fields')
    link(variation, '', warm, 'Variation')

    ice_tint = node(u.MaterialExpressionVectorParameter, -2520, 5000,
        parameter_name='Ice Plate Tint', group='Ice Surface', default_value=u.LinearColor(.12,.30,.42,1))
    ice_rough = scalar('Ice Roughness', .12, -2520, 5320, 'Ice Surface', 'Gloss of the smoother ice islands.', .02, .45)
    frost_rough = scalar('Frost Roughness', .72, -2520, 5540, 'Frost Surface', 'Matte roughness of the white crystals.', .35, .95)
    specular = scalar('Ice F0', .04, -1980, 5520, 'Ice Surface', 'Dielectric specular reflectance of the ice and frost.', .02, .08)
    ice_color = custom('Blue ice changes density and tint across each plate',
        'return Tint*lerp(0.60,1.40,saturate(Shade));', u.CustomMaterialOutputType.CMOT_FLOAT3,
        [('Tint', ice_tint, 'RGB'), ('Shade', shade, '')], -1990, 5010)
    frost_color = custom('White frost varies from translucent-looking blue to dense white clusters',
        'return Tint*lerp(0.58,1.0,saturate(Shade));', u.CustomMaterialOutputType.CMOT_FLOAT3,
        [('Tint', params['Frost Tint'], 'RGB'), ('Shade', shade, '')], -1990, 5720)
    ice_r = custom('Smooth ice with small roughness changes', 'return clamp(Rough+(Shade-0.5)*0.12,0.025,0.45);',
        u.CustomMaterialOutputType.CMOT_FLOAT1, [('Rough', ice_rough, ''), ('Shade', shade, '')], -1980, 5290)
    frost_r = custom('Variable matte frost crystals', 'return clamp(Rough+(Shade-0.5)*0.20,0.35,0.95);',
        u.CustomMaterialOutputType.CMOT_FLOAT1, [('Rough', frost_rough, ''), ('Shade', shade, '')], -1990, 5920)
    ice_normal = custom('Original M_Ice surface relief on glossy plates',
        'return normalize(lerp(float3(0,0,1),Normal,0.65));', u.CustomMaterialOutputType.CMOT_FLOAT3,
        [('Normal', original_normal, 'RGB')], -1990, 6150)
    frost_normal = custom('Softer relief under rough frost',
        'return normalize(lerp(float3(0,0,1),Normal,0.25));', u.CustomMaterialOutputType.CMOT_FLOAT3,
        [('Normal', original_normal, 'RGB')], -1990, 6350)
    ice_slab = node(u.MaterialExpressionSubstrateSlabBSDF, -1400, 4930,
        desc='Smooth reflective ice plates with the original M_Ice normal relief.')
    frost_slab = node(u.MaterialExpressionSubstrateSlabBSDF, -1400, 5590,
        desc='Matte white frost collects unevenly on and around the ice plates.')
    for slab, color, rough, normal in [(ice_slab, ice_color, ice_r, ice_normal), (frost_slab, frost_color, frost_r, frost_normal)]:
        link(color, '', slab, 'Diffuse Albedo')
        link(specular, '', slab, 'F0')
        link(rough, '', slab, 'Roughness')
        link(normal, '', slab, 'Normal')
    blend = node(u.MaterialExpressionSubstrateHorizontalMixing, -850, 5130, use_parameter_blending=True,
        desc='Mix glossy ice and clumped frost as native Substrate slabs. Parameter blending keeps one lighting evaluation.')
    link(ice_slab, '', blend, 'Background')
    link(frost_slab, '', blend, 'Foreground')
    link(frost_mix, '', blend, 'Mix')
    assert e.connect_material_property(blend, '', u.MaterialProperty.MP_FRONT_MATERIAL)

    # Vertex-safe relief uses only procedural UV noise, not pixel texture samples.
    params['IceThickness'].set_editor_properties({'default_value': 1.6, 'group': 'Ice Surface',
        'desc': 'Uneven plate relief in centimetres above the existing tongue coating. Does not affect collision.'})
    vertex_height = custom('Vertex-safe uneven plate thickness, reduced by warmth',
        'return smoothstep(0.32,0.68,Fields.r)*saturate(Amount)*max(Thickness,0.0)*Warmth;',
        u.CustomMaterialOutputType.CMOT_FLOAT1, [('Fields', field, ''), ('Amount', params['IceAmount'], ''),
        ('Thickness', params['IceThickness'], ''), ('Warmth', warm, '')], -1460, 6730)
    vertex_normal = next(n for n in nodes if isinstance(n, u.MaterialExpressionVertexNormalWS))
    wpo = node(u.MaterialExpressionMultiply, -970, 6730, desc='Small local relief makes the patches sit above the tongue.')
    link(vertex_normal, '', wpo, 'A')
    link(vertex_height, '', wpo, 'B')
    assert e.connect_material_property(wpo, '', u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    errors = list(e.recompile_material(mat))
    assert not errors, errors
    assert e.get_material_property_input_node(mat, u.MaterialProperty.MP_OPACITY_MASK) == dither
    assert a.save_loaded_asset(mat, only_if_is_dirty=False)
    task = u.AssetExportTask()
    task.set_editor_properties({'object': mat, 'filename': (out/'MaterialAfter.copy').as_posix(),
        'automated': True, 'prompt': False, 'replace_identical': True, 'exporter': u.ObjectExporterT3D()})
    assert u.Exporter.run_asset_export_task(task)
    mask_line = next(line.strip() for line in (out/'MaterialAfter.copy').read_text(encoding='utf-8-sig').splitlines()
        if line.strip().startswith('OpacityMask='))
    assert 'Expression=' in mask_line and 'UseConstant=True' not in mask_line
    report = {'material': mat.get_path_name(), 'backup': backup_path, 'compile_errors': errors,
        'front': blend.get_class().get_name(), 'native_slabs': 2, 'parameter_blending': True,
        'ice_patch_coverage': .58, 'ice_patch_scale': 7.0, 'ice_patch_softness': .075, 'frost_clump_coverage': .58,
        'warm_fade_width_cm': 120, 'warm_edge_variation_cm': 95, 'plate_relief_cm': 1.6,
        'shared_frost_mask': '/Game/Gameplay/Cold/Frost/T_PlayerFrostMask',
        'warm_gameplay_areas_remain_clear': True, 'both_warm_zones_supported': True,
        'stable_authored_uvs': True, 'zero_amount_clears_all': True}
    (out/'MaterialBuild.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('TONGUE_FROST_PATCH_VARIATION_SAVED', json.dumps(report))
