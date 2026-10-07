"""Create the designer-owned Director profile after compiling native code."""
import unreal as ue

path = '/Game/Data/DA_GameDirector'
library = ue.EditorAssetLibrary
asset = library.load_asset(path) if library.does_asset_exist(path) else None
if asset:
    if not isinstance(asset, ue.MCGameDirectorProfile):
        raise RuntimeError('DA_GameDirector exists with an unexpected class')
    ue.log('MC_DIRECTOR_PROFILE_EXISTS: preserved designer settings')
else:
    library.make_directory('/Game/Data')
    factory = ue.DataAssetFactory()
    factory.set_editor_property('data_asset_class', ue.MCGameDirectorProfile)
    asset = ue.AssetToolsHelpers.get_asset_tools().create_asset(
        'DA_GameDirector', '/Game/Data', ue.MCGameDirectorProfile, factory)
    if not asset or not library.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError('Could not create/save DA_GameDirector')
    ue.log('MC_DIRECTOR_PROFILE_CREATED: seven days, 360 to 480 seconds')

days = asset.get_editor_property('days')
if len(days) != 7:
    raise RuntimeError('Director profile must contain seven days')
ue.log('MC_DIRECTOR_PROFILE_READY: durations=' + ','.join(
    str(day.get_editor_property('day_seconds')) for day in days))
