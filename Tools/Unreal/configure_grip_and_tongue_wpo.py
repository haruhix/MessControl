"""Create the grip tuning asset and add UV1 weight displacement to the derived tongue material.
Does not change the artist material, mesh, rig or map.
"""
import unreal as u
lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary; assets=u.AssetToolsHelpers.get_asset_tools()
path='/Game/Data/DA_Grip'
if not lib.does_asset_exist(path):
    factory=u.DataAssetFactory(); factory.set_editor_property('data_asset_class',u.MCGripProfile)
    grip=assets.create_asset('DA_Grip','/Game/Data',u.MCGripProfile,factory)
    assert lib.save_loaded_asset(grip,only_if_is_dirty=False)
mat=lib.load_asset('/Game/Gameplay/Arena/M_TonguePain')
assert mat
old=edit.get_material_property_input_node(mat,u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
if old:
    assert str(old.get_editor_property('desc'))=='MC weight WPO world', 'Unexpected existing WPO; preserve it for manual integration'
else:
    uv=edit.create_material_expression(mat,u.MaterialExpressionTextureCoordinate,800,2400)
    uv.set_editor_property('coordinate_index',1)
    offset=edit.create_material_expression(mat,u.MaterialExpressionCustom,1040,2400)
    offset.set_editor_property('description','MC weight: local depth from UV1; collision unchanged')
    offset.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT3)
    entry=u.CustomInput(); entry.set_editor_property('input_name','Weight')
    offset.set_editor_property('inputs',[entry]); offset.set_editor_property('code','return float3(0,0,-Weight.x);')
    world=edit.create_material_expression(mat,u.MaterialExpressionTransform,1300,2400)
    world.set_editor_property('desc','MC weight WPO world')
    world.set_editor_property('transform_source_type',u.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    world.set_editor_property('transform_type',u.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    assert edit.connect_material_expressions(uv,'',offset,'Weight')
    assert edit.connect_material_expressions(offset,'',world,'')
    assert edit.connect_material_property(world,'',u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
errors=edit.recompile_material(mat)
assert not errors,str(errors)
assert lib.save_loaded_asset(mat,only_if_is_dirty=False)
assert edit.get_material_property_input_node(mat,u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
u.log('MC_GRIP_WPO_ASSETS_PASS')
u.SystemLibrary.quit_editor()
