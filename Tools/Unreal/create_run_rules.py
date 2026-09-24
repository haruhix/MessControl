"""Create the step-1 DA_RunRules after compiling. Preserve existing designer values."""
import unreal as ue

path = '/Game/Data/DA_RunRules'
library = ue.EditorAssetLibrary
asset = library.load_asset(path) if library.does_asset_exist(path) else None
if asset:
    if not isinstance(asset, ue.MCRunRules):
        raise RuntimeError('DA_RunRules exists with an unexpected class')
    ue.log('MC_RUN_RULES_EXISTS: kept existing settings')
else:
    library.make_directory('/Game/Data')
    factory = ue.DataAssetFactory()
    factory.set_editor_property('data_asset_class', ue.MCRunRules)
    asset = ue.AssetToolsHelpers.get_asset_tools().create_asset('DA_RunRules', '/Game/Data', ue.MCRunRules, factory)
    if not asset or not library.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError('Could not create/save DA_RunRules')
    ue.log('MC_RUN_RULES_CREATED: health=100, days=7, players=4, planned arena teeth=8')
