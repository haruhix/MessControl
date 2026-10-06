"""Author the new unlit floor guide material only; never save the open map."""
import unreal

path = '/Game/Gameplay/Delivery/M_DeliveryZoneGuide'
if unreal.EditorAssetLibrary.does_asset_exist(path):
    raise RuntimeError('Refusing to replace an existing delivery guide material: ' + path)
material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    'M_DeliveryZoneGuide', '/Game/Gameplay/Delivery', unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property('two_sided', True)
edit = unreal.MaterialEditingLibrary
color = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -560, -120)
color.set_editor_property('parameter_name', 'ZoneColor')
color.set_editor_property('default_value', unreal.LinearColor(.04, 1, .32, 1))
strength = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -560, 20)
strength.set_editor_property('parameter_name', 'GlowStrength')
strength.set_editor_property('default_value', 1.6)
emissive = edit.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, -120)
edit.connect_material_expressions(color, '', emissive, 'A')
edit.connect_material_expressions(strength, '', emissive, 'B')
edit.connect_material_property(emissive, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
vertex = edit.create_material_expression(material, unreal.MaterialExpressionVertexColor, -560, 180)
opacity = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -560, 320)
opacity.set_editor_property('parameter_name', 'Opacity')
opacity.set_editor_property('default_value', 1)
alpha = edit.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, 180)
edit.connect_material_expressions(vertex, 'A', alpha, 'A')
edit.connect_material_expressions(opacity, '', alpha, 'B')
edit.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY)
edit.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
    raise RuntimeError('Could not save the new delivery guide material')
unreal.log('MC_DELIVERY_MATERIAL_CREATED ' + path)
