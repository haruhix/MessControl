"""Import the Substance plaque maps and apply the readable care presentation.

Run in the stopped Unreal Editor or its Python commandlet. Texture sources
and the editable Substance graph are kept in SourceArt/GrimeRich_20261004.
NullRHI can save packages quickly; its zero shader counts do not validate
rendering. Inspect the saved result in an editor render afterwards.
"""
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
source = root / 'SourceArt/GrimeRich_20261004/Textures'
destination = '/Game/Gameplay/Care/Textures'
maps = {'BaseColor': 'basecolor', 'Height': 'height',
        'Normal': 'normal', 'Roughness': 'roughness'}
assert all((source / ('MC_PlaqueRich_' + name + '.png')).is_file()
           for name in maps.values()), 'Export the Substance plaque graph first'
dirty = {p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
targets = {destination + '/T_GrimeRich_' + name for name in maps}
assert not dirty.intersection(targets), 'Preserve unsaved rich texture edits'
for suffix, name in maps.items():
    task = u.AssetImportTask()
    task.filename = str(source / ('MC_PlaqueRich_' + name + '.png'))
    task.destination_path = destination
    task.destination_name = 'T_GrimeRich_' + suffix
    task.automated = True
    task.replace_existing = True
    task.save = False
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    asset = u.EditorAssetLibrary.load_asset(destination + '/' + task.destination_name)
    assert isinstance(asset, u.Texture2D), task.destination_name
    asset.set_editor_property('srgb', suffix == 'BaseColor')
    asset.set_editor_property('compression_settings',
        u.TextureCompressionSettings.TC_NORMALMAP if suffix == 'Normal'
        else u.TextureCompressionSettings.TC_DEFAULT)
    assert u.EditorAssetLibrary.save_loaded_asset(asset, False)
    u.log('MC_RICH_TEXTURE ' + asset.get_path_name())

# Use the same authored workflows in live and headless editors.
for name in ('refine_grime_materials.py', 'refine_brush_foam.py'):
    path = root / 'Tools/Unreal' / name
    exec(compile(path.read_text(encoding='utf-8'), str(path), 'exec'),
         {'__name__': '__main__', '__file__': str(path)})
u.log('MC_RICH_CARE_SAVED')
