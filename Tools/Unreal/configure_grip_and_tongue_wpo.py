"""Create grip tuning and verify the tongue's shared render/physics surface.
Historical filename retained for existing setup commands. Pressure now moves
mesh vertices on the CPU; adding WPO here would apply the depth twice.
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
assert old is None, 'Run repair_artist_surface_collision.py: legacy WPO separates rendering from collision'
u.log('MC_GRIP_SURFACE_ASSETS_PASS')
u.SystemLibrary.quit_editor()
