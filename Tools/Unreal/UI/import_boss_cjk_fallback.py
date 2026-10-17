"""Import the unmodified Apache-2.0 Droid face and configure boss-name CJK ranges.

Run after the new editor helper is compiled. Only the isolated CJK FontFace and
existing boss-name Runtime UFont are saved. Cuprum source/default face, digit
font, theme, widgets and global culture are not edited.
"""
import hashlib
import json
from pathlib import Path

import unreal as u


ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = (ROOT / 'ArtSource/UI/BossReferenceMatch_20261017/Fonts/DroidSansFallback.ttf').resolve()
FACE_PATH = '/Game/Gameplay/UI/Fonts/FF_MCBossCJK_Regular'
NAMES_PATH = '/Game/Gameplay/UI/Fonts/FF_MCBossNames_Regular_Font'
DEFAULT_FACE_PATH = '/Game/Gameplay/UI/Fonts/FF_MCBossNames_Regular'
REPORT = ROOT / 'Saved/GameplayUI/BossReference/cjk_fallback_import.json'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    require(ROOT.drive.upper() == 'E:', 'Task output must remain on E:')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'CJK setup requires idle editor')
    records = json.loads((SOURCE.parent / 'provenance.json').read_text(encoding='utf-8'))
    record = next(row for row in records if row['filename'] == SOURCE.name)
    require(record['license'] == 'Apache 2.0', 'Unexpected CJK face license')
    require(hashlib.sha256(SOURCE.read_bytes()).hexdigest() == record['sha256'], 'CJK source hash mismatch')
    require((SOURCE.parent / record['license_file']).is_file(), 'Missing Apache license notice')
    lib = u.EditorAssetLibrary
    names = u.load_asset(NAMES_PATH)
    default_face = u.load_asset(DEFAULT_FACE_PATH)
    require(isinstance(names, u.Font) and names.get_editor_property('font_cache_type') == u.FontCacheType.RUNTIME,
            'Expected existing boss-name Runtime UFont')
    require(isinstance(default_face, u.FontFace), 'Existing default Cuprum FontFace is missing')
    require(Path(default_face.get_editor_property('source_filename')).resolve() == SOURCE.parent / 'MCBossNames-Regular.ttf',
            'Unexpected default boss-name source identity')
    default_source_sha = hashlib.sha256((SOURCE.parent / 'MCBossNames-Regular.ttf').read_bytes()).hexdigest()
    existed = lib.does_asset_exist(FACE_PATH)
    if not existed:
        factory = u.FontFileImportFactory()
        factory.set_editor_property('batch_create_font_asset', u.BatchCreateFontAsset.NO)
        task = u.AssetImportTask()
        for key, value in (('filename', str(SOURCE)), ('destination_path', FACE_PATH.rsplit('/', 1)[0]),
                           ('destination_name', FACE_PATH.rsplit('/', 1)[1]), ('factory', factory),
                           ('automated', True), ('replace_existing', False), ('save', False)):
            task.set_editor_property(key, value)
        u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    face = u.load_asset(FACE_PATH)
    require(isinstance(face, u.FontFace), 'CJK import did not create the expected FontFace')
    require(Path(face.get_editor_property('source_filename')).resolve() == SOURCE, 'Existing CJK face source identity differs')
    require(u.MCGameplayUIEditorLibrary.configure_boss_name_cjk_fallback(names, face), 'Native CJK fallback setup failed')
    # A failed prior run can leave an imported face in memory, without a package.
    face_file = ROOT / 'Content' / (FACE_PATH[len('/Game/'):] + '.uasset')
    if not face_file.is_file():
        require(lib.save_loaded_asset(face, only_if_is_dirty=False), 'Could not save the CJK FontFace')
    require(lib.save_loaded_asset(names, only_if_is_dirty=False), 'Could not save boss-name Runtime UFont')
    require(hashlib.sha256((SOURCE.parent / 'MCBossNames-Regular.ttf').read_bytes()).hexdigest() == default_source_sha,
            'Default Cuprum source was unexpectedly modified')
    report = {'status': 'passed', 'cjk_font_face': FACE_PATH, 'runtime_name_font': NAMES_PATH,
              'cjk_font_family': 'Droid Sans Fallback', 'license': record['license'],
              'source_sha256': record['sha256'], 'default_cuprum_source_unchanged': default_source_sha,
              'entry': 'MC_Boss_CJK_Fallback', 'scaling_factor': 1.0, 'cultures': '',
              'global_culture_changed': False, 'actual_CJK_glyph_render': 'pending',
              'alert_theme_font_CJK_coverage': 'separate check; this hook configures only boss-name font'}
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    u.log('MC_BOSS_CJK_FALLBACK_IMPORT_PASS ' + json.dumps(report, ensure_ascii=False))
    return report


if __name__ == '__main__':
    main()
