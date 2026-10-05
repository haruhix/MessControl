"""Author the inexpensive opaque material used by procedural dental calculus.

Run with Unreal Python. Only the owned calculus material is saved; no map edits.
The mesh supplies baked deposit/fresh-fracture colour through VertexColor.
"""
import unreal as u

lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
folder = '/Game/Gameplay/Care'
name = 'M_ToothCalculus'
lib.make_directory(folder)
material = lib.load_asset(folder + '/' + name) if lib.does_asset_exist(folder + '/' + name) else None
if material is None:
    material = u.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, u.Material, u.MaterialFactoryNew())
assert isinstance(material, u.Material)
edit.delete_all_material_expressions(material)
material.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
material.set_editor_property('shading_model', u.MaterialShadingModel.MSM_DEFAULT_LIT)
material.set_editor_property('two_sided', False)
material.set_editor_property('dithered_lod_transition', False)

vertex = edit.create_material_expression(material, u.MaterialExpressionVertexColor, -450, 0)
assert edit.connect_material_property(vertex, '', u.MaterialProperty.MP_BASE_COLOR)

for index, (parameter, value, output) in enumerate([
    ('Roughness', .82, u.MaterialProperty.MP_ROUGHNESS),
    ('Specular', .26, u.MaterialProperty.MP_SPECULAR),
    ('Metallic', 0., u.MaterialProperty.MP_METALLIC),
]):
    node = edit.create_material_expression(material, u.MaterialExpressionScalarParameter,
                                          -450, 200 + index * 170)
    node.set_editor_property('parameter_name', parameter)
    node.set_editor_property('group', 'Calculus')
    node.set_editor_property('default_value', value)
    assert edit.connect_material_property(node, '', output)

edit.layout_material_expressions(material)
errors = edit.recompile_material(material)
assert not errors, str(errors)
assert lib.save_loaded_asset(material, only_if_is_dirty=False)
u.log('MC_CALCULUS_MATERIAL_SAVED ' + material.get_path_name())
