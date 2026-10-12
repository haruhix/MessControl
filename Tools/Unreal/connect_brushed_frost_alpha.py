"""Connect brushed-floor vertex alpha to the existing frost opacity mask."""
import json
import unreal as u

assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
e, assets = u.MaterialEditingLibrary, u.EditorAssetLibrary
path = '/Game/Gameplay/Cold/Frost/M_TongueFrost'
material = u.load_asset(path)
assert material and isinstance(material, u.Material)
prefix = 'MC brushed warming patch | '
nodes = list(e.get_material_expressions(material))
owned = [n for n in nodes if str(n.get_editor_property('desc')).startswith(prefix)]
if not owned:
    with u.ScopedEditorTransaction('Show brushed warming patches in tongue frost'):
        dither = e.get_material_property_input_node(material, u.MaterialProperty.MP_OPACITY_MASK)
        names = [str(v) for v in e.get_material_expression_input_names(dither)]
        assert 'Alpha Threshold' in names
        original = list(e.get_inputs_for_material_expression(material, dither))[names.index('Alpha Threshold')]
        assert original
        color = e.create_material_expression(material, u.MaterialExpressionVertexColor, 1500, 1000)
        color.set_editor_property('desc', prefix + 'Native cleared-floor fade')
        multiply = e.create_material_expression(material, u.MaterialExpressionMultiply, 1750, 1000)
        multiply.set_editor_property('desc', prefix + 'Preserve frost growth, clear only brushed vertices')
        assert e.connect_material_expressions(original, '', multiply, 'A')
        assert e.connect_material_expressions(color, 'A', multiply, 'B')
        assert e.connect_material_expressions(multiply, '', dither, 'Alpha Threshold')
        e.recompile_material(material)
        assert assets.save_loaded_asset(material, only_if_is_dirty=False)
u.log('MC_BRUSHED_FROST_ALPHA ' + json.dumps(dict(path=path, created=not bool(owned))))
