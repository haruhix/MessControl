import json
from pathlib import Path
import unreal as u

e, a = u.MaterialEditingLibrary, u.EditorAssetLibrary
mat = u.load_asset('/Game/Gameplay/Cold/Frost/M_TongueFrost')
nodes = list(e.get_material_expressions(mat))
assert not any(isinstance(n, u.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name')) == 'Ice Detail Tiling' for n in nodes)
slab = next(n for n in nodes if isinstance(n, u.MaterialExpressionSubstrateSlabBSDF) and str(n.get_editor_property('desc')).startswith('Smooth reflective ice'))
slab_inputs = dict(zip([str(v) for v in e.get_material_expression_input_names(slab)], e.get_inputs_for_material_expression(mat, slab)))
normal_shading = slab_inputs['Normal']
normal_texture = list(e.get_inputs_for_material_expression(mat, normal_shading))[0]
assert isinstance(normal_texture, u.MaterialExpressionTextureSample)
with u.ScopedEditorTransaction('Make the ice cracks small enough for separate tongue ice plates'):
    uv = e.create_material_expression(mat, u.MaterialExpressionTextureCoordinate, -2750, 6410)
    uv.set_editor_property('coordinate_index', 0)
    tiling = e.create_material_expression(mat, u.MaterialExpressionScalarParameter, -2750, 6590)
    tiling.set_editor_properties({'parameter_name': 'Ice Detail Tiling', 'default_value': 10.0,
        'slider_min': 1.0, 'slider_max': 24.0, 'group': 'Ice Surface',
        'desc': 'Repeats the original M_Ice normal texture across the tongue. Higher values give finer cracks.'})
    scaled = e.create_material_expression(mat, u.MaterialExpressionMultiply, -2450, 6410)
    assert e.connect_material_expressions(uv, '', scaled, 'A')
    assert e.connect_material_expressions(tiling, '', scaled, 'B')
    assert e.connect_material_expressions(scaled, '', normal_texture, 'UVs')
    errors = list(e.recompile_material(mat))
    assert not errors, errors
    assert a.save_loaded_asset(mat, only_if_is_dirty=False)
    out = Path(u.Paths.project_dir())/'Artifacts/TongueFrostVariation'
    report = json.loads((out/'MaterialBuild.json').read_text(encoding='utf-8'))
    report.update(ice_detail_tiling=10.0, original_ice_normal_texture=normal_texture.get_editor_property('texture').get_path_name(),
        parallax_height_loop_used_in_active_graph=False, compile_errors=errors)
    (out/'MaterialBuild.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    task = u.AssetExportTask()
    task.set_editor_properties({'object': mat, 'filename': (out/'MaterialAfter.copy').as_posix(),
        'automated': True, 'prompt': False, 'replace_identical': True, 'exporter': u.ObjectExporterT3D()})
    assert u.Exporter.run_asset_export_task(task)
    print('TONGUE_ICE_DETAIL_TUNED', json.dumps(report))
