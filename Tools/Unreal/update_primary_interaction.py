"""Update control hints and verify the existing tongue WPO connection."""
import unreal as u

lib = u.EditorAssetLibrary

def hint(text):
    return str(text).replace('Hold E', 'Hold LMB').replace('HOLD E', 'HOLD LMB').replace('E:', 'LMB:').replace('hold E', 'hold LMB')

plan = lib.load_asset('/Game/Data/DA_Day01')
assert plan
steps = plan.get_editor_property('steps')
for step in steps:
    step.set_editor_property('instruction', u.Text(hint(step.get_editor_property('instruction'))))
plan.set_editor_property('steps', steps)
assert lib.save_loaded_asset(plan, only_if_is_dirty=False)

for name in ('DA_Coffee', 'DA_Food', 'DA_LooseTooth'):
    asset = lib.load_asset('/Game/Data/' + name)
    if asset:
        asset.set_editor_property('instruction', u.Text(hint(asset.get_editor_property('instruction'))))
        assert lib.save_loaded_asset(asset, only_if_is_dirty=False)

grip = lib.load_asset('/Game/Data/DA_Grip')
settings = grip.get_editor_property('settings')
assert 1 <= settings.get_editor_property('drag_distance_scale') <= 2
assert lib.save_loaded_asset(grip, only_if_is_dirty=False)

world = u.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_Mouth')
tongues = [actor for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors() if isinstance(actor, u.MCTongue)]
assert tongues, 'Map has no gameplay tongue'
for tongue in tongues:
    material = tongue.get_editor_property('surface_material')
    u.log('PRIMARY_TONGUE_MATERIAL ' + str(material))
    while isinstance(material, u.MaterialInstanceConstant):
        material = material.get_editor_property('parent')
    assert material, 'Tongue has no surface material'
    node = u.MaterialEditingLibrary.get_material_property_input_node(material, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    assert node, 'Tongue material has no WPO connection'
    u.log('PRIMARY_WPO_VERIFIED ' + material.get_path_name())
u.log('PRIMARY_INTERACTION_ASSETS_PASS')
u.SystemLibrary.quit_editor()
