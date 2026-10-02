"""Keep the authored yellow buckles when a player's MI_Bag is recolored.

Run in the editor with PIE stopped. The bag gets a dedicated copy of its
gameplay parent, retaining the existing surface maps and BodyStretch animation.
"""
import unreal as u

assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before authoring the bag material.'
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
bag = lib.load_asset('/Game/Gameplay/CharacterCurrent/Materials/MI_Bag')
assert isinstance(bag, u.MaterialInstanceConstant)
destination = '/Game/Gameplay/CharacterCurrent/Materials/M_BagGameplay'
material = lib.load_asset(destination) if lib.does_asset_exist(destination) else None
if not material:
    material = lib.duplicate_asset(bag.get_editor_property('parent').get_path_name(), destination)
assert isinstance(material, u.Material)

description = 'Keep authored yellow bag details'
expressions = edit.get_material_expressions(material)
custom = next((n for n in expressions if isinstance(n, u.MaterialExpressionCustom)
               and n.get_editor_property('description') == description), None)
if not custom:
    tinted = edit.get_material_property_input_node(material, u.MaterialProperty.MP_BASE_COLOR)
    tinted_output = edit.get_material_property_input_node_output_name(material, u.MaterialProperty.MP_BASE_COLOR)
    original = next(n for n in expressions if isinstance(n, u.MaterialExpressionTextureSampleParameter2D)
                    and str(n.get_editor_property('parameter_name')) == 'Albedo Texture')
    brightness = next(n for n in expressions if isinstance(n, u.MaterialExpressionScalarParameter)
                     and str(n.get_editor_property('parameter_name')) == 'lightning')
    assert tinted
    custom = edit.create_material_expression(material, u.MaterialExpressionCustom, 1650, -300)
    custom.set_editor_property('description', description)
    custom.set_editor_property('output_type', u.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs = []
    for name in ('Tinted', 'Original', 'Brightness'):
        entry = u.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    custom.set_editor_property('inputs', inputs)
    assert edit.connect_material_expressions(tinted, tinted_output, custom, 'Tinted')
    assert edit.connect_material_expressions(original, 'RGB', custom, 'Original')
    assert edit.connect_material_expressions(brightness, '', custom, 'Brightness')

# Detect warm yellow in the original albedo, before desaturation or random tint.
# Relative chroma also protects the darker shading on the golden buckles.
custom.set_editor_property('code', '''
float maximum = max(Original.r, max(Original.g, Original.b));
float yellowChroma = (min(Original.r, Original.g) - Original.b) / max(maximum, 0.0001);
float keepYellow = smoothstep(0.06, 0.16, yellowChroma);
return lerp(Tinted, Original * Brightness, keepYellow);
''')
assert edit.connect_material_property(custom, '', u.MaterialProperty.MP_BASE_COLOR)
edit.recompile_material(material)
assert lib.save_loaded_asset(material, only_if_is_dirty=False)
edit.set_material_instance_parent(bag, material)
edit.update_material_instance(bag)
assert bag.get_editor_property('parent') == material
assert lib.save_loaded_asset(bag, only_if_is_dirty=False)
assert edit.get_material_property_input_node(material, u.MaterialProperty.MP_BASE_COLOR) == custom
u.log('BAG_YELLOW_PRESERVED ' + material.get_path_name())
