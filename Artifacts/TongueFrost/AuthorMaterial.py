import json
from pathlib import Path
import unreal as u

e, a = u.MaterialEditingLibrary, u.EditorAssetLibrary
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
source_path = '/Game/Gameplay/Cold/Frost/M_PlayerIceCoating_BeforeMotionFix'
dest_path = '/Game/Gameplay/Cold/Frost/M_TongueFrost'
assert not a.does_asset_exist(dest_path), 'Inspect the existing tongue material before replacing it.'
source = u.load_asset(source_path)
assert source and source.get_blend_mode() == u.BlendMode.BLEND_MASKED
out = Path(u.Paths.project_dir())/'Artifacts/TongueFrost'
out.mkdir(parents=True, exist_ok=True)

with u.ScopedEditorTransaction('Add fern frost and a soft warm-zone fade to the tongue'):
    mat = a.duplicate_asset(source_path, dest_path)
    assert mat
    nodes = list(e.get_material_expressions(mat))
    dither = e.get_material_property_input_node(mat, u.MaterialProperty.MP_OPACITY_MASK)
    names = [str(n) for n in e.get_material_expression_input_names(dither)]
    assert 'Alpha Threshold' in names
    growth = list(e.get_inputs_for_material_expression(mat, dither))[names.index('Alpha Threshold')]
    assert isinstance(growth, u.MaterialExpressionCustom)
    for n in nodes:
        if isinstance(n, u.MaterialExpressionScalarParameter):
            name = str(n.get_editor_property('parameter_name'))
            n.set_editor_property('group', 'Tongue Frost')
            if name == 'IceThickness':
                n.set_editor_property('default_value', 0.0)
                n.set_editor_property('desc', 'The native coating already follows the tongue with a surface-normal offset.')
            elif name == 'IceAmount':
                n.set_editor_property('desc', 'Driven by the winter event arrival: zero clears the tongue, one reveals the frost.')
            elif name == 'Frost Tiling':
                n.set_editor_properties({'default_value': 6.0, 'slider_min': 1.0, 'slider_max': 12.0,
                    'desc': 'Crystal repeats in the tongue UVs; the pattern follows its deformation.'})

    def node(cls, x, y, **values):
        n = e.create_material_expression(mat, cls, x, y)
        n.set_editor_properties(values)
        return n

    def scalar(name, value, y, desc):
        return node(u.MaterialExpressionScalarParameter, -2600, y,
            parameter_name=name, group='Warm Zone', default_value=value, desc=desc)

    def link(src, output, target, pin):
        assert e.connect_material_expressions(src, output, target, pin), pin

    pos = node(u.MaterialExpressionWorldPosition, -2600, 3150,
        world_position_shader_offset=u.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    center = node(u.MaterialExpressionVectorParameter, -2600, 3300,
        parameter_name='Warm Center', group='Warm Zone', default_value=u.LinearColor(0, 0, 0, 1))
    radius = scalar('Warm Radius', 330.0, 3480, 'Current gameplay-safe radius, updated by the winter event.')
    enabled = scalar('Warm Enabled', 0.0, 3650, 'The event enables the current safe circle.')
    next_center = node(u.MaterialExpressionVectorParameter, -2600, 3840,
        parameter_name='Next Warm Center', group='Warm Zone', default_value=u.LinearColor(0, 0, 0, 1))
    next_radius = scalar('Next Warm Radius', 330.0, 4020, 'Safe radius during the circle handover.')
    next_enabled = scalar('Next Warm Enabled', 0.0, 4190, 'Enabled only while both circles provide warmth.')
    width = scalar('Warm Fade Width', 120.0, 4360,
        'Soft fade in centimetres outside each safe circle. The entire safe area stays free of frost.')
    width.set_editor_properties({'slider_min': 10.0, 'slider_max': 300.0})
    inputs = []
    for name in ['P', 'Center', 'Radius', 'Enabled', 'NextCenter', 'NextRadius', 'NextEnabled', 'Width']:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    code = ('float w=max(Width,1.0);\n'
        'float r=max(Radius,0.0);\n'
        'float nr=max(NextRadius,0.0);\n'
        'float outside=smoothstep(r,r+w,length(P.xy-Center.xy));\n'
        'float nextOutside=smoothstep(nr,nr+w,length(P.xy-NextCenter.xy));\n'
        'return lerp(1.0,outside,saturate(Enabled))*lerp(1.0,nextOutside,saturate(NextEnabled));')
    warm = node(u.MaterialExpressionCustom, -2110, 3320,
        inputs=inputs, code=code, description='Smooth frost fade outside the current and next warm circles',
        output_type=u.CustomMaterialOutputType.CMOT_FLOAT1)
    for src, output, pin in [(pos, '', 'P'), (center, 'RGB', 'Center'), (radius, '', 'Radius'),
            (enabled, '', 'Enabled'), (next_center, 'RGB', 'NextCenter'), (next_radius, '', 'NextRadius'),
            (next_enabled, '', 'NextEnabled'), (width, '', 'Width')]:
        link(src, output, warm, pin)
    coverage = node(u.MaterialExpressionMultiply, -1660, 3190,
        desc='The same growing crystal mask, softened by warmth before temporal dithering.')
    link(growth, '', coverage, 'A')
    link(warm, '', coverage, 'B')
    link(coverage, '', dither, 'Alpha Threshold')
    errors = list(e.recompile_material(mat))
    assert not errors, errors
    assert mat.get_blend_mode() == u.BlendMode.BLEND_MASKED
    assert a.save_loaded_asset(mat, only_if_is_dirty=False)
    task = u.AssetExportTask()
    task.set_editor_properties({'object': mat, 'filename': (out/'MaterialGraph.copy').as_posix(),
        'automated': True, 'prompt': False, 'replace_identical': True, 'exporter': u.ObjectExporterT3D()})
    assert u.Exporter.run_asset_export_task(task)
    mask_line = next(line.strip() for line in (out/'MaterialGraph.copy').read_text(encoding='utf-8-sig').splitlines()
        if line.strip().startswith('OpacityMask='))
    assert 'Expression=' in mask_line and 'UseConstant=True' not in mask_line, mask_line
    report = {'material': mat.get_path_name(), 'shared_mask': '/Game/Gameplay/Cold/Frost/T_PlayerFrostMask.T_PlayerFrostMask',
        'source': source_path, 'compile_errors': errors, 'warm_fade_width_cm': 120.0, 'frost_tiling': 6.0,
        'warm_fade_code': code, 'opacity_mask_input': mask_line,
        'safe_circle_clear': True, 'both_warm_zones_supported': True, 'character_deformation_included': False}
    (out/'MaterialBuild.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('TONGUE_FROST_MATERIAL_SAVED', json.dumps(report))
