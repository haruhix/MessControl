"""Import the isolated non-boss compact font and assign only its theme field.

Run in an idle MessControl editor. Sources/license are on E:. Existing boss
fonts and theme typography fields stay unchanged. No widget/CDO rewriting.
"""
import hashlib
import json
import runpy
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = ROOT / 'ArtSource/UI/GameplayPolish_20261017/Fonts/Roboto-BoldCondensed.ttf'
FACE_PATH = '/Game/Gameplay/UI/Fonts/FF_MCGameplay_Condensed'
FONT_PATH = FACE_PATH + '_Font'
THEME_PATH = '/Game/Gameplay/UI/Theme/DA_GameplayUITheme'


def require(ok, text):
    if not ok:
        raise RuntimeError(text)


def main():
    require(ROOT.drive.upper() == 'E:', 'Task storage must be on E:')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE before font authoring')
    require(SOURCE.is_file() and (SOURCE.parent / 'ROBOTO_License.txt').is_file(), 'Font source/license missing')
    lib = u.EditorAssetLibrary
    require(lib.does_asset_exist(FACE_PATH) == lib.does_asset_exist(FONT_PATH), 'Preserving incomplete existing font pair')
    if not lib.does_asset_exist(FACE_PATH):
        factory = u.FontFileImportFactory()
        factory.set_editor_property('batch_create_font_asset', u.BatchCreateFontAsset.CREATE_IF_NO_FONT_EXISTS)
        task = u.AssetImportTask()
        task.set_editor_property('filename', str(SOURCE))
        task.set_editor_property('destination_path', FACE_PATH.rsplit('/', 1)[0])
        task.set_editor_property('destination_name', FACE_PATH.rsplit('/', 1)[1])
        task.set_editor_property('factory', factory)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', False)
        task.set_editor_property('replace_existing_settings', False)
        task.set_editor_property('save', False)
        u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    face, font = u.load_asset(FACE_PATH), u.load_asset(FONT_PATH)
    require(isinstance(face, u.FontFace) and isinstance(font, u.Font), 'Unexpected font asset classes')
    require(Path(face.get_editor_property('source_filename')).resolve() == SOURCE, 'Preserving font with another source')
    require(font.get_editor_property('font_cache_type') == u.FontCacheType.RUNTIME, 'Expected runtime font')
    for path, asset in ((FACE_PATH, face), (FONT_PATH, font)):
        disk = ROOT / 'Content' / (path[len('/Game/'):] + '.uasset')
        if not disk.is_file():
            require(lib.save_loaded_asset(asset, only_if_is_dirty=False), 'Could not save ' + path)
    theme = u.load_asset(THEME_PATH)
    protected = ('body_font', 'heading_font', 'display_font', 'palette')
    # Palette/font exports are compared byte-for-byte, independent of UObject repr.
    before = {key: theme.get_editor_property(key).export_text() for key in protected}
    compact = theme.get_editor_property('heading_font').copy()
    compact.set_editor_property('font_object', font)
    compact.set_editor_property('typeface_font_name', 'Default')
    theme.set_editor_property('compact_heading_font', compact)
    after = {key: theme.get_editor_property(key).export_text() for key in protected}
    require(before == after, 'Existing boss/theme styling changed unexpectedly')
    require(lib.save_loaded_asset(theme, only_if_is_dirty=False), 'Could not save theme compact font')
    # The bundled Roboto face lacks arrows/checks; preserve the primary face
    # and add the engine's saved Droid symbol fallback to this isolated UFont.
    runpy.run_path(str(Path(__file__).with_name('repair_ui_polish_font_fallback.py')), run_name='__main__')
    report = {'status': 'passed', 'font_face': FACE_PATH, 'font': FONT_PATH,
              'theme_field': 'compact_heading_font', 'existing_typography_palette_preserved': before == after,
              'source': str(SOURCE), 'source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
              'license': str(SOURCE.parent / 'ROBOTO_License.txt'),
              'provenance': 'Bundled UE 5.8 Roboto Bold Condensed; Google 2012; Apache 2.0',
              'validation': 'Cyrillic and long EN/DE actual glyph rendering required after import'}
    output = ROOT / 'Saved/GameplayUI/Polish/font_authoring.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    u.log('MC_UI_POLISH_FONT PASS ' + str(output))


if __name__ == '__main__':
    main()
