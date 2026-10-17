"""Reimport only the task-owned digit FontFace after the U+0025 adjustment.

Run inside idle UE Python. No new companion font is created; the existing
Runtime UFont is not edited or saved. No widget/theme/CDO properties change.
"""
import hashlib
import json
from pathlib import Path

import unreal as u


ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = (ROOT / 'ArtSource/UI/BossReferenceMatch_20261017/Fonts/MCBossDigits-Regular.ttf').resolve()
FACE_PATH = '/Game/Gameplay/UI/Fonts/FF_MCBossDigits_Regular'
FONT_PATH = FACE_PATH + '_Font'
REPORT = ROOT / 'Saved/GameplayUI/BossReference/digits_percent_reimport.json'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def package_file(path):
    require(path in (FACE_PATH, FONT_PATH), 'Unexpected package: ' + path)
    return ROOT / 'Content' / (path[len('/Game/'):] + '.uasset')


def main():
    require(ROOT.drive.upper() == 'E:', 'Task output must remain on drive E:')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Reimport requires idle editor')
    manifest = json.loads((SOURCE.parent / 'provenance.json').read_text(encoding='utf-8'))
    record = next(row for row in manifest if row['filename'] == SOURCE.name)
    require(record['unreal_font_face_asset'] == FACE_PATH and record['unreal_runtime_font_asset'] == FONT_PATH,
            'Provenance asset identity does not match this isolated task')
    require(hashlib.sha256(SOURCE.read_bytes()).hexdigest() == record['sha256'], 'Digit TTF SHA256 mismatch')
    require(record['percent_glyph_adjustment']['scale_y'] == .82, 'Missing approved percent adjustment')
    lib = u.EditorAssetLibrary
    face = u.load_asset(FACE_PATH)
    font = u.load_asset(FONT_PATH)
    require(isinstance(face, u.FontFace), 'Expected existing task-owned FontFace: ' + FACE_PATH)
    require(isinstance(font, u.Font), 'Expected existing companion Runtime UFont: ' + FONT_PATH)
    require(font.get_editor_property('font_cache_type') == u.FontCacheType.RUNTIME, 'Companion is not Runtime UFont')
    require(Path(face.get_editor_property('source_filename')).resolve() == SOURCE,
            'Existing FontFace source identity differs; refusing replacement')
    require(face.get_path_name() == FACE_PATH + '.' + FACE_PATH.rsplit('/', 1)[1], 'FontFace object identity differs')
    require(font.get_path_name() == FONT_PATH + '.' + FONT_PATH.rsplit('/', 1)[1], 'Companion object identity differs')
    require(package_file(FONT_PATH).is_file(), 'Save the existing companion before isolated reimport')
    companion_sha = hashlib.sha256(package_file(FONT_PATH).read_bytes()).hexdigest()
    original_settings = {key: face.get_editor_property(key) for key in ('hinting', 'loading_policy', 'layout_method')}

    factory = u.FontFileImportFactory()
    # Mirrors UFontFileImportFactory::Reimport: never create another UFont.
    factory.set_editor_property('batch_create_font_asset', u.BatchCreateFontAsset.NO)
    task = u.AssetImportTask()
    for key, value in (('filename', str(SOURCE)), ('destination_path', FACE_PATH.rsplit('/', 1)[0]),
                       ('destination_name', FACE_PATH.rsplit('/', 1)[1]), ('factory', factory),
                       ('automated', True), ('replace_existing', True),
                       ('replace_existing_settings', False), ('save', False)):
        task.set_editor_property(key, value)
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    updated = u.load_asset(FACE_PATH)
    companion = u.load_asset(FONT_PATH)
    require(isinstance(updated, u.FontFace), 'Reimport did not return the expected FontFace')
    require(updated == face, 'Reimport changed FontFace object identity')
    require(companion == font and isinstance(companion, u.Font), 'Companion Runtime UFont identity changed')
    require(companion.get_editor_property('font_cache_type') == u.FontCacheType.RUNTIME, 'Companion cache type changed')
    require(Path(updated.get_editor_property('source_filename')).resolve() == SOURCE, 'Reimport source identity changed')
    for key, value in original_settings.items():
        updated.set_editor_property(key, value)
    # UFontFace::PostEditChangeProperty flushes Slate's cached glyphs. Notify
    # even for an identical hinting value; no global console/culture mutation.
    updated.set_editor_property('hinting', original_settings['hinting'],
                                notify_mode=u.PropertyAccessChangeNotifyMode.ALWAYS)
    require(lib.save_loaded_asset(updated, only_if_is_dirty=False), 'Could not save the updated FontFace')
    require(hashlib.sha256(package_file(FONT_PATH).read_bytes()).hexdigest() == companion_sha,
            'Companion package changed during isolated FontFace reimport')
    report = {'status': 'passed', 'font_face': FACE_PATH, 'runtime_font': FONT_PATH,
              'source': str(SOURCE), 'source_sha256': record['sha256'],
              'percent_scale_y': .82, 'font_face_identity_preserved': True,
              'companion_identity_preserved': True, 'companion_disk_sha256_unchanged': companion_sha,
              'companion_saved': False, 'glyph_cache_flush': 'FontFace PostEditChangeProperty',
              'protected_typeface_inspection': 'unavailable; original companion not replaced',
              'actual_render_qa': 'pending'}
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    u.log('MC_BOSS_DIGITS_REIMPORT_PASS ' + json.dumps(report, ensure_ascii=False))
    return report


if __name__ == '__main__':
    main()
