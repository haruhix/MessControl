"""Adds the four-system prototype data, without rebuilding the user's level or meshes."""
import unreal as u

lib = u.EditorAssetLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
for name, cls in [('DA_ToothCare', u.MCToothCareProfile), ('DA_FoodPhysics', u.MCFoodProfile)]:
    path = '/Game/Data/' + name
    if not lib.does_asset_exist(path):
        factory = u.DataAssetFactory()
        factory.set_editor_property('data_asset_class', cls)
        data = assets.create_asset(name, '/Game/Data', cls, factory)
        if not data or not lib.save_loaded_asset(data, only_if_is_dirty=False):
            raise RuntimeError('Could not save ' + path)

instructions = {
    'DA_Coffee': 'Hold LMB facing a tooth: 4 contacts, 0.5s each. C: care for yourself. Teamwork adds contacts.',
    'DA_LooseTooth': 'Hold E facing a damaged or loose tooth to heal and secure it. C: care for yourself.',
    'DA_Food': 'Dodge falling food. Hold E and move to drag it. Pull stuck food towards the centre, then bring it to THROAT.'
}
for name, text in instructions.items():
    data = lib.load_asset('/Game/Data/' + name)
    if data:
        data.set_editor_property('instruction', text)
        lib.save_loaded_asset(data, only_if_is_dirty=False)

mat = lib.load_asset('/Game/Art/Materials/M_ArenaTooth')
if mat:
    mat.set_editor_property('used_with_skeletal_mesh', True)
    mat.set_editor_property('used_with_morph_targets', True)
    u.MaterialEditingLibrary.recompile_material(mat)
    lib.save_loaded_asset(mat, only_if_is_dirty=False)
mesh = lib.load_asset('/Game/Art/Rig/SK_ToothHero')
for slot in mesh.get_editor_property('materials') if mesh else []:
    u.log('MC_CARE_MATERIAL: ' + str(slot.material_slot_name) + ' ' + str(slot.material_interface))
u.log('MC_CARE_FOOD_ASSETS_PASS')
