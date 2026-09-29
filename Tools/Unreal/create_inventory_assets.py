"""Run in Unreal Python after building. Existing equipment/VFX edits are preserved."""
import unreal

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
assets.make_directory('/Game/Gameplay/VFX')
profile = assets.load_asset('/Game/Data/DA_Equipment') if assets.does_asset_exist('/Game/Data/DA_Equipment') else None
if profile is None:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.MCEquipmentProfile)
    profile = tools.create_asset('DA_Equipment', '/Game/Data', unreal.MCEquipmentProfile, factory)
    pick = assets.load_asset('/Game/Art/Meshes/Equipments/SM_Pick')
    profile.set_editor_property('pickaxe_mesh', pick)
    bounds = pick.get_bounds()
    print('MC_PICK_BOUNDS', bounds)
    extent = bounds.box_extent
    scale = 85.0 / max(extent.x*2, extent.y*2, extent.z*2, 1)
    profile.set_editor_property('pickaxe_transform', unreal.Transform(location=[25,0,0], rotation=[0,0,0], scale=[scale,scale,scale]))
    assert assets.save_loaded_asset(profile)

# Preserve the existing tissue graph and append a separate frost control.
mat = assets.load_asset('/Game/Gameplay/Care/M_UlcerBlend')
mel = unreal.MaterialEditingLibrary
nodes = mel.get_material_expressions(mat)
if not any(isinstance(n,unreal.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name')) == 'Frozen' for n in nodes):
    original = mel.get_material_property_input_node(mat, unreal.MaterialProperty.MP_BASE_COLOR)
    original_pin = mel.get_material_property_input_node_output_name(mat, unreal.MaterialProperty.MP_BASE_COLOR)
    assert original
    frozen = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, 1650, 900)
    frozen.set_editor_property('parameter_name', 'Frozen')
    frozen.set_editor_property('default_value', 0.0)
    color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, 1650, 1050)
    color.set_editor_property('parameter_name', 'FrostColor')
    color.set_editor_property('default_value', unreal.LinearColor(.16,.66,.94,1))
    mix = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, 1950, 800)
    assert mel.connect_material_expressions(original,original_pin,mix,'A')
    assert mel.connect_material_expressions(color,'',mix,'B')
    assert mel.connect_material_expressions(frozen,'',mix,'Alpha')
    assert mel.connect_material_property(mix,'',unreal.MaterialProperty.MP_BASE_COLOR)
    rough = mel.get_material_property_input_node(mat,unreal.MaterialProperty.MP_ROUGHNESS)
    rough_mix = mel.create_material_expression(mat,unreal.MaterialExpressionLinearInterpolate,1950,1100)
    rough_mix.set_editor_property('const_b',.13)
    assert mel.connect_material_expressions(rough,'',rough_mix,'A')
    assert mel.connect_material_expressions(frozen,'',rough_mix,'Alpha')
    assert mel.connect_material_property(rough_mix,'',unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    assert assets.save_loaded_asset(mat)

foam = unreal.MCVFXAssetBuilder.create_brush_foam()
assert foam, 'Niagara foam authoring failed'
print('MC_INVENTORY_ASSETS_READY', profile.get_path_name(), foam.get_path_name())
