"""Bind reference enamel to the local cleaning material; preserve sculpted meshes."""
import unreal as u
lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary
m=lib.load_asset('/Game/Art/Materials/MI_ArenaToothReference')
parent=lib.load_asset('/Game/Gameplay/Care/M_ArenaToothCare')
assert parent and 'GrimeColor' in [str(n) for n in edit.get_vector_parameter_names(parent)]
edit.set_material_instance_parent(m,parent)
edit.set_material_instance_vector_parameter_value(m,'Tint',u.LinearColor(.98,.95,.87,1))
edit.set_material_instance_vector_parameter_value(m,'GrimeColor',u.LinearColor(.25,.066,.008,1))
edit.update_material_instance(m);lib.save_loaded_asset(m,False)
foam=lib.load_asset('/Game/Gameplay/Care/M_BrushFoam')
edit.set_base_material_usage(foam,u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
if not edit.get_material_property_input_node(foam,u.MaterialProperty.MP_EMISSIVE_COLOR):
    n=edit.create_material_expression(foam,u.MaterialExpressionConstant3Vector)
    edit.connect_material_property(n,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
n=edit.get_material_property_input_node(foam,u.MaterialProperty.MP_EMISSIVE_COLOR)
n.set_editor_property('constant',u.LinearColor(.65,.78,.85,1))
edit.recompile_material(foam);lib.save_loaded_asset(foam,False)
u.log('MC_REFERENCE_CLEANING_MATERIAL_FIXED')
