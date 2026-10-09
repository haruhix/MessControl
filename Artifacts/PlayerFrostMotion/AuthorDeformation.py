import json
from pathlib import Path
import unreal as u

e, a = u.MaterialEditingLibrary, u.EditorAssetLibrary
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
mat = u.load_asset('/Game/Gameplay/Cold/M_PlayerIceCoating')
source = u.load_asset('/Game/Art/Materials/M_TeethGameplay')
assert mat and source
assert not any(isinstance(n, u.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name')) == 'BodyStretch' for n in e.get_material_expressions(mat))
coating_wpo = e.get_material_property_input_node(mat, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
original_mask = e.get_material_property_input_node(mat, u.MaterialProperty.MP_OPACITY_MASK)
source_wpo = e.get_material_property_input_node(source, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
assert coating_wpo and source_wpo and original_mask
backup_path = '/Game/Gameplay/Cold/Frost/M_PlayerIceCoating_BeforeMotionFix'
assert not a.does_asset_exist(backup_path)
backup = a.duplicate_asset(mat.get_path_name().split('.')[0], backup_path)
assert backup and a.save_loaded_asset(backup, only_if_is_dirty=False)
nodes = []
def walk(n):
    if n and n not in nodes:
        nodes.append(n)
        for p in e.get_inputs_for_material_expression(source, n):
            walk(p)
walk(source_wpo)
allowed = {'MaterialExpressionAdd', 'MaterialExpressionTransform', 'MaterialExpressionCustom', 'MaterialExpressionTransformPosition', 'MaterialExpressionWorldPosition', 'MaterialExpressionScalarParameter', 'MaterialExpressionPreSkinnedPosition'}
assert all(n.get_class().get_name() in allowed for n in nodes)
with u.ScopedEditorTransaction('Make the frost shell follow the character shader deformation'):
    copies = {}
    for i, n in enumerate(nodes):
        copy = e.duplicate_material_expression(mat, None, n)
        assert copy
        copy.set_editor_property('material_expression_editor_x', -2400+(i%4)*320)
        copy.set_editor_property('material_expression_editor_y', 3300+(i//4)*230)
        if isinstance(copy, u.MaterialExpressionScalarParameter):
            copy.set_editor_property('group', 'Character Deformation')
        copies[n] = copy
    for n in nodes:
        names = list(e.get_material_expression_input_names(n))
        inputs = list(e.get_inputs_for_material_expression(source, n))
        assert len(names) == len(inputs)
        for name, src in zip(names, inputs):
            if src:
                # This deformation branch has only first-output scalar/vector nodes.
                assert e.connect_material_expressions(copies[src], '', copies[n], '' if str(name) == 'None' else str(name))
    total = e.create_material_expression(mat, u.MaterialExpressionAdd, -800, 3300)
    total.set_editor_property('desc', 'Match body squash/stretch and chipped geometry, then keep the ice shell outside it.')
    assert e.connect_material_expressions(copies[source_wpo], '', total, 'A')
    assert e.connect_material_expressions(coating_wpo, '', total, 'B')
    assert e.connect_material_property(total, '', u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    errors = list(e.recompile_material(mat))
    assert not errors, errors
    assert e.get_material_property_input_node(mat, u.MaterialProperty.MP_OPACITY_MASK) == original_mask
    assert a.save_loaded_asset(mat, only_if_is_dirty=False)
    out = Path(u.Paths.project_dir())/'Artifacts/PlayerFrostMotion'
    out.mkdir(parents=True, exist_ok=True)
    report = {'material': mat.get_path_name(), 'source_deformation': source.get_path_name(), 'backup': backup_path,
        'compile_errors': errors, 'copied_nodes': len(nodes), 'parameters': ['BodyStretch', 'Damage', 'DamageChipDepth'],
        'growth_mask_preserved': True, 'shell_offset_preserved': True}
    (out/'MaterialBuild.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('FROST_DEFORMATION_SAVED', json.dumps(report))
