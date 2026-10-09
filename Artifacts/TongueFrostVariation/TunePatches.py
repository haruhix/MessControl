import json
from pathlib import Path
import unreal as u

mat = u.load_asset('/Game/Gameplay/Cold/Frost/M_TongueFrost')
e, a = u.MaterialEditingLibrary, u.EditorAssetLibrary
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
out = Path(u.Paths.project_dir())/'Artifacts/TongueFrostVariation'
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
tongue = next(iter(u.GameplayStatics.get_all_actors_of_class(world, u.MCTongue)))
verts, indices, normals, uvs, tangents = u.ProceduralMeshLibrary.get_section_from_procedural_mesh(tongue.get_editor_property('surface'), 0)
print('TONGUE_UV_EXTENT', min(v.x for v in uvs), max(v.x for v in uvs), min(v.y for v in uvs), max(v.y for v in uvs))
nodes = list(e.get_material_expressions(mat))
params = {str(n.get_editor_property('parameter_name')): n for n in nodes
    if isinstance(n, (u.MaterialExpressionScalarParameter, u.MaterialExpressionVectorParameter))}
with u.ScopedEditorTransaction('Tune the ice into smaller separate plates with denser crystal clusters'):
    params['Ice Patch Scale'].set_editor_properties({'default_value': 24.0, 'slider_max': 48.0})
    params['Ice Patch Coverage'].set_editor_property('default_value', .52)
    params['Frost Clump Coverage'].set_editor_property('default_value', .67)
    params['Warm Edge Variation'].set_editor_property('default_value', 65.0)
    warm = next(n for n in nodes if isinstance(n, u.MaterialExpressionCustom) and str(n.get_editor_property('description')).startswith('Uneven thaw fringe'))
    code = str(warm.get_editor_property('code'))
    code = code.replace('edge/95.0', 'edge/65.0').replace('lerp(0.45,1.85,Fields.b)', 'lerp(0.40,1.35,Fields.b)')
    warm.set_editor_property('code', code)
    errors = list(e.recompile_material(mat))
    assert not errors, errors
    assert a.save_loaded_asset(mat, only_if_is_dirty=False)
    report = json.loads((out/'MaterialBuild.json').read_text(encoding='utf-8'))
    report.update(ice_patch_scale=24.0, ice_patch_coverage=.52, frost_clump_coverage=.67, warm_edge_variation_cm=65.0,
        uv_extent=[min(v.x for v in uvs), max(v.x for v in uvs), min(v.y for v in uvs), max(v.y for v in uvs)])
    (out/'MaterialBuild.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('TONGUE_ICE_PATCH_SIZE_TUNED', json.dumps(report))
