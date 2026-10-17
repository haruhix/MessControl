import unreal as u

material = u.load_asset('/Game/Gameplay/UI/Materials/M_UI_LEDGrid')
custom = next(x for x in u.MaterialEditingLibrary.get_material_expressions(material) if x.get_class().get_name() == 'MaterialExpressionCustom')
custom.set_editor_property('code', 'return float4(Tint.rgb,1);')
u.MaterialEditingLibrary.recompile_material(material)
u.log('LED_AB_TINT_OPAQUE_SET_UNSAVED')
